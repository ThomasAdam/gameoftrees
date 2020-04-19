/*
 * Copyright (c) 2018, 2019, 2020 Stefan Sperling <stsp@openbsd.org>
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
#include <sys/queue.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/syslimits.h>
#include <sys/mman.h>

#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <imsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sha1.h>
#include <zlib.h>

#include "got_error.h"
#include "got_object.h"
#include "got_path.h"

#include "got_lib_delta.h"
#include "got_lib_delta_cache.h"
#include "got_lib_object.h"
#include "got_lib_object_cache.h"
#include "got_lib_object_parse.h"
#include "got_lib_privsep.h"
#include "got_lib_pack.h"

static volatile sig_atomic_t sigint_received;

static void
catch_sigint(int signo)
{
	sigint_received = 1;
}

static const struct got_error *
open_object(struct got_object **obj, struct got_pack *pack,
    struct got_packidx *packidx, int idx, struct got_object_id *id,
    struct got_object_cache *objcache)
{
	const struct got_error *err;

	err = got_packfile_open_object(obj, pack, packidx, idx, id);
	if (err)
		return err;
	(*obj)->refcnt++;

	err = got_object_cache_add(objcache, id, *obj);
	if (err) {
		if (err->code == GOT_ERR_OBJ_EXISTS ||
		    err->code == GOT_ERR_OBJ_TOO_LARGE)
			err = NULL;
		return err;
	}
	(*obj)->refcnt++;
	return NULL;
}

static const struct got_error *
object_request(struct imsg *imsg, struct imsgbuf *ibuf, struct got_pack *pack,
    struct got_packidx *packidx, struct got_object_cache *objcache)
{
	const struct got_error *err = NULL;
	struct got_imsg_packed_object iobj;
	struct got_object *obj;
	struct got_object_id id;
	size_t datalen;

	datalen = imsg->hdr.len - IMSG_HEADER_SIZE;
	if (datalen != sizeof(iobj))
		return got_error(GOT_ERR_PRIVSEP_LEN);
	memcpy(&iobj, imsg->data, sizeof(iobj));
	memcpy(id.sha1, iobj.id, SHA1_DIGEST_LENGTH);

	obj = got_object_cache_get(objcache, &id);
	if (obj) {
		obj->refcnt++;
	} else {
		err = open_object(&obj, pack, packidx, iobj.idx, &id,
		    objcache);
		if (err)
			goto done;
	}

	err = got_privsep_send_obj(ibuf, obj);
done:
	got_object_close(obj);
	return err;
}

const struct got_error *
open_commit(struct got_commit_object **commit, struct got_pack *pack,
    struct got_packidx *packidx, int obj_idx, struct got_object_id *id,
    struct got_object_cache *objcache)
{
	const struct got_error *err = NULL;
	struct got_object *obj = NULL;
	uint8_t *buf = NULL;
	size_t len;

	*commit = NULL;

	obj = got_object_cache_get(objcache, id);
	if (obj) {
		obj->refcnt++;
	} else {
		err = open_object(&obj, pack, packidx, obj_idx, id,
		    objcache);
		if (err)
			return err;
	}

	err = got_packfile_extract_object_to_mem(&buf, &len, obj, pack);
	if (err)
		goto done;

	obj->size = len;

	err = got_object_parse_commit(commit, buf, len);
done:
	got_object_close(obj);
	free(buf);
	return err;
}

static const struct got_error *
commit_request(struct imsg *imsg, struct imsgbuf *ibuf, struct got_pack *pack,
    struct got_packidx *packidx, struct got_object_cache *objcache)
{
	const struct got_error *err = NULL;
	struct got_imsg_packed_object iobj;
	struct got_commit_object *commit = NULL;
	struct got_object_id id;
	size_t datalen;

	datalen = imsg->hdr.len - IMSG_HEADER_SIZE;
	if (datalen != sizeof(iobj))
		return got_error(GOT_ERR_PRIVSEP_LEN);
	memcpy(&iobj, imsg->data, sizeof(iobj));
	memcpy(id.sha1, iobj.id, SHA1_DIGEST_LENGTH);

	err = open_commit(&commit, pack, packidx, iobj.idx, &id, objcache);
	if (err)
		goto done;

	err = got_privsep_send_commit(ibuf, commit);
done:
	if (commit)
		got_object_commit_close(commit);
	if (err) {
		if (err->code == GOT_ERR_PRIVSEP_PIPE)
			err = NULL;
		else
			got_privsep_send_error(ibuf, err);
	}

	return err;
}

const struct got_error *
open_tree(uint8_t **buf, struct got_pathlist_head *entries, int *nentries,
    struct got_pack *pack, struct got_packidx *packidx, int obj_idx,
    struct got_object_id *id, struct got_object_cache *objcache)
{
	const struct got_error *err = NULL;
	struct got_object *obj = NULL;
	size_t len;

	*buf = NULL;
	*nentries = 0;

	obj = got_object_cache_get(objcache, id);
	if (obj) {
		obj->refcnt++;
	} else {
		err = open_object(&obj, pack, packidx, obj_idx, id,
		    objcache);
		if (err)
			return err;
	}

	err = got_packfile_extract_object_to_mem(buf, &len, obj, pack);
	if (err)
		goto done;

	obj->size = len;

	err = got_object_parse_tree(entries, nentries, *buf, len);
done:
	got_object_close(obj);
	if (err) {
		free(*buf);
		*buf = NULL;
	}
	return err;
}

static const struct got_error *
tree_request(struct imsg *imsg, struct imsgbuf *ibuf, struct got_pack *pack,
    struct got_packidx *packidx, struct got_object_cache *objcache)
{
	const struct got_error *err = NULL;
	struct got_imsg_packed_object iobj;
	struct got_pathlist_head entries;
	int nentries = 0;
	uint8_t *buf = NULL;
	struct got_object_id id;
	size_t datalen;

	TAILQ_INIT(&entries);

	datalen = imsg->hdr.len - IMSG_HEADER_SIZE;
	if (datalen != sizeof(iobj))
		return got_error(GOT_ERR_PRIVSEP_LEN);
	memcpy(&iobj, imsg->data, sizeof(iobj));
	memcpy(id.sha1, iobj.id, SHA1_DIGEST_LENGTH);

	err = open_tree(&buf, &entries, &nentries, pack, packidx, iobj.idx,
	     &id, objcache);
	if (err)
		return err;

	err = got_privsep_send_tree(ibuf, &entries, nentries);
	got_object_parsed_tree_entries_free(&entries);
	free(buf);
	if (err) {
		if (err->code == GOT_ERR_PRIVSEP_PIPE)
			err = NULL;
		else
			got_privsep_send_error(ibuf, err);
	}

	return err;
}

static const struct got_error *
receive_file(FILE **f, struct imsgbuf *ibuf, int imsg_code)
{
	const struct got_error *err;
	struct imsg imsg;
	size_t datalen;

	err = got_privsep_recv_imsg(&imsg, ibuf, 0);
	if (err)
		return err;

	if (imsg.hdr.type != imsg_code) {
		err = got_error(GOT_ERR_PRIVSEP_MSG);
		goto done;
	}

	datalen = imsg.hdr.len - IMSG_HEADER_SIZE;
	if (datalen != 0) {
		err = got_error(GOT_ERR_PRIVSEP_LEN);
		goto done;
	}
	if (imsg.fd == -1) {
		err = got_error(GOT_ERR_PRIVSEP_NO_FD);
		goto done;
	}

	*f = fdopen(imsg.fd, "w+");
	if (*f == NULL) {
		err = got_error_from_errno("fdopen");
		close(imsg.fd);
		goto done;
	}
done:
	imsg_free(&imsg);
	return err;
}

static const struct got_error *
blob_request(struct imsg *imsg, struct imsgbuf *ibuf, struct got_pack *pack,
    struct got_packidx *packidx, struct got_object_cache *objcache)
{
	const struct got_error *err = NULL;
	struct got_imsg_packed_object iobj;
	struct got_object *obj = NULL;
	FILE *outfile = NULL, *basefile = NULL, *accumfile = NULL;
	struct got_object_id id;
	size_t datalen;
	uint64_t blob_size;
	uint8_t *buf = NULL;

	datalen = imsg->hdr.len - IMSG_HEADER_SIZE;
	if (datalen != sizeof(iobj))
		return got_error(GOT_ERR_PRIVSEP_LEN);
	memcpy(&iobj, imsg->data, sizeof(iobj));
	memcpy(id.sha1, iobj.id, SHA1_DIGEST_LENGTH);

	obj = got_object_cache_get(objcache, &id);
	if (obj) {
		obj->refcnt++;
	} else {
		err = open_object(&obj, pack, packidx, iobj.idx, &id,
		    objcache);
		if (err)
			return err;
	}

	err = receive_file(&outfile, ibuf, GOT_IMSG_BLOB_OUTFD);
	if (err)
		goto done;
	err = receive_file(&basefile, ibuf, GOT_IMSG_TMPFD);
	if (err)
		goto done;
	err = receive_file(&accumfile, ibuf, GOT_IMSG_TMPFD);
	if (err)
		goto done;

	if (obj->flags & GOT_OBJ_FLAG_DELTIFIED) {
		err = got_pack_get_max_delta_object_size(&blob_size, obj, pack);
		if (err)
			goto done;
	} else
		blob_size = obj->size;

	if (blob_size <= GOT_PRIVSEP_INLINE_BLOB_DATA_MAX)
		err = got_packfile_extract_object_to_mem(&buf, &obj->size,
		    obj, pack);
	else
		err = got_packfile_extract_object(pack, obj, outfile, basefile,
		    accumfile);
	if (err)
		goto done;

	err = got_privsep_send_blob(ibuf, obj->size, obj->hdrlen, buf);
done:
	free(buf);
	if (outfile && fclose(outfile) != 0 && err == NULL)
		err = got_error_from_errno("fclose");
	if (basefile && fclose(basefile) != 0 && err == NULL)
		err = got_error_from_errno("fclose");
	if (accumfile && fclose(accumfile) != 0 && err == NULL)
		err = got_error_from_errno("fclose");
	got_object_close(obj);
	if (err && err->code != GOT_ERR_PRIVSEP_PIPE)
		got_privsep_send_error(ibuf, err);

	return err;
}

static const struct got_error *
tag_request(struct imsg *imsg, struct imsgbuf *ibuf, struct got_pack *pack,
    struct got_packidx *packidx, struct got_object_cache *objcache)
{
	const struct got_error *err = NULL;
	struct got_imsg_packed_object iobj;
	struct got_object *obj = NULL;
	struct got_tag_object *tag = NULL;
	uint8_t *buf = NULL;
	size_t len;
	struct got_object_id id;
	size_t datalen;

	datalen = imsg->hdr.len - IMSG_HEADER_SIZE;
	if (datalen != sizeof(iobj))
		return got_error(GOT_ERR_PRIVSEP_LEN);
	memcpy(&iobj, imsg->data, sizeof(iobj));
	memcpy(id.sha1, iobj.id, SHA1_DIGEST_LENGTH);

	obj = got_object_cache_get(objcache, &id);
	if (obj) {
		obj->refcnt++;
	} else {
		err = open_object(&obj, pack, packidx, iobj.idx, &id,
		    objcache);
		if (err)
			return err;
	}

	err = got_packfile_extract_object_to_mem(&buf, &len, obj, pack);
	if (err)
		goto done;

	obj->size = len;
	err = got_object_parse_tag(&tag, buf, len);
	if (err)
		goto done;

	err = got_privsep_send_tag(ibuf, tag);
done:
	free(buf);
	got_object_close(obj);
	if (tag)
		got_object_tag_close(tag);
	if (err) {
		if (err->code == GOT_ERR_PRIVSEP_PIPE)
			err = NULL;
		else
			got_privsep_send_error(ibuf, err);
	}

	return err;
}

static struct got_parsed_tree_entry *
find_entry_by_name(struct got_pathlist_head *entries, int nentries,
    const char *name, size_t len)
{
	struct got_pathlist_entry *pe;

	/* Note that tree entries are sorted in strncmp() order. */
	TAILQ_FOREACH(pe, entries, entry) {
		int cmp = strncmp(pe->path, name, len);
		if (cmp < 0)
			continue;
		if (cmp > 0)
			break;
		if (pe->path[len] == '\0')
			return (struct got_parsed_tree_entry *)pe->data;
	}
	return NULL;
}

