/*
 * Copyright (c) 2018, 2019 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/syslimits.h>
#include <sys/resource.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sha1.h>
#include <zlib.h>
#include <ctype.h>
#include <libgen.h>
#include <limits.h>
#include <imsg.h>
#include <time.h>

#include "got_error.h"
#include "got_object.h"
#include "got_repository.h"
#include "got_opentemp.h"
#include "got_path.h"

#include "got_lib_sha1.h"
#include "got_lib_delta.h"
#include "got_lib_inflate.h"
#include "got_lib_object.h"
#include "got_lib_privsep.h"
#include "got_lib_object_idcache.h"
#include "got_lib_object_cache.h"
#include "got_lib_object_parse.h"
#include "got_lib_pack.h"
#include "got_lib_repository.h"

#ifndef MIN
#define	MIN(_a,_b) ((_a) < (_b) ? (_a) : (_b))
#endif

struct got_object_id *
got_object_get_id(struct got_object *obj)
{
	return &obj->id;
}

const struct got_error *
got_object_get_id_str(char **outbuf, struct got_object *obj)
{
	return got_object_id_str(outbuf, &obj->id);
}

const struct got_error *
got_object_get_type(int *type, struct got_repository *repo,
    struct got_object_id *id)
{
	const struct got_error *err = NULL;
	struct got_object *obj;

	err = got_object_open(&obj, repo, id);
	if (err)
		return err;

	switch (obj->type) {
	case GOT_OBJ_TYPE_COMMIT:
	case GOT_OBJ_TYPE_TREE:
	case GOT_OBJ_TYPE_BLOB:
	case GOT_OBJ_TYPE_TAG:
		*type = obj->type;
		break;
	default:
		err = got_error(GOT_ERR_OBJ_TYPE);
		break;
	}

	got_object_close(obj);
	return err;
}

const struct got_error *
got_object_get_path(char **path, struct got_object_id *id,
    struct got_repository *repo)
{
	const struct got_error *err = NULL;
	char *hex = NULL;
	char *path_objects;

	*path = NULL;

	path_objects = got_repo_get_path_objects(repo);
	if (path_objects == NULL)
		return got_error_from_errno("got_repo_get_path_objects");

	err = got_object_id_str(&hex, id);
	if (err)
		goto done;

	if (asprintf(path, "%s/%.2x/%s", path_objects,
	    id->sha1[0], hex + 2) == -1)
		err = got_error_from_errno("asprintf");

done:
	free(hex);
	free(path_objects);
	return err;
}

static const struct got_error *
open_loose_object(int *fd, struct got_object_id *id,
    struct got_repository *repo)
{
	const struct got_error *err = NULL;
	char *path;

	err = got_object_get_path(&path, id, repo);
	if (err)
		return err;
	*fd = open(path, O_RDONLY | O_NOFOLLOW);
	if (*fd == -1) {
		err = got_error_from_errno2("open", path);
		goto done;
	}
done:
	free(path);
	return err;
}

static const struct got_error *
get_packfile_path(char **path_packfile, struct got_packidx *packidx)
{
	size_t size;

	/* Packfile path contains ".pack" instead of ".idx", so add one byte. */
	size = strlen(packidx->path_packidx) + 2;
	if (size < GOT_PACKFILE_NAMELEN + 1)
		return got_error_path(packidx->path_packidx, GOT_ERR_BAD_PATH);

	*path_packfile = malloc(size);
	if (*path_packfile == NULL)
		return got_error_from_errno("malloc");

	/* Copy up to and excluding ".idx". */
	if (strlcpy(*path_packfile, packidx->path_packidx,
	    size - strlen(GOT_PACKIDX_SUFFIX) - 1) >= size)
		return got_error(GOT_ERR_NO_SPACE);

	if (strlcat(*path_packfile, GOT_PACKFILE_SUFFIX, size) >= size)
		return got_error(GOT_ERR_NO_SPACE);

	return NULL;
}

static const struct got_error *
request_packed_object(struct got_object **obj, struct got_pack *pack, int idx,
    struct got_object_id *id)
{
	const struct got_error *err = NULL;
	struct imsgbuf *ibuf = pack->privsep_child->ibuf;

	err = got_privsep_send_packed_obj_req(ibuf, idx, id);
	if (err)
		return err;

	err = got_privsep_recv_obj(obj, ibuf);
	if (err)
		return err;

	memcpy(&(*obj)->id, id, sizeof((*obj)->id));

	return NULL;
}

static void
set_max_datasize(void)
{
	struct rlimit rl;

	if (getrlimit(RLIMIT_DATA, &rl) != 0)
		return;

	rl.rlim_cur = rl.rlim_max;
	setrlimit(RLIMIT_DATA, &rl);
}

static const struct got_error *
start_pack_privsep_child(struct got_pack *pack, struct got_packidx *packidx)
{
	const struct got_error *err = NULL;
	int imsg_fds[2];
	pid_t pid;
	struct imsgbuf *ibuf;

	ibuf = calloc(1, sizeof(*ibuf));
	if (ibuf == NULL)
		return got_error_from_errno("calloc");

	pack->privsep_child = calloc(1, sizeof(*pack->privsep_child));
	if (pack->privsep_child == NULL) {
		err = got_error_from_errno("calloc");
		free(ibuf);
		return err;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, imsg_fds) == -1) {
		err = got_error_from_errno("socketpair");
		goto done;
	}

	pid = fork();
	if (pid == -1) {
		err = got_error_from_errno("fork");
		goto done;
	} else if (pid == 0) {
		set_max_datasize();
		got_privsep_exec_child(imsg_fds, GOT_PATH_PROG_READ_PACK,
		    pack->path_packfile);
		/* not reached */
	}

	if (close(imsg_fds[1]) != 0)
		return got_error_from_errno("close");
	pack->privsep_child->imsg_fd = imsg_fds[0];
	pack->privsep_child->pid = pid;
	imsg_init(ibuf, imsg_fds[0]);
	pack->privsep_child->ibuf = ibuf;

	err = got_privsep_init_pack_child(ibuf, pack, packidx);
	if (err) {
		const struct got_error *child_err;
		err = got_privsep_send_stop(pack->privsep_child->imsg_fd);
		child_err = got_privsep_wait_for_child(
		    pack->privsep_child->pid);
		if (child_err && err == NULL)
			err = child_err;
	}
done:
	if (err) {
		free(ibuf);
		free(pack->privsep_child);
		pack->privsep_child = NULL;
	}
	return err;
}

static const struct got_error *
read_packed_object_privsep(struct got_object **obj,
    struct got_repository *repo, struct got_pack *pack,
    struct got_packidx *packidx, int idx, struct got_object_id *id)
{
	const struct got_error *err = NULL;