const struct got_error *
tree_path_changed(int *changed, uint8_t **buf1, uint8_t **buf2,
    struct got_pathlist_head *entries1, int *nentries1,
    struct got_pathlist_head *entries2, int *nentries2,
    const char *path, struct got_pack *pack, struct got_packidx *packidx,
    struct imsgbuf *ibuf, struct got_object_cache *objcache)
{
	const struct got_error *err = NULL;
	struct got_parsed_tree_entry *pte1 = NULL, *pte2 = NULL;
	const char *seg, *s;
	size_t seglen;

	*changed = 0;

	/* We not do support comparing the root path. */
	if (got_path_is_root_dir(path))
		return got_error_path(path, GOT_ERR_BAD_PATH);

	s = path;
	while (*s == '/')
		s++;
	seg = s;
	seglen = 0;
	while (*s) {
		if (*s != '/') {
			s++;
			seglen++;
			if (*s)
				continue;
		}

		pte1 = find_entry_by_name(entries1, *nentries1, seg, seglen);
		if (pte1 == NULL) {
			err = got_error(GOT_ERR_NO_OBJ);
			break;
		}

		pte2 = find_entry_by_name(entries2, *nentries2, seg, seglen);
		if (pte2 == NULL) {
			*changed = 1;
			break;
		}

		if (pte1->mode != pte2->mode) {
			*changed = 1;
			break;
		}

		if (memcmp(pte1->id, pte2->id, SHA1_DIGEST_LENGTH) == 0) {
			*changed = 0;
			break;
		}

		if (*s == '\0') { /* final path element */
			*changed = 1;
			break;
		}

		seg = s + 1;
		s++;
		seglen = 0;
		if (*s) {
			struct got_object_id id1, id2;
			int idx;

			memcpy(id1.sha1, pte1->id, SHA1_DIGEST_LENGTH);
			idx = got_packidx_get_object_idx_sha1(packidx,
			    pte1->id);
			if (idx == -1) {
				err = got_error_no_obj(&id1);
				break;
			}
			got_object_parsed_tree_entries_free(entries1);
			*nentries1 = 0;
			free(*buf1);
			*buf1 = NULL;
			err = open_tree(buf1, entries1, nentries1, pack,
			    packidx, idx, &id1, objcache);
			pte1 = NULL;
			if (err)
				break;

			memcpy(id2.sha1, pte2->id, SHA1_DIGEST_LENGTH);
			idx = got_packidx_get_object_idx_sha1(packidx,
			    pte2->id);
			if (idx == -1) {
				err = got_error_no_obj(&id2);
				break;
			}
			got_object_parsed_tree_entries_free(entries2);
			*nentries2 = 0;
			free(*buf2);
			*buf2 = NULL;
			err = open_tree(buf2, entries2, nentries2, pack,
			    packidx, idx, &id2, objcache);
			pte2 = NULL;
			if (err)
				break;
		}
	}

	return err;
}

static const struct got_error *
send_traversed_commits(struct got_object_id *commit_ids, size_t ncommits,
    struct imsgbuf *ibuf)
{
	const struct got_error *err;
	struct ibuf *wbuf;
	int i;

	wbuf = imsg_create(ibuf, GOT_IMSG_TRAVERSED_COMMITS, 0, 0,
	    sizeof(struct got_imsg_traversed_commits) +
	    ncommits * SHA1_DIGEST_LENGTH);
	if (wbuf == NULL)
		return got_error_from_errno("imsg_create TRAVERSED_COMMITS");

	if (imsg_add(wbuf, &ncommits, sizeof(ncommits)) == -1) {
		err = got_error_from_errno("imsg_add TRAVERSED_COMMITS");
		ibuf_free(wbuf);
		return err;
	}
	for (i = 0; i < ncommits; i++) {
		struct got_object_id *id = &commit_ids[i];
		if (imsg_add(wbuf, id->sha1, SHA1_DIGEST_LENGTH) == -1) {
			err = got_error_from_errno(
			    "imsg_add TRAVERSED_COMMITS");
			ibuf_free(wbuf);
			return err;
		}
	}

	wbuf->fd = -1;
	imsg_close(ibuf, wbuf);

	return got_privsep_flush_imsg(ibuf);
}