	if (pack->privsep_child)
		return request_packed_object(obj, pack, idx, id);

	err = start_pack_privsep_child(pack, packidx);
	if (err)
		return err;

	return request_packed_object(obj, pack, idx, id);
}


static const struct got_error *
open_packed_object(struct got_object **obj, struct got_object_id *id,
    struct got_repository *repo)
{
	const struct got_error *err = NULL;
	struct got_pack *pack = NULL;
	struct got_packidx *packidx = NULL;
	int idx;
	char *path_packfile;

	err = got_repo_search_packidx(&packidx, &idx, repo, id);
	if (err)
		return err;

	err = get_packfile_path(&path_packfile, packidx);
	if (err)
		return err;

	pack = got_repo_get_cached_pack(repo, path_packfile);
	if (pack == NULL) {
		err = got_repo_cache_pack(&pack, repo, path_packfile, packidx);
		if (err)
			goto done;
	}

	err = read_packed_object_privsep(obj, repo, pack, packidx, idx, id);
	if (err)
		goto done;
done:
	free(path_packfile);
	return err;
}

static const struct got_error *
request_object(struct got_object **obj, struct got_repository *repo, int fd)
{
	const struct got_error *err = NULL;
	struct imsgbuf *ibuf;

	ibuf = repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_OBJECT].ibuf;

	err = got_privsep_send_obj_req(ibuf, fd);
	if (err)
		return err;

	return got_privsep_recv_obj(obj, ibuf);
}

static const struct got_error *
read_object_header_privsep(struct got_object **obj, struct got_repository *repo,
    int obj_fd)
{
	const struct got_error *err;
	int imsg_fds[2];
	pid_t pid;
	struct imsgbuf *ibuf;

	if (repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_OBJECT].imsg_fd != -1)
		return request_object(obj, repo, obj_fd);

	ibuf = calloc(1, sizeof(*ibuf));
	if (ibuf == NULL)
		return got_error_from_errno("calloc");

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, imsg_fds) == -1) {
		err = got_error_from_errno("socketpair");
		free(ibuf);
		return err;
	}

	pid = fork();
	if (pid == -1) {
		err = got_error_from_errno("fork");
		free(ibuf);
		return err;
	}
	else if (pid == 0) {
		got_privsep_exec_child(imsg_fds, GOT_PATH_PROG_READ_OBJECT,
		    repo->path);
		/* not reached */
	}

	if (close(imsg_fds[1]) != 0) {
		err = got_error_from_errno("close");
		free(ibuf);
		return err;
	}
	repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_OBJECT].imsg_fd =
	    imsg_fds[0];
	repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_OBJECT].pid = pid;
	imsg_init(ibuf, imsg_fds[0]);
	repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_OBJECT].ibuf = ibuf;

	return request_object(obj, repo, obj_fd);
}


const struct got_error *
got_object_open(struct got_object **obj, struct got_repository *repo,
    struct got_object_id *id)
{
	const struct got_error *err = NULL;
	char *path;
	int fd;

	*obj = got_repo_get_cached_object(repo, id);
	if (*obj != NULL) {
		(*obj)->refcnt++;
		return NULL;
	}

	err = open_packed_object(obj, id, repo);
	if (err && err->code != GOT_ERR_NO_OBJ)
		return err;
	if (*obj) {
		(*obj)->refcnt++;
		return got_repo_cache_object(repo, id, *obj);
	}

	err = got_object_get_path(&path, id, repo);
	if (err)
		return err;

	fd = open(path, O_RDONLY | O_NOFOLLOW);
	if (fd == -1) {
		if (errno == ENOENT)
			err = got_error_no_obj(id);
		else
			err = got_error_from_errno2("open", path);
		goto done;
	} else {
		err = read_object_header_privsep(obj, repo, fd);
		if (err)
			goto done;
		memcpy((*obj)->id.sha1, id->sha1, SHA1_DIGEST_LENGTH);
	}

	(*obj)->refcnt++;
	err = got_repo_cache_object(repo, id, *obj);
done:
	free(path);
	return err;

}

const struct got_error *
got_object_open_by_id_str(struct got_object **obj, struct got_repository *repo,
    const char *id_str)
{
	struct got_object_id id;

	if (!got_parse_sha1_digest(id.sha1, id_str))
		return got_error_path(id_str, GOT_ERR_BAD_OBJ_ID_STR);

	return got_object_open(obj, repo, &id);
}

const struct got_error *
got_object_resolve_id_str(struct got_object_id **id,
    struct got_repository *repo, const char *id_str)
{
	const struct got_error *err = NULL;
	struct got_object *obj;

	err = got_object_open_by_id_str(&obj, repo, id_str);
	if (err)
		return err;

	*id = got_object_id_dup(got_object_get_id(obj));
	got_object_close(obj);
	if (*id == NULL)
		return got_error_from_errno("got_object_id_dup");

	return NULL;
}

static const struct got_error *
request_packed_commit(struct got_commit_object **commit, struct got_pack *pack,
    int pack_idx, struct got_object_id *id)
{
	const struct got_error *err = NULL;

	err = got_privsep_send_commit_req(pack->privsep_child->ibuf, -1, id,
	    pack_idx);
	if (err)
		return err;

	err = got_privsep_recv_commit(commit, pack->privsep_child->ibuf);
	if (err)
		return err;

	(*commit)->flags |= GOT_COMMIT_FLAG_PACKED;
	return NULL;
}

static const struct got_error *
read_packed_commit_privsep(struct got_commit_object **commit,
    struct got_pack *pack, struct got_packidx *packidx, int idx,
    struct got_object_id *id)
{
	const struct got_error *err = NULL;

	if (pack->privsep_child)
		return request_packed_commit(commit, pack, idx, id);

	err = start_pack_privsep_child(pack, packidx);
	if (err)
		return err;

	return request_packed_commit(commit, pack, idx, id);
}

static const struct got_error *
request_commit(struct got_commit_object **commit, struct got_repository *repo,
    int fd)
{
	const struct got_error *err = NULL;
	struct imsgbuf *ibuf;

	ibuf = repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_COMMIT].ibuf;

	err = got_privsep_send_commit_req(ibuf, fd, NULL, -1);
	if (err)
		return err;

	return got_privsep_recv_commit(commit, ibuf);
}

static const struct got_error *
read_commit_privsep(struct got_commit_object **commit, int obj_fd,
    struct got_repository *repo)
{
	const struct got_error *err;
	int imsg_fds[2];
	pid_t pid;
	struct imsgbuf *ibuf;