static const struct got_error *
send_commit_traversal_done(struct imsgbuf *ibuf)
{
	if (imsg_compose(ibuf, GOT_IMSG_COMMIT_TRAVERSAL_DONE, 0, 0, -1,
	    NULL, 0) == -1)
		return got_error_from_errno("imsg_compose TRAVERSAL_DONE");

	return got_privsep_flush_imsg(ibuf);
}


static const struct got_error *
commit_traversal_request(struct imsg *imsg, struct imsgbuf *ibuf,
    struct got_pack *pack, struct got_packidx *packidx,
    struct got_object_cache *objcache)
{
	const struct got_error *err = NULL;
	struct got_imsg_packed_object iobj;
	struct got_object_qid *pid;
	struct got_commit_object *commit = NULL, *pcommit = NULL;
	struct got_pathlist_head entries, pentries;
	int nentries = 0, pnentries = 0;
	struct got_object_id id;
	size_t datalen, path_len;
	char *path = NULL;
	const int min_alloc = 64;
	int changed = 0, ncommits = 0, nallocated = 0;
	struct got_object_id *commit_ids = NULL;

	TAILQ_INIT(&entries);
	TAILQ_INIT(&pentries);

	datalen = imsg->hdr.len - IMSG_HEADER_SIZE;
	if (datalen < sizeof(iobj))
		return got_error(GOT_ERR_PRIVSEP_LEN);
	memcpy(&iobj, imsg->data, sizeof(iobj));
	memcpy(id.sha1, iobj.id, SHA1_DIGEST_LENGTH);

	path_len = datalen - sizeof(iobj) - 1;
	if (path_len < 0)
		return got_error(GOT_ERR_PRIVSEP_LEN);
	if (path_len > 0) {
		path = imsg->data + sizeof(iobj);
		if (path[path_len] != '\0')
			return got_error(GOT_ERR_PRIVSEP_LEN);
	}

	nallocated = min_alloc;
	commit_ids = reallocarray(NULL, nallocated, sizeof(*commit_ids));
	if (commit_ids == NULL)
		return got_error_from_errno("reallocarray");

	do {
		const size_t max_datalen = MAX_IMSGSIZE - IMSG_HEADER_SIZE;
		int idx;

		if (sigint_received) {
			err = got_error(GOT_ERR_CANCELLED);
			goto done;
		}

		if (commit == NULL) {
			idx = got_packidx_get_object_idx(packidx, &id);
			if (idx == -1)
				break;
			err = open_commit(&commit, pack, packidx,
			    idx, &id, objcache);
			if (err) {
				if (err->code != GOT_ERR_NO_OBJ)
					goto done;
				err = NULL;
				break;
			}
		}

		if (sizeof(struct got_imsg_traversed_commits) +
		    ncommits * SHA1_DIGEST_LENGTH >= max_datalen) {
			err = send_traversed_commits(commit_ids, ncommits,
			    ibuf);
			if (err)
				goto done;
			ncommits = 0;
		}
		ncommits++;
		if (ncommits > nallocated) {
			struct got_object_id *new;
			nallocated += min_alloc;
			new = reallocarray(commit_ids, nallocated,
			    sizeof(*commit_ids));
			if (new == NULL) {
				err = got_error_from_errno("reallocarray");
				goto done;
			}
			commit_ids = new;
		}
		memcpy(commit_ids[ncommits - 1].sha1, id.sha1,
		    SHA1_DIGEST_LENGTH);

		pid = SIMPLEQ_FIRST(&commit->parent_ids);
		if (pid == NULL)
			break;

		idx = got_packidx_get_object_idx(packidx, pid->id);
		if (idx == -1)
			break;

		err = open_commit(&pcommit, pack, packidx, idx, pid->id,
		    objcache);
		if (err) {
			if (err->code != GOT_ERR_NO_OBJ)
				goto done;
			err = NULL;
			break;
		}

		if (path[0] == '/' && path[1] == '\0') {
			if (got_object_id_cmp(pcommit->tree_id,
			    commit->tree_id) != 0) {
				changed = 1;
				break;
			}
		} else {
			int pidx;
			uint8_t *buf = NULL, *pbuf = NULL;

			idx = got_packidx_get_object_idx(packidx,
			    commit->tree_id);
			if (idx == -1)
				break;
			pidx = got_packidx_get_object_idx(packidx,
			    pcommit->tree_id);
			if (pidx == -1)
				break;

			err = open_tree(&buf, &entries, &nentries, pack,
			    packidx, idx, commit->tree_id, objcache);
			if (err)
				goto done;
			err = open_tree(&pbuf, &pentries, &pnentries, pack,
			    packidx, pidx, pcommit->tree_id, objcache);
			if (err) {
				free(buf);
				goto done;
			}

			err = tree_path_changed(&changed, &buf, &pbuf,
			    &entries, &nentries, &pentries, &pnentries, path,
			    pack, packidx, ibuf, objcache);

			got_object_parsed_tree_entries_free(&entries);
			nentries = 0;
			free(buf);
			got_object_parsed_tree_entries_free(&pentries);
			pnentries = 0;
			free(pbuf);
			if (err) {
				if (err->code != GOT_ERR_NO_OBJ)
					goto done;
				err = NULL;
				break;
			}
		}

		if (!changed) {
			memcpy(id.sha1, pid->id->sha1, SHA1_DIGEST_LENGTH);
			got_object_commit_close(commit);
			commit = pcommit;
			pcommit = NULL;
		}
	} while (!changed);