	if (repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_COMMIT].imsg_fd != -1)
		return request_commit(commit, repo, obj_fd);

	ibuf = calloc(1, sizeof(*ibuf));
	if (ibuf == NULL)
		return got_error_from_errno("calloc");

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, imsg_fds) == -1) {
		err = got_error_from_errno("socketpair");
		free(ibuf);
		return err;
	}

	pid = fork();
	if (pid == -1) {
		err = got_error_from_errno("fork");
		free(ibuf);
		return err;
	}
	else if (pid == 0) {
		got_privsep_exec_child(imsg_fds, GOT_PATH_PROG_READ_COMMIT,
		    repo->path);
		/* not reached */
	}

	if (close(imsg_fds[1]) != 0) {
		err = got_error_from_errno("close");
		free(ibuf);
		return err;
	}
	repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_COMMIT].imsg_fd =
	    imsg_fds[0];
	repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_COMMIT].pid = pid;
	imsg_init(ibuf, imsg_fds[0]);
	repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_COMMIT].ibuf = ibuf;

	return request_commit(commit, repo, obj_fd);
}


static const struct got_error *
open_commit(struct got_commit_object **commit,
    struct got_repository *repo, struct got_object_id *id, int check_cache)
{
	const struct got_error *err = NULL;
	struct got_packidx *packidx = NULL;
	int idx;
	char *path_packfile = NULL;

	if (check_cache) {
		*commit = got_repo_get_cached_commit(repo, id);
		if (*commit != NULL) {
			(*commit)->refcnt++;
			return NULL;
		}
	} else
		*commit = NULL;

	err = got_repo_search_packidx(&packidx, &idx, repo, id);
	if (err == NULL) {
		struct got_pack *pack = NULL;

		err = get_packfile_path(&path_packfile, packidx);
		if (err)
			return err;

		pack = got_repo_get_cached_pack(repo, path_packfile);
		if (pack == NULL) {
			err = got_repo_cache_pack(&pack, repo, path_packfile,
			    packidx);
			if (err)
				goto done;
		}
		err = read_packed_commit_privsep(commit, pack,
		    packidx, idx, id);
	} else if (err->code == GOT_ERR_NO_OBJ) {
		int fd;

		err = open_loose_object(&fd, id, repo);
		if (err)
			return err;
		err = read_commit_privsep(commit, fd, repo);
	}

	if (err == NULL) {
		(*commit)->refcnt++;
		err = got_repo_cache_commit(repo, id, *commit);
	}
done:
	free(path_packfile);
	return err;
}

const struct got_error *
got_object_open_as_commit(struct got_commit_object **commit,
    struct got_repository *repo, struct got_object_id *id)
{
	*commit = got_repo_get_cached_commit(repo, id);
	if (*commit != NULL) {
		(*commit)->refcnt++;
		return NULL;
	}

	return open_commit(commit, repo, id, 0);
}

const struct got_error *
got_object_commit_open(struct got_commit_object **commit,
    struct got_repository *repo, struct got_object *obj)
{
	return open_commit(commit, repo, got_object_get_id(obj), 1);
}

const struct got_error *
got_object_qid_alloc(struct got_object_qid **qid, struct got_object_id *id)
{
	const struct got_error *err = NULL;

	*qid = calloc(1, sizeof(**qid));
	if (*qid == NULL)
		return got_error_from_errno("calloc");

	(*qid)->id = got_object_id_dup(id);
	if ((*qid)->id == NULL) {
		err = got_error_from_errno("got_object_id_dup");
		got_object_qid_free(*qid);
		*qid = NULL;
		return err;
	}

	return NULL;
}

static const struct got_error *
request_packed_tree(struct got_tree_object **tree, struct got_pack *pack,
    int pack_idx, struct got_object_id *id)
{
	const struct got_error *err = NULL;

	err = got_privsep_send_tree_req(pack->privsep_child->ibuf, -1, id,
	    pack_idx);
	if (err)
		return err;

	return got_privsep_recv_tree(tree, pack->privsep_child->ibuf);
}

static const struct got_error *
read_packed_tree_privsep(struct got_tree_object **tree,
    struct got_pack *pack, struct got_packidx *packidx, int idx,
    struct got_object_id *id)
{
	const struct got_error *err = NULL;

	if (pack->privsep_child)
		return request_packed_tree(tree, pack, idx, id);

	err = start_pack_privsep_child(pack, packidx);
	if (err)
		return err;

	return request_packed_tree(tree, pack, idx, id);
}

static const struct got_error *
request_tree(struct got_tree_object **tree, struct got_repository *repo,
    int fd)
{
	const struct got_error *err = NULL;
	struct imsgbuf *ibuf;

	ibuf = repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_TREE].ibuf;

	err = got_privsep_send_tree_req(ibuf, fd, NULL, -1);
	if (err)
		return err;

	return got_privsep_recv_tree(tree, ibuf);
}

const struct got_error *
read_tree_privsep(struct got_tree_object **tree, int obj_fd,
    struct got_repository *repo)
{
	const struct got_error *err;
	int imsg_fds[2];
	pid_t pid;
	struct imsgbuf *ibuf;

	if (repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_TREE].imsg_fd != -1)
		return request_tree(tree, repo, obj_fd);

	ibuf = calloc(1, sizeof(*ibuf));
	if (ibuf == NULL)
		return got_error_from_errno("calloc");

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, imsg_fds) == -1) {
		err = got_error_from_errno("socketpair");
		free(ibuf);
		return err;
	}

	pid = fork();
	if (pid == -1) {
		err = got_error_from_errno("fork");
		free(ibuf);
		return err;
	}
	else if (pid == 0) {
		got_privsep_exec_child(imsg_fds, GOT_PATH_PROG_READ_TREE,
		    repo->path);
		/* not reached */
	}

	if (close(imsg_fds[1]) != 0) {
		err = got_error_from_errno("close");
		free(ibuf);
		return err;
	}
	repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_TREE].imsg_fd =
	    imsg_fds[0];
	repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_TREE].pid = pid;
	imsg_init(ibuf, imsg_fds[0]);
	repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_TREE].ibuf = ibuf;


	return request_tree(tree, repo, obj_fd);
}

static const struct got_error *
open_tree(struct got_tree_object **tree, struct got_repository *repo,
    struct got_object_id *id, int check_cache)
{
	const struct got_error *err = NULL;
	struct got_packidx *packidx = NULL;
	int idx;
	char *path_packfile = NULL;

	if (check_cache) {
		*tree = got_repo_get_cached_tree(repo, id);
		if (*tree != NULL) {
			(*tree)->refcnt++;
			return NULL;
		}
	} else
		*tree = NULL;