	if (ncommits > 0) {
		err = send_traversed_commits(commit_ids, ncommits, ibuf);
		if (err)
			goto done;

		if (changed) {
			err = got_privsep_send_commit(ibuf, commit);
			if (err)
				goto done;
		}
	}
	err = send_commit_traversal_done(ibuf);
done:
	free(commit_ids);
	if (commit)
		got_object_commit_close(commit);
	if (pcommit)
		got_object_commit_close(pcommit);
	if (nentries != 0)
		got_object_parsed_tree_entries_free(&entries);
	if (pnentries != 0)
		got_object_parsed_tree_entries_free(&pentries);
	if (err) {
		if (err->code == GOT_ERR_PRIVSEP_PIPE)
			err = NULL;
		else
			got_privsep_send_error(ibuf, err);
	}

	return err;
}

static const struct got_error *
receive_packidx(struct got_packidx **packidx, struct imsgbuf *ibuf)
{
	const struct got_error *err = NULL;
	struct imsg imsg;
	struct got_imsg_packidx ipackidx;
	size_t datalen;
	struct got_packidx *p;

	*packidx = NULL;

	err = got_privsep_recv_imsg(&imsg, ibuf, 0);
	if (err)
		return err;

	p = calloc(1, sizeof(*p));
	if (p == NULL) {
		err = got_error_from_errno("calloc");
		goto done;
	}

	if (imsg.hdr.type != GOT_IMSG_PACKIDX) {
		err = got_error(GOT_ERR_PRIVSEP_MSG);
		goto done;
	}

	if (imsg.fd == -1) {
		err = got_error(GOT_ERR_PRIVSEP_NO_FD);
		goto done;
	}

	datalen = imsg.hdr.len - IMSG_HEADER_SIZE;
	if (datalen != sizeof(ipackidx)) {
		err = got_error(GOT_ERR_PRIVSEP_LEN);
		goto done;
	}
	memcpy(&ipackidx, imsg.data, sizeof(ipackidx));

	p->len = ipackidx.len;
	p->fd = dup(imsg.fd);
	if (p->fd == -1) {
		err = got_error_from_errno("dup");
		goto done;
	}
	if (lseek(p->fd, 0, SEEK_SET) == -1) {
		err = got_error_from_errno("lseek");
		goto done;
	}

#ifndef GOT_PACK_NO_MMAP
	p->map = mmap(NULL, p->len, PROT_READ, MAP_PRIVATE, p->fd, 0);
	if (p->map == MAP_FAILED)
		p->map = NULL; /* fall back to read(2) */
#endif
	err = got_packidx_init_hdr(p, 1);
done:
	if (err) {
		if (imsg.fd != -1)
			close(imsg.fd);
		got_packidx_close(p);
	} else
		*packidx = p;
	imsg_free(&imsg);
	return err;
}

static const struct got_error *
receive_pack(struct got_pack **packp, struct imsgbuf *ibuf)
{
	const struct got_error *err = NULL;
	struct imsg imsg;
	struct got_imsg_pack ipack;
	size_t datalen;
	struct got_pack *pack;

	*packp = NULL;

	err = got_privsep_recv_imsg(&imsg, ibuf, 0);
	if (err)
		return err;

	pack = calloc(1, sizeof(*pack));
	if (pack == NULL) {
		err = got_error_from_errno("calloc");
		goto done;
	}

	if (imsg.hdr.type != GOT_IMSG_PACK) {
		err = got_error(GOT_ERR_PRIVSEP_MSG);
		goto done;
	}

	if (imsg.fd == -1) {
		err = got_error(GOT_ERR_PRIVSEP_NO_FD);
		goto done;
	}

	datalen = imsg.hdr.len - IMSG_HEADER_SIZE;
	if (datalen != sizeof(ipack)) {
		err = got_error(GOT_ERR_PRIVSEP_LEN);
		goto done;
	}
	memcpy(&ipack, imsg.data, sizeof(ipack));

	pack->filesize = ipack.filesize;
	pack->fd = dup(imsg.fd);
	if (pack->fd == -1) {
		err = got_error_from_errno("dup");
		goto done;
	}
	if (lseek(pack->fd, 0, SEEK_SET) == -1) {
		err = got_error_from_errno("lseek");
		goto done;
	}
	pack->path_packfile = strdup(ipack.path_packfile);
	if (pack->path_packfile == NULL) {
		err = got_error_from_errno("strdup");
		goto done;
	}

	pack->delta_cache = got_delta_cache_alloc(100,
	    GOT_DELTA_RESULT_SIZE_CACHED_MAX);
	if (pack->delta_cache == NULL) {
		err = got_error_from_errno("got_delta_cache_alloc");
		goto done;
	}

#ifndef GOT_PACK_NO_MMAP
	pack->map = mmap(NULL, pack->filesize, PROT_READ, MAP_PRIVATE,
	    pack->fd, 0);
	if (pack->map == MAP_FAILED)
		pack->map = NULL; /* fall back to read(2) */
#endif
done:
	if (err) {
		if (imsg.fd != -1)
			close(imsg.fd);
		free(pack);
	} else
		*packp = pack;
	imsg_free(&imsg);
	return err;
}