	err = got_repo_search_packidx(&packidx, &idx, repo, id);
	if (err == NULL) {
		struct got_pack *pack = NULL;

		err = get_packfile_path(&path_packfile, packidx);
		if (err)
			return err;

		pack = got_repo_get_cached_pack(repo, path_packfile);
		if (pack == NULL) {
			err = got_repo_cache_pack(&pack, repo, path_packfile,
			    packidx);
			if (err)
				goto done;
		}
		err = read_packed_tree_privsep(tree, pack,
		    packidx, idx, id);
	} else if (err->code == GOT_ERR_NO_OBJ) {
		int fd;

		err = open_loose_object(&fd, id, repo);
		if (err)
			return err;
		err = read_tree_privsep(tree, fd, repo);
	}

	if (err == NULL) {
		(*tree)->refcnt++;
		err = got_repo_cache_tree(repo, id, *tree);
	}
done:
	free(path_packfile);
	return err;
}

const struct got_error *
got_object_open_as_tree(struct got_tree_object **tree,
    struct got_repository *repo, struct got_object_id *id)
{
	*tree = got_repo_get_cached_tree(repo, id);
	if (*tree != NULL) {
		(*tree)->refcnt++;
		return NULL;
	}

	return open_tree(tree, repo, id, 0);
}

const struct got_error *
got_object_tree_open(struct got_tree_object **tree,
    struct got_repository *repo, struct got_object *obj)
{
	return open_tree(tree, repo, got_object_get_id(obj), 1);
}

int
got_object_tree_get_nentries(struct got_tree_object *tree)
{
	return tree->nentries;
}

struct got_tree_entry *
got_object_tree_get_first_entry(struct got_tree_object *tree)
{
	return got_object_tree_get_entry(tree, 0);
}

struct got_tree_entry *
got_object_tree_get_last_entry(struct got_tree_object *tree)
{
	return got_object_tree_get_entry(tree, tree->nentries - 1);
}

struct got_tree_entry *
got_object_tree_get_entry(struct got_tree_object *tree, int i)
{
	if (i < 0 || i >= tree->nentries)
		return NULL;
	return &tree->entries[i];
}

mode_t
got_tree_entry_get_mode(struct got_tree_entry *te)
{
	return te->mode;
}

const char *
got_tree_entry_get_name(struct got_tree_entry *te)
{
	return &te->name[0];
}

struct got_object_id *
got_tree_entry_get_id(struct got_tree_entry *te)
{
	return &te->id;
}

const struct got_error *
got_object_blob_read_to_str(char **s, struct got_blob_object *blob)
{
	const struct got_error *err = NULL;
	size_t len, totlen, hdrlen, offset;

	hdrlen = got_object_blob_get_hdrlen(blob);
	totlen = 0;
	offset = 0;
	do {
		char *p;

		err = got_object_blob_read_block(&len, blob);
		if (err)
			return err;

		if (len == 0)
			break;

		totlen += len - hdrlen;
		p = realloc(*s, totlen + 1);
		if (p == NULL) {
			err = got_error_from_errno("realloc");
			free(*s);
			*s = NULL;
			return err;
		}
		*s = p;
		/* Skip blob object header first time around. */
		memcpy(*s + offset,
		    got_object_blob_get_read_buf(blob) + hdrlen, len - hdrlen);
		hdrlen = 0;
		offset = totlen;
	} while (len > 0);

	(*s)[totlen] = '\0';
	return NULL;
}

const struct got_error *
got_tree_entry_get_symlink_target(char **link_target, struct got_tree_entry *te,
    struct got_repository *repo)
{
	const struct got_error *err = NULL;
	struct got_blob_object *blob = NULL;

	*link_target = NULL;

	if (!got_object_tree_entry_is_symlink(te))
		return got_error(GOT_ERR_TREE_ENTRY_TYPE);

	err = got_object_open_as_blob(&blob, repo,
	    got_tree_entry_get_id(te), PATH_MAX);
	if (err)
		return err;

	err = got_object_blob_read_to_str(link_target, blob);
	got_object_blob_close(blob);
	if (err) {
		free(*link_target);
		*link_target = NULL;
	}
	return err;
}

int
got_tree_entry_get_index(struct got_tree_entry *te)
{
	return te->idx;
}

struct got_tree_entry *
got_tree_entry_get_next(struct got_tree_object *tree,
    struct got_tree_entry *te)
{
	return got_object_tree_get_entry(tree, te->idx + 1);
}

struct got_tree_entry *
got_tree_entry_get_prev(struct got_tree_object *tree,
    struct got_tree_entry *te)
{
	return got_object_tree_get_entry(tree, te->idx - 1);
}

static const struct got_error *
request_packed_blob(uint8_t **outbuf, size_t *size, size_t *hdrlen, int outfd,
    struct got_pack *pack, struct got_packidx *packidx, int idx,
    struct got_object_id *id)
{
	const struct got_error *err = NULL;
	int outfd_child;
	int basefd, accumfd; /* temporary files for delta application */

	basefd = got_opentempfd();
	if (basefd == -1)
		return got_error_from_errno("got_opentempfd");
	accumfd = got_opentempfd();
	if (accumfd == -1)
		return got_error_from_errno("got_opentempfd");

	outfd_child = dup(outfd);
	if (outfd_child == -1)
		return got_error_from_errno("dup");

	err = got_privsep_send_blob_req(pack->privsep_child->ibuf, -1, id, idx);
	if (err)
		return err;

	err = got_privsep_send_blob_outfd(pack->privsep_child->ibuf,
	    outfd_child);
	if (err) {
		close(basefd);
		close(accumfd);
		return err;
	}

	err = got_privsep_send_tmpfd(pack->privsep_child->ibuf,
	    basefd);
	if (err) {
		close(accumfd);
		return err;
	}

	err = got_privsep_send_tmpfd(pack->privsep_child->ibuf,
	    accumfd);
	if (err)
		return err;

	err = got_privsep_recv_blob(outbuf, size, hdrlen,
	    pack->privsep_child->ibuf);
	if (err)
		return err;

	if (lseek(outfd, SEEK_SET, 0) == -1)
		err = got_error_from_errno("lseek");

	return err;
}

static const struct got_error *
read_packed_blob_privsep(uint8_t **outbuf, size_t *size, size_t *hdrlen,
    int outfd, struct got_pack *pack, struct got_packidx *packidx, int idx,
    struct got_object_id *id)
{
	const struct got_error *err = NULL;

	if (pack->privsep_child == NULL) {
		err = start_pack_privsep_child(pack, packidx);
		if (err)
			return err;
	}

	return request_packed_blob(outbuf, size, hdrlen, outfd, pack, packidx,
	    idx, id);
}

static const struct got_error *
request_blob(uint8_t **outbuf, size_t *size, size_t *hdrlen, int outfd,
    int infd, struct imsgbuf *ibuf)
{
	const struct got_error *err = NULL;
	int outfd_child;

	outfd_child = dup(outfd);
	if (outfd_child == -1)
		return got_error_from_errno("dup");

	err = got_privsep_send_blob_req(ibuf, infd, NULL, -1);
	if (err)
		return err;

	err = got_privsep_send_blob_outfd(ibuf, outfd_child);
	if (err)
		return err;

	err = got_privsep_recv_blob(outbuf, size, hdrlen, ibuf);
	if (err)
		return err;

	if (lseek(outfd, SEEK_SET, 0) == -1)
		return got_error_from_errno("lseek");

	return err;
}

static const struct got_error *
read_blob_privsep(uint8_t **outbuf, size_t *size, size_t *hdrlen,
    int outfd, int infd, struct got_repository *repo)
{
	const struct got_error *err;
	int imsg_fds[2];
	pid_t pid;
	struct imsgbuf *ibuf;

	if (repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_BLOB].imsg_fd != -1) {
		ibuf = repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_BLOB].ibuf;
		return request_blob(outbuf, size, hdrlen, outfd, infd, ibuf);
	}

	ibuf = calloc(1, sizeof(*ibuf));
	if (ibuf == NULL)
		return got_error_from_errno("calloc");

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, imsg_fds) == -1) {
		err = got_error_from_errno("socketpair");
		free(ibuf);
		return err;
	}

	pid = fork();
	if (pid == -1) {
		err = got_error_from_errno("fork");
		free(ibuf);
		return err;
	}
	else if (pid == 0) {
		got_privsep_exec_child(imsg_fds, GOT_PATH_PROG_READ_BLOB,
		    repo->path);
		/* not reached */
	}

	if (close(imsg_fds[1]) != 0) {
		err = got_error_from_errno("close");
		free(ibuf);
		return err;
	}
	repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_BLOB].imsg_fd =
	    imsg_fds[0];
	repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_BLOB].pid = pid;
	imsg_init(ibuf, imsg_fds[0]);
	repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_BLOB].ibuf = ibuf;

	return request_blob(outbuf, size, hdrlen, outfd, infd, ibuf);
}

static const struct got_error *
open_blob(struct got_blob_object **blob, struct got_repository *repo,
    struct got_object_id *id, size_t blocksize)
{
	const struct got_error *err = NULL;
	struct got_packidx *packidx = NULL;
	int idx;
	char *path_packfile = NULL;
	uint8_t *outbuf;
	int outfd;
	size_t size, hdrlen;
	struct stat sb;

	*blob = calloc(1, sizeof(**blob));
	if (*blob == NULL)
		return got_error_from_errno("calloc");

	outfd = got_opentempfd();
	if (outfd == -1)
		return got_error_from_errno("got_opentempfd");

	(*blob)->read_buf = malloc(blocksize);
	if ((*blob)->read_buf == NULL) {
		err = got_error_from_errno("malloc");
		goto done;
	}

	err = got_repo_search_packidx(&packidx, &idx, repo, id);
	if (err == NULL) {
		struct got_pack *pack = NULL;

		err = get_packfile_path(&path_packfile, packidx);
		if (err)
			goto done;

		pack = got_repo_get_cached_pack(repo, path_packfile);
		if (pack == NULL) {
			err = got_repo_cache_pack(&pack, repo, path_packfile,
			    packidx);
			if (err)
				goto done;
		}
		err = read_packed_blob_privsep(&outbuf, &size, &hdrlen, outfd,
		    pack, packidx, idx, id);
	} else if (err->code == GOT_ERR_NO_OBJ) {
		int infd;

		err = open_loose_object(&infd, id, repo);
		if (err)
			goto done;
		err = read_blob_privsep(&outbuf, &size, &hdrlen, outfd, infd,
		    repo);
	}
	if (err)
		goto done;

	if (hdrlen > size) {
		err = got_error(GOT_ERR_BAD_OBJ_HDR);
		goto done;
	}

	if (outbuf) {
		if (close(outfd) != 0 && err == NULL)
			err = got_error_from_errno("close");
		outfd = -1;
		(*blob)->f = fmemopen(outbuf, size, "rb");
		if ((*blob)->f == NULL) {
			err = got_error_from_errno("fmemopen");
			free(outbuf);
			goto done;
		}
		(*blob)->data = outbuf;
	} else {
		if (fstat(outfd, &sb) == -1) {
			err = got_error_from_errno("fstat");
			goto done;
		}

		if (sb.st_size != size) {
			err = got_error(GOT_ERR_PRIVSEP_LEN);
			goto done;
		}

		(*blob)->f = fdopen(outfd, "rb");
		if ((*blob)->f == NULL) {
			err = got_error_from_errno("fdopen");
			close(outfd);
			outfd = -1;
			goto done;
		}
	}

	(*blob)->hdrlen = hdrlen;
	(*blob)->blocksize = blocksize;
	memcpy(&(*blob)->id.sha1, id->sha1, SHA1_DIGEST_LENGTH);

done:
	free(path_packfile);
	if (err) {
		if (*blob) {
			got_object_blob_close(*blob);
			*blob = NULL;
		} else if (outfd != -1)
			close(outfd);
	}
	return err;
}

const struct got_error *
got_object_open_as_blob(struct got_blob_object **blob,
    struct got_repository *repo, struct got_object_id *id,
    size_t blocksize)
{
	return open_blob(blob, repo, id, blocksize);
}

const struct got_error *
got_object_blob_open(struct got_blob_object **blob,
    struct got_repository *repo, struct got_object *obj, size_t blocksize)
{
	return open_blob(blob, repo, got_object_get_id(obj), blocksize);
}

const struct got_error *
got_object_blob_close(struct got_blob_object *blob)
{
	const struct got_error *err = NULL;
	free(blob->read_buf);
	if (blob->f && fclose(blob->f) != 0)
		err = got_error_from_errno("fclose");
	free(blob->data);
	free(blob);
	return err;
}

void
got_object_blob_rewind(struct got_blob_object *blob)
{
	if (blob->f)
		rewind(blob->f);
}

char *
got_object_blob_id_str(struct got_blob_object *blob, char *buf, size_t size)
{
	return got_sha1_digest_to_str(blob->id.sha1, buf, size);
}

size_t
got_object_blob_get_hdrlen(struct got_blob_object *blob)
{
	return blob->hdrlen;
}

const uint8_t *
got_object_blob_get_read_buf(struct got_blob_object *blob)
{
	return blob->read_buf;
}

const struct got_error *
got_object_blob_read_block(size_t *outlenp, struct got_blob_object *blob)
{
	size_t n;

	n = fread(blob->read_buf, 1, blob->blocksize, blob->f);
	if (n == 0 && ferror(blob->f))
		return got_ferror(blob->f, GOT_ERR_IO);
	*outlenp = n;
	return NULL;
}