int
main(int argc, char *argv[])
{
	const struct got_error *err = NULL;
	struct imsgbuf ibuf;
	struct imsg imsg;
	struct got_packidx *packidx = NULL;
	struct got_pack *pack = NULL;
	struct got_object_cache objcache;

	//static int attached;
	//while (!attached) sleep(1);

	signal(SIGINT, catch_sigint);

	imsg_init(&ibuf, GOT_IMSG_FD_CHILD);

	err = got_object_cache_init(&objcache, GOT_OBJECT_CACHE_TYPE_OBJ);
	if (err) {
		err = got_error_from_errno("got_object_cache_init");
		got_privsep_send_error(&ibuf, err);
		return 1;
	}

#ifndef PROFILE
	/* revoke access to most system calls */
	if (pledge("stdio recvfd", NULL) == -1) {
		err = got_error_from_errno("pledge");
		got_privsep_send_error(&ibuf, err);
		return 1;
	}
#endif

	err = receive_packidx(&packidx, &ibuf);
	if (err) {
		got_privsep_send_error(&ibuf, err);
		return 1;
	}

	err = receive_pack(&pack, &ibuf);
	if (err) {
		got_privsep_send_error(&ibuf, err);
		return 1;
	}

	for (;;) {
		imsg.fd = -1;

		if (sigint_received) {
			err = got_error(GOT_ERR_CANCELLED);
			break;
		}

		err = got_privsep_recv_imsg(&imsg, &ibuf, 0);
		if (err) {
			if (err->code == GOT_ERR_PRIVSEP_PIPE)
				err = NULL;
			break;
		}

		if (imsg.hdr.type == GOT_IMSG_STOP)
			break;

		switch (imsg.hdr.type) {
		case GOT_IMSG_PACKED_OBJECT_REQUEST:
			err = object_request(&imsg, &ibuf, pack, packidx,
			    &objcache);
			break;
		case GOT_IMSG_COMMIT_REQUEST:
			err = commit_request(&imsg, &ibuf, pack, packidx,
			    &objcache);
			break;
		case GOT_IMSG_TREE_REQUEST:
			err = tree_request(&imsg, &ibuf, pack, packidx,
			   &objcache);
			break;
		case GOT_IMSG_BLOB_REQUEST:
			err = blob_request(&imsg, &ibuf, pack, packidx,
			   &objcache);
			break;
		case GOT_IMSG_TAG_REQUEST:
			err = tag_request(&imsg, &ibuf, pack, packidx,
			   &objcache);
			break;
		case GOT_IMSG_COMMIT_TRAVERSAL_REQUEST:
			err = commit_traversal_request(&imsg, &ibuf, pack,
			    packidx, &objcache);
			break;
		default:
			err = got_error(GOT_ERR_PRIVSEP_MSG);
			break;
		}

		if (imsg.fd != -1 && close(imsg.fd) != 0 && err == NULL)
			err = got_error_from_errno("close");
		imsg_free(&imsg);
		if (err)
			break;
	}

	if (packidx)
		got_packidx_close(packidx);
	if (pack)
		got_pack_close(pack);
	got_object_cache_close(&objcache);
	imsg_clear(&ibuf);
	if (err) {
		if (!sigint_received && err->code != GOT_ERR_PRIVSEP_PIPE) {
			fprintf(stderr, "%s: %s\n", getprogname(), err->msg);
			got_privsep_send_error(&ibuf, err);
		}
	}
	if (close(GOT_IMSG_FD_CHILD) != 0 && err == NULL)
		err = got_error_from_errno("close");
	return err ? 1 : 0;
}