const struct got_error *
got_object_blob_dump_to_file(size_t *filesize, int *nlines,
    off_t **line_offsets, FILE *outfile, struct got_blob_object *blob)
{
	const struct got_error *err = NULL;
	size_t n, len, hdrlen;
	const uint8_t *buf;
	int i;
	size_t noffsets = 0;
	off_t off = 0, total_len = 0;

	if (line_offsets)
		*line_offsets = NULL;
	if (filesize)
		*filesize = 0;
	if (nlines)
		*nlines = 0;

	hdrlen = got_object_blob_get_hdrlen(blob);
	do {
		err = got_object_blob_read_block(&len, blob);
		if (err)
			return err;
		if (len == 0)
			break;
		buf = got_object_blob_get_read_buf(blob);
		i = hdrlen;
		if (line_offsets && nlines) {
			if (*line_offsets == NULL) {
				/* Have some data but perhaps no '\n'. */
				noffsets = 1;
				*nlines = 1;
				*line_offsets = calloc(1, sizeof(**line_offsets));
				if (*line_offsets == NULL)
					return got_error_from_errno("calloc");

				/* Skip forward over end of first line. */
				while (i < len) {
					if (buf[i] == '\n')
						break;
					i++;
				}
			}
			/* Scan '\n' offsets in remaining chunk of data. */
			while (i < len) {
				if (buf[i] != '\n') {
					i++;
					continue;
				}
				(*nlines)++;
				if (noffsets < *nlines) {
					off_t *o = recallocarray(*line_offsets,
					    noffsets, *nlines,
					    sizeof(**line_offsets));
					if (o == NULL) {
						free(*line_offsets);
						*line_offsets = NULL;
						return got_error_from_errno(
						    "recallocarray");
					}
					*line_offsets = o;
					noffsets = *nlines;
				}
				off = total_len + i - hdrlen + 1;
				(*line_offsets)[*nlines - 1] = off;
				i++;
			}
		}
		/* Skip blob object header first time around. */
		n = fwrite(buf + hdrlen, 1, len - hdrlen, outfile);
		if (n != len - hdrlen)
			return got_ferror(outfile, GOT_ERR_IO);
		total_len += len - hdrlen;
		hdrlen = 0;
	} while (len != 0);

	if (fflush(outfile) != 0)
		return got_error_from_errno("fflush");
	rewind(outfile);

	if (filesize)
		*filesize = total_len;

	return NULL;
}

static const struct got_error *
request_packed_tag(struct got_tag_object **tag, struct got_pack *pack,
    int pack_idx, struct got_object_id *id)
{
	const struct got_error *err = NULL;

	err = got_privsep_send_tag_req(pack->privsep_child->ibuf, -1, id,
	    pack_idx);
	if (err)
		return err;

	return got_privsep_recv_tag(tag, pack->privsep_child->ibuf);
}

static const struct got_error *
read_packed_tag_privsep(struct got_tag_object **tag,
    struct got_pack *pack, struct got_packidx *packidx, int idx,
    struct got_object_id *id)
{
	const struct got_error *err = NULL;

	if (pack->privsep_child)
		return request_packed_tag(tag, pack, idx, id);

	err = start_pack_privsep_child(pack, packidx);
	if (err)
		return err;

	return request_packed_tag(tag, pack, idx, id);
}

static const struct got_error *
request_tag(struct got_tag_object **tag, struct got_repository *repo,
    int fd)
{
	const struct got_error *err = NULL;
	struct imsgbuf *ibuf;

	ibuf = repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_TAG].ibuf;

	err = got_privsep_send_tag_req(ibuf, fd, NULL, -1);
	if (err)
		return err;

	return got_privsep_recv_tag(tag, ibuf);
}

static const struct got_error *
read_tag_privsep(struct got_tag_object **tag, int obj_fd,
    struct got_repository *repo)
{
	const struct got_error *err;
	int imsg_fds[2];
	pid_t pid;
	struct imsgbuf *ibuf;

	if (repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_TAG].imsg_fd != -1)
		return request_tag(tag, repo, obj_fd);

	ibuf = calloc(1, sizeof(*ibuf));
	if (ibuf == NULL)
		return got_error_from_errno("calloc");

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, imsg_fds) == -1) {
		err = got_error_from_errno("socketpair");
		free(ibuf);
		return err;
	}

	pid = fork();
	if (pid == -1) {
		err = got_error_from_errno("fork");
		free(ibuf);
		return err;
	}
	else if (pid == 0) {
		got_privsep_exec_child(imsg_fds, GOT_PATH_PROG_READ_TAG,
		    repo->path);
		/* not reached */
	}

	if (close(imsg_fds[1]) != 0) {
		err = got_error_from_errno("close");
		free(ibuf);
		return err;
	}
	repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_TAG].imsg_fd =
	    imsg_fds[0];
	repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_TAG].pid = pid;
	imsg_init(ibuf, imsg_fds[0]);
	repo->privsep_children[GOT_REPO_PRIVSEP_CHILD_TAG].ibuf = ibuf;

	return request_tag(tag, repo, obj_fd);
}

static const struct got_error *
open_tag(struct got_tag_object **tag, struct got_repository *repo,
    struct got_object_id *id, int check_cache)
{
	const struct got_error *err = NULL;
	struct got_packidx *packidx = NULL;
	int idx;
	char *path_packfile = NULL;
	struct got_object *obj = NULL;
	int obj_type = GOT_OBJ_TYPE_ANY;

	if (check_cache) {
		*tag = got_repo_get_cached_tag(repo, id);
		if (*tag != NULL) {
			(*tag)->refcnt++;
			return NULL;
		}
	} else
		*tag = NULL;

	err = got_repo_search_packidx(&packidx, &idx, repo, id);
	if (err == NULL) {
		struct got_pack *pack = NULL;

		err = get_packfile_path(&path_packfile, packidx);
		if (err)
			return err;

		pack = got_repo_get_cached_pack(repo, path_packfile);
		if (pack == NULL) {
			err = got_repo_cache_pack(&pack, repo, path_packfile,
			    packidx);
			if (err)
				goto done;
		}

		/* Beware of "lightweight" tags: Check object type first. */
		err = read_packed_object_privsep(&obj, repo, pack, packidx,
		    idx, id);
		if (err)
			goto done;
		obj_type = obj->type;
		got_object_close(obj);
		if (obj_type != GOT_OBJ_TYPE_TAG) {
			err = got_error(GOT_ERR_OBJ_TYPE);
			goto done;
		}
		err = read_packed_tag_privsep(tag, pack, packidx, idx, id);
	} else if (err->code == GOT_ERR_NO_OBJ) {
		int fd;

		err = open_loose_object(&fd, id, repo);
		if (err)
			return err;
		err = read_object_header_privsep(&obj, repo, fd);
		if (err)
			return err;
		obj_type = obj->type;
		got_object_close(obj);
		if (obj_type != GOT_OBJ_TYPE_TAG)
			return got_error(GOT_ERR_OBJ_TYPE);

		err = open_loose_object(&fd, id, repo);
		if (err)
			return err;
		err = read_tag_privsep(tag, fd, repo);
	}

	if (err == NULL) {
		(*tag)->refcnt++;
		err = got_repo_cache_tag(repo, id, *tag);
	}
done:
	free(path_packfile);
	return err;
}

const struct got_error *
got_object_open_as_tag(struct got_tag_object **tag,
    struct got_repository *repo, struct got_object_id *id)
{
	*tag = got_repo_get_cached_tag(repo, id);
	if (*tag != NULL) {
		(*tag)->refcnt++;
		return NULL;
	}

	return open_tag(tag, repo, id, 0);
}

const struct got_error *
got_object_tag_open(struct got_tag_object **tag,
    struct got_repository *repo, struct got_object *obj)
{
	return open_tag(tag, repo, got_object_get_id(obj), 1);
}

const char *
got_object_tag_get_name(struct got_tag_object *tag)
{
	return tag->tag;
}

int
got_object_tag_get_object_type(struct got_tag_object *tag)
{
	return tag->obj_type;
}

struct got_object_id *
got_object_tag_get_object_id(struct got_tag_object *tag)
{
	return &tag->id;
}

time_t
got_object_tag_get_tagger_time(struct got_tag_object *tag)
{
	return tag->tagger_time;
}

time_t
got_object_tag_get_tagger_gmtoff(struct got_tag_object *tag)
{
	return tag->tagger_gmtoff;
}

const char *
got_object_tag_get_tagger(struct got_tag_object *tag)
{
	return tag->tagger;
}

const char *
got_object_tag_get_message(struct got_tag_object *tag)
{
	return tag->tagmsg;
}

static struct got_tree_entry *
find_entry_by_name(struct got_tree_object *tree, const char *name, size_t len)
{
	int i;

	/* Note that tree entries are sorted in strncmp() order. */
	for (i = 0; i < tree->nentries; i++) {
		struct got_tree_entry *te = &tree->entries[i];
		int cmp = strncmp(te->name, name, len);
		if (cmp < 0)
			continue;
		if (cmp > 0)
			break;
		if (te->name[len] == '\0')
			return te;
	}
	return NULL;
}

struct got_tree_entry *
got_object_tree_find_entry(struct got_tree_object *tree, const char *name)
{
	return find_entry_by_name(tree, name, strlen(name));
}

const struct got_error *
got_object_id_by_path(struct got_object_id **id, struct got_repository *repo,
    struct got_object_id *commit_id, const char *path)
{
	const struct got_error *err = NULL;
	struct got_commit_object *commit = NULL;
	struct got_tree_object *tree = NULL;
	struct got_tree_entry *te = NULL;
	const char *seg, *s;
	size_t seglen;

	*id = NULL;

	err = got_object_open_as_commit(&commit, repo, commit_id);
	if (err)
		goto done;

	/* Handle opening of root of commit's tree. */
	if (got_path_is_root_dir(path)) {
		*id = got_object_id_dup(commit->tree_id);
		if (*id == NULL)
			err = got_error_from_errno("got_object_id_dup");
		goto done;
	}

	err = got_object_open_as_tree(&tree, repo, commit->tree_id);
	if (err)
		goto done;

	s = path;
	while (s[0] == '/')
		s++;
	seg = s;
	seglen = 0;
	while (*s) {
		struct got_tree_object *next_tree;

		if (*s != '/') {
			s++;
			seglen++;
			if (*s)
				continue;
		}

		te = find_entry_by_name(tree, seg, seglen);
		if (te == NULL) {
			err = got_error(GOT_ERR_NO_TREE_ENTRY);
			goto done;
		}

		if (*s == '\0')
			break;

		seg = s + 1;
		seglen = 0;
		s++;
		if (*s) {
			err = got_object_open_as_tree(&next_tree, repo,
			    &te->id);
			te = NULL;
			if (err)
				goto done;
			got_object_tree_close(tree);
			tree = next_tree;
		}
	}

	if (te) {
		*id = got_object_id_dup(&te->id);
		if (*id == NULL)
			return got_error_from_errno("got_object_id_dup");
	} else
		err = got_error(GOT_ERR_NO_TREE_ENTRY);
done:
	if (commit)
		got_object_commit_close(commit);
	if (tree)
		got_object_tree_close(tree);
	return err;
}

/*
 * Normalize file mode bits to avoid false positive tree entry differences
 * in case tree entries have unexpected mode bits set.
 */
static mode_t
normalize_mode_for_comparison(mode_t mode)
{
	/*
	 * For directories, the only relevant bit is the IFDIR bit.
	 * This allows us to detect paths changing from a directory
	 * to a file and vice versa.
	 */
	if (S_ISDIR(mode))
		return mode & S_IFDIR;

	/*
	 * For symlinks, the only relevant bit is the IFLNK bit.
	 * This allows us to detect paths changing from a symlinks
	 * to a file or directory and vice versa.
	 */
	if (S_ISLNK(mode))
		return mode & S_IFLNK;

	/* For files, the only change we care about is the executable bit. */
	return mode & S_IXUSR;
}

const struct got_error *
got_object_tree_path_changed(int *changed,
    struct got_tree_object *tree01, struct got_tree_object *tree02,
    const char *path, struct got_repository *repo)
{
	const struct got_error *err = NULL;
	struct got_tree_object *tree1 = NULL, *tree2 = NULL;
	struct got_tree_entry *te1 = NULL, *te2 = NULL;
	const char *seg, *s;
	size_t seglen;

	*changed = 0;

	/* We not do support comparing the root path. */
	if (got_path_is_root_dir(path))
		return got_error_path(path, GOT_ERR_BAD_PATH);

	tree1 = tree01;
	tree2 = tree02;
	s = path;
	while (*s == '/')
		s++;
	seg = s;
	seglen = 0;
	while (*s) {
		struct got_tree_object *next_tree1, *next_tree2;
		mode_t mode1, mode2;

		if (*s != '/') {
			s++;
			seglen++;
			if (*s)
				continue;
		}

		te1 = find_entry_by_name(tree1, seg, seglen);
		if (te1 == NULL) {
			err = got_error(GOT_ERR_NO_OBJ);
			goto done;
		}

		te2 = find_entry_by_name(tree2, seg, seglen);
		if (te2 == NULL) {
			*changed = 1;
			goto done;
		}

		mode1 = normalize_mode_for_comparison(te1->mode);
		mode2 = normalize_mode_for_comparison(te2->mode);
		if (mode1 != mode2) {
			*changed = 1;
			goto done;
		}

		if (got_object_id_cmp(&te1->id, &te2->id) == 0) {
			*changed = 0;
			goto done;
		}

		if (*s == '\0') { /* final path element */
			*changed = 1;
			goto done;
		}

		seg = s + 1;
		s++;
		seglen = 0;
		if (*s) {
			err = got_object_open_as_tree(&next_tree1, repo,
			    &te1->id);
			te1 = NULL;
			if (err)
				goto done;
			if (tree1 != tree01)
				got_object_tree_close(tree1);
			tree1 = next_tree1;

			err = got_object_open_as_tree(&next_tree2, repo,
			    &te2->id);
			te2 = NULL;
			if (err)
				goto done;
			if (tree2 != tree02)
				got_object_tree_close(tree2);
			tree2 = next_tree2;
		}
	}
done:
	if (tree1 && tree1 != tree01)
		got_object_tree_close(tree1);
	if (tree2 && tree2 != tree02)
		got_object_tree_close(tree2);
	return err;
}

const struct got_error *
got_object_tree_entry_dup(struct got_tree_entry **new_te,
    struct got_tree_entry *te)
{
	const struct got_error *err = NULL;

	*new_te = calloc(1, sizeof(**new_te));
	if (*new_te == NULL)
		return got_error_from_errno("calloc");

	(*new_te)->mode = te->mode;
	memcpy((*new_te)->name, te->name, sizeof((*new_te)->name));
	memcpy(&(*new_te)->id, &te->id, sizeof((*new_te)->id));
	return err;
}

int
got_object_tree_entry_is_submodule(struct got_tree_entry *te)
{
	return (te->mode & S_IFMT) == (S_IFDIR | S_IFLNK);
}

int
got_object_tree_entry_is_symlink(struct got_tree_entry *te)
{
	/* S_IFDIR check avoids confusing symlinks with submodules. */
	return ((te->mode & (S_IFDIR | S_IFLNK)) == S_IFLNK);
}

static const struct got_error *
resolve_symlink(char **link_target, const char *path,
    struct got_object_id *commit_id, struct got_repository *repo)
{
	const struct got_error *err = NULL;
	char *name, *parent_path = NULL;
	struct got_object_id *tree_obj_id = NULL;
	struct got_tree_object *tree = NULL;
	struct got_tree_entry *te = NULL;

	*link_target = NULL;
	
	name = basename(path);
	if (name == NULL)
		return got_error_from_errno2("basename", path);

	err = got_path_dirname(&parent_path, path);
	if (err)
		return err;

	err = got_object_id_by_path(&tree_obj_id, repo, commit_id,
	    parent_path);
	if (err) {
		if (err->code == GOT_ERR_NO_TREE_ENTRY) {
			/* Display the complete path in error message. */
			err = got_error_path(path, err->code);
		}
		goto done;
	}

	err = got_object_open_as_tree(&tree, repo, tree_obj_id);
	if (err)
		goto done;

	te = got_object_tree_find_entry(tree, name);
	if (te == NULL) {
		err = got_error_path(path, GOT_ERR_NO_TREE_ENTRY);
		goto done;
	}

	if (got_object_tree_entry_is_symlink(te)) {
		err = got_tree_entry_get_symlink_target(link_target, te, repo);
		if (err)
			goto done;
		if (!got_path_is_absolute(*link_target)) {
			char *abspath;
			if (asprintf(&abspath, "%s/%s", parent_path,
			    *link_target) == -1) {
				err = got_error_from_errno("asprintf");
				goto done;
			}
			free(*link_target);
			*link_target = malloc(PATH_MAX);
			if (*link_target == NULL) {
				err = got_error_from_errno("malloc");
				goto done;
			}
			err = got_canonpath(abspath, *link_target, PATH_MAX);
			free(abspath);
			if (err)
				goto done;
		}
	}
done:
	free(tree_obj_id);
	if (tree)
		got_object_tree_close(tree);
	if (err) {
		free(*link_target);
		*link_target = NULL;
	}
	return err;
}

const struct got_error *
got_object_resolve_symlinks(char **link_target, const char *path,
    struct got_object_id *commit_id, struct got_repository *repo)
{
	const struct got_error *err = NULL;
	char *next_target = NULL;
	int max_recursion = 40; /* matches Git */

	*link_target = NULL;

	do {
		err = resolve_symlink(&next_target,
		    *link_target ? *link_target : path, commit_id, repo);
		if (err)
			break;
		if (next_target) {
			free(*link_target);
			if (--max_recursion == 0) {
				err = got_error_path(path, GOT_ERR_RECURSION);
				*link_target = NULL;
				break;
			}
			*link_target = next_target;
		}
	} while (next_target);

	return err;
}

const struct got_error *
got_traverse_packed_commits(struct got_object_id_queue *traversed_commits,
    struct got_object_id *commit_id, const char *path,
    struct got_repository *repo)
{
	const struct got_error *err = NULL;
	struct got_pack *pack = NULL;
	struct got_packidx *packidx = NULL;
	char *path_packfile = NULL;
	struct got_commit_object *changed_commit = NULL;
	struct got_object_id *changed_commit_id = NULL;
	int idx;

	err = got_repo_search_packidx(&packidx, &idx, repo, commit_id);
	if (err) {
		if (err->code != GOT_ERR_NO_OBJ)
			return err;
		return NULL;
	}

	err = get_packfile_path(&path_packfile, packidx);
	if (err)
		return err;

	pack = got_repo_get_cached_pack(repo, path_packfile);
	if (pack == NULL) {
		err = got_repo_cache_pack(&pack, repo, path_packfile, packidx);
		if (err)
			goto done;
	}

	if (pack->privsep_child == NULL) {
		err = start_pack_privsep_child(pack, packidx);
		if (err)
			goto done;
	}

	err = got_privsep_send_commit_traversal_request(
	    pack->privsep_child->ibuf, commit_id, idx, path);
	if (err)
		goto done;

	err = got_privsep_recv_traversed_commits(&changed_commit,
	    &changed_commit_id, traversed_commits, pack->privsep_child->ibuf);
	if (err)
		goto done;

	if (changed_commit) {
		/*
		 * Cache the commit in which the path was changed.
		 * This commit might be opened again soon.
		 */
		changed_commit->refcnt++;
		err = got_repo_cache_commit(repo, changed_commit_id,
		    changed_commit);
		got_object_commit_close(changed_commit);
	}
done:
	free(path_packfile);
	free(changed_commit_id);
	return err;
}
