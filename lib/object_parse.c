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
#include <sys/syslimits.h>
#include <sys/wait.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sha1.h>
#include <zlib.h>
#include <ctype.h>
#include <limits.h>
#include <imsg.h>
#include <time.h>
#include <unistd.h>

#include "got_error.h"
#include "got_object.h"
#include "got_repository.h"
#include "got_opentemp.h"
#include "got_path.h"

#include "got_lib_sha1.h"
#include "got_lib_delta.h"
#include "got_lib_inflate.h"
#include "got_lib_object.h"
#include "got_lib_object_cache.h"
#include "got_lib_pack.h"
#include "got_lib_privsep.h"
#include "got_lib_repository.h"

#ifndef nitems
#define nitems(_a) (sizeof(_a) / sizeof((_a)[0]))
#endif

int
got_object_id_cmp(const struct got_object_id *id1,
    const struct got_object_id *id2)
{
	return memcmp(id1->sha1, id2->sha1, SHA1_DIGEST_LENGTH);
}

const struct got_error *
got_object_qid_alloc_partial(struct got_object_qid **qid)
{
	const struct got_error *err = NULL;

	*qid = malloc(sizeof(**qid));
	if (*qid == NULL)
		return got_error_from_errno("malloc");

	(*qid)->id = malloc(sizeof(*((*qid)->id)));
	if ((*qid)->id == NULL) {
		err = got_error_from_errno("malloc");
		got_object_qid_free(*qid);
		*qid = NULL;
		return err;
	}

	return NULL;
}

const struct got_error *
got_object_id_str(char **outbuf, struct got_object_id *id)
{
	static const size_t len = SHA1_DIGEST_STRING_LENGTH;

	*outbuf = malloc(len);
	if (*outbuf == NULL)
		return got_error_from_errno("malloc");

	if (got_sha1_digest_to_str(id->sha1, *outbuf, len) == NULL) {
		free(*outbuf);
		*outbuf = NULL;
		return got_error(GOT_ERR_BAD_OBJ_ID_STR);
	}

	return NULL;
}

void
got_object_close(struct got_object *obj)
{
	if (obj->refcnt > 0) {
		obj->refcnt--;
		if (obj->refcnt > 0)
			return;
	}

	if (obj->flags & GOT_OBJ_FLAG_DELTIFIED) {
		struct got_delta *delta;
		while (!SIMPLEQ_EMPTY(&obj->deltas.entries)) {
			delta = SIMPLEQ_FIRST(&obj->deltas.entries);
			SIMPLEQ_REMOVE_HEAD(&obj->deltas.entries, entry);
			free(delta->delta_buf);
			free(delta);
		}
	}
	if (obj->flags & GOT_OBJ_FLAG_PACKED)
		free(obj->path_packfile);
	free(obj);
}

void
got_object_qid_free(struct got_object_qid *qid)
{
	free(qid->id);
	free(qid);
}

void
got_object_id_queue_free(struct got_object_id_queue *ids)
{
	struct got_object_qid *qid;

	while (!SIMPLEQ_EMPTY(ids)) {
		qid = SIMPLEQ_FIRST(ids);
		SIMPLEQ_REMOVE_HEAD(ids, entry);
		got_object_qid_free(qid);
	}
}

const struct got_error *
got_object_parse_header(struct got_object **obj, char *buf, size_t len)
{
	const char *obj_labels[] = {
		GOT_OBJ_LABEL_COMMIT,
		GOT_OBJ_LABEL_TREE,
		GOT_OBJ_LABEL_BLOB,
		GOT_OBJ_LABEL_TAG,
	};
	const int obj_types[] = {
		GOT_OBJ_TYPE_COMMIT,
		GOT_OBJ_TYPE_TREE,
		GOT_OBJ_TYPE_BLOB,
		GOT_OBJ_TYPE_TAG,
	};
	int type = 0;
	size_t size = 0, hdrlen = 0;
	int i;

	*obj = NULL;

	hdrlen = strnlen(buf, len) + 1 /* '\0' */;
	if (hdrlen > len)
		return got_error(GOT_ERR_BAD_OBJ_HDR);

	for (i = 0; i < nitems(obj_labels); i++) {
		const char *label = obj_labels[i];
		size_t label_len = strlen(label);
		const char *errstr;

		if (strncmp(buf, label, label_len) != 0)
			continue;

		type = obj_types[i];
		if (len <= label_len)
			return got_error(GOT_ERR_BAD_OBJ_HDR);
		size = strtonum(buf + label_len, 0, LONG_MAX, &errstr);
		if (errstr != NULL)
			return got_error(GOT_ERR_BAD_OBJ_HDR);
		break;
	}

	if (type == 0)
		return got_error(GOT_ERR_BAD_OBJ_HDR);

	*obj = calloc(1, sizeof(**obj));
	if (*obj == NULL)
		return got_error_from_errno("calloc");
	(*obj)->type = type;
	(*obj)->hdrlen = hdrlen;
	(*obj)->size = size;
	return NULL;
}

const struct got_error *
got_object_read_header(struct got_object **obj, int fd)
{
	const struct got_error *err;
	struct got_inflate_buf zb;
	char *buf;
	const size_t zbsize = 64;
	size_t outlen, totlen;
	int nbuf = 1;

	*obj = NULL;

	buf = malloc(zbsize);
	if (buf == NULL)
		return got_error_from_errno("malloc");

	err = got_inflate_init(&zb, buf, zbsize);
	if (err)
		return err;

	totlen = 0;
	do {
		err = got_inflate_read_fd(&zb, fd, &outlen);
		if (err)
			goto done;
		if (outlen == 0)
			break;
		totlen += outlen;
		if (memchr(zb.outbuf, '\0', outlen) == NULL) {
			char *newbuf;
			nbuf++;
			newbuf = recallocarray(buf, nbuf - 1, nbuf, zbsize);
			if (newbuf == NULL) {
				err = got_error_from_errno("recallocarray");
				goto done;
			}
			buf = newbuf;
			zb.outbuf = newbuf + totlen;
			zb.outlen = (nbuf * zbsize) - totlen;
		}
	} while (memchr(zb.outbuf, '\0', outlen) == NULL);

	err = got_object_parse_header(obj, buf, totlen);
done:
	free(buf);
	got_inflate_end(&zb);
	return err;
}

struct got_commit_object *
got_object_commit_alloc_partial(void)
{
	struct got_commit_object *commit;

	commit = calloc(1, sizeof(*commit));
	if (commit == NULL)
		return NULL;
	commit->tree_id = malloc(sizeof(*commit->tree_id));
	if (commit->tree_id == NULL) {
		free(commit);
		return NULL;
	}

	SIMPLEQ_INIT(&commit->parent_ids);

	return commit;
}

const struct got_error *
got_object_commit_add_parent(struct got_commit_object *commit,
    const char *id_str)
{
	const struct got_error *err = NULL;
	struct got_object_qid *qid;

	err = got_object_qid_alloc_partial(&qid);
	if (err)
		return err;

	if (!got_parse_sha1_digest(qid->id->sha1, id_str)) {
		err = got_error(GOT_ERR_BAD_OBJ_DATA);
		got_object_qid_free(qid);
		return err;
	}

	SIMPLEQ_INSERT_TAIL(&commit->parent_ids, qid, entry);
	commit->nparents++;

	return NULL;
}

static const struct got_error *
parse_gmtoff(time_t *gmtoff, const char *tzstr)
{
	int sign = 1;
	const char *p = tzstr;
	time_t h, m;

	*gmtoff = 0;

	if (*p == '-')
		sign = -1;
	else if (*p != '+')
		return got_error(GOT_ERR_BAD_OBJ_DATA);
	p++;
	if (!isdigit(*p) && !isdigit(*(p + 1)))
		return got_error(GOT_ERR_BAD_OBJ_DATA);
	h = (((*p - '0') * 10) + (*(p + 1) - '0'));

	p += 2;
	if (!isdigit(*p) && !isdigit(*(p + 1)))
		return got_error(GOT_ERR_BAD_OBJ_DATA);
	m = ((*p - '0') * 10) + (*(p + 1) - '0');

	*gmtoff = (h * 60 * 60 + m * 60) * sign;
	return NULL;
}

static const struct got_error *
parse_commit_time(time_t *time, time_t *gmtoff, char *committer)
{
	const struct got_error *err = NULL;
	const char *errstr;
	char *space, *tzstr;

	/* Parse and strip off trailing timezone indicator string. */
	space = strrchr(committer, ' ');
	if (space == NULL)
		return got_error(GOT_ERR_BAD_OBJ_DATA);
	tzstr = strdup(space + 1);
	if (tzstr == NULL)
		return got_error_from_errno("strdup");
	err = parse_gmtoff(gmtoff, tzstr);
	free(tzstr);
	if (err)
		return err;
	*space = '\0';

	/* Timestamp is separated from committer name + email by space. */
	space = strrchr(committer, ' ');
	if (space == NULL)
		return got_error(GOT_ERR_BAD_OBJ_DATA);

	/* Timestamp parsed here is expressed as UNIX timestamp (UTC). */
	*time = strtonum(space + 1, 0, INT64_MAX, &errstr);
	if (errstr)
		return got_error(GOT_ERR_BAD_OBJ_DATA);

	/* Strip off parsed time information, leaving just author and email. */
	*space = '\0';

	return NULL;
}

void
got_object_commit_close(struct got_commit_object *commit)
{
	if (commit->refcnt > 0) {
		commit->refcnt--;
		if (commit->refcnt > 0)
			return;
	}

	got_object_id_queue_free(&commit->parent_ids);
	free(commit->tree_id);
	free(commit->author);
	free(commit->committer);
	free(commit->logmsg);
	free(commit);
}

struct got_object_id *
got_object_commit_get_tree_id(struct got_commit_object *commit)
{
	return commit->tree_id;
}

int
got_object_commit_get_nparents(struct got_commit_object *commit)
{
	return commit->nparents;
}

const struct got_object_id_queue *
got_object_commit_get_parent_ids(struct got_commit_object *commit)
{
	return &commit->parent_ids;
}

const char *
got_object_commit_get_author(struct got_commit_object *commit)
{
	return commit->author;
}

time_t
got_object_commit_get_author_time(struct got_commit_object *commit)
{
	return commit->author_time;
}

time_t got_object_commit_get_author_gmtoff(struct got_commit_object *commit)
{
	return commit->author_gmtoff;
}

const char *
got_object_commit_get_committer(struct got_commit_object *commit)
{
	return commit->committer;
}

time_t
got_object_commit_get_committer_time(struct got_commit_object *commit)
{
	return commit->committer_time;
}

time_t
got_object_commit_get_committer_gmtoff(struct got_commit_object *commit)
{
	return commit->committer_gmtoff;
}

const struct got_error *
got_object_commit_get_logmsg(char **logmsg, struct got_commit_object *commit)
{
	const struct got_error *err = NULL;
	char *msg0, *msg, *line, *s;
	size_t len;
	int headers = 1;

	*logmsg = NULL;

	msg0 = strdup(commit->logmsg);
	if (msg0 == NULL)
		return got_error_from_errno("strdup");

	/* Copy log message line by line to strip out unusual headers... */
	msg = msg0;
	do {
		if ((line = strsep(&msg, "\n")) == NULL)
			break;

		if (headers == 1) {
			if (line[0] != '\0' &&
			    strncmp(line, GOT_COMMIT_LABEL_TREE,
			        strlen(GOT_COMMIT_LABEL_TREE)) != 0 &&
			    strncmp(line, GOT_COMMIT_LABEL_AUTHOR,
			        strlen(GOT_COMMIT_LABEL_AUTHOR)) != 0 &&
			    strncmp(line, GOT_COMMIT_LABEL_PARENT,
			        strlen(GOT_COMMIT_LABEL_PARENT)) != 0 &&
			    strncmp(line, GOT_COMMIT_LABEL_COMMITTER,
			        strlen(GOT_COMMIT_LABEL_COMMITTER)) != 0)
				continue;

			if (line[0] == '\0')
				headers = 0;
		}

		if (asprintf(&s, "%s%s\n",
		    *logmsg ? *logmsg : "", line) == -1) {
			err = got_error_from_errno("asprintf");
			goto done;
		}
		free(*logmsg);
		*logmsg = s;

	} while (line);

	/* Trim redundant trailing whitespace. */
	len = strlen(*logmsg);
	while (len > 1 && isspace((unsigned char)(*logmsg)[len - 2]) &&
	    isspace((unsigned char)(*logmsg)[len - 1])) {
		(*logmsg)[len - 1] = '\0';
		len--;
	}
done:
	free(msg0);
	if (err) {
		free(*logmsg);
		*logmsg = NULL;
	}
	return err;
}

const char *
got_object_commit_get_logmsg_raw(struct got_commit_object *commit)
{
	return commit->logmsg;
}

const struct got_error *
got_object_parse_commit(struct got_commit_object **commit, char *buf,
    size_t len)
{
	const struct got_error *err = NULL;
	char *s = buf;
	size_t label_len;
	ssize_t remain = (ssize_t)len;

	if (remain == 0)
		return got_error(GOT_ERR_BAD_OBJ_DATA);

	*commit = got_object_commit_alloc_partial();
	if (*commit == NULL)
		return got_error_from_errno("got_object_commit_alloc_partial");

	label_len = strlen(GOT_COMMIT_LABEL_TREE);
	if (strncmp(s, GOT_COMMIT_LABEL_TREE, label_len) == 0) {
		remain -= label_len;
		if (remain < SHA1_DIGEST_STRING_LENGTH) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		s += label_len;
		if (!got_parse_sha1_digest((*commit)->tree_id->sha1, s)) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		remain -= SHA1_DIGEST_STRING_LENGTH;
		s += SHA1_DIGEST_STRING_LENGTH;
	} else {
		err = got_error(GOT_ERR_BAD_OBJ_DATA);
		goto done;
	}

	label_len = strlen(GOT_COMMIT_LABEL_PARENT);
	while (strncmp(s, GOT_COMMIT_LABEL_PARENT, label_len) == 0) {
		remain -= label_len;
		if (remain < SHA1_DIGEST_STRING_LENGTH) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		s += label_len;
		err = got_object_commit_add_parent(*commit, s);
		if (err)
			goto done;

		remain -= SHA1_DIGEST_STRING_LENGTH;
		s += SHA1_DIGEST_STRING_LENGTH;
	}

	label_len = strlen(GOT_COMMIT_LABEL_AUTHOR);
	if (strncmp(s, GOT_COMMIT_LABEL_AUTHOR, label_len) == 0) {
		char *p;
		size_t slen;

		remain -= label_len;
		if (remain <= 0) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		s += label_len;
		p = memchr(s, '\n', remain);
		if (p == NULL) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		*p = '\0';
		slen = strlen(s);
		err = parse_commit_time(&(*commit)->author_time,
		    &(*commit)->author_gmtoff, s);
		if (err)
			goto done;
		(*commit)->author = strdup(s);
		if ((*commit)->author == NULL) {
			err = got_error_from_errno("strdup");
			goto done;
		}
		s += slen + 1;
		remain -= slen + 1;
	}

	label_len = strlen(GOT_COMMIT_LABEL_COMMITTER);
	if (strncmp(s, GOT_COMMIT_LABEL_COMMITTER, label_len) == 0) {
		char *p;
		size_t slen;

		remain -= label_len;
		if (remain <= 0) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		s += label_len;
		p = memchr(s, '\n', remain);
		if (p == NULL) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		*p = '\0';
		slen = strlen(s);
		err = parse_commit_time(&(*commit)->committer_time,
		    &(*commit)->committer_gmtoff, s);
		if (err)
			goto done;
		(*commit)->committer = strdup(s);
		if ((*commit)->committer == NULL) {
			err = got_error_from_errno("strdup");
			goto done;
		}
		s += slen + 1;
		remain -= slen + 1;
	}

	(*commit)->logmsg = strndup(s, remain);
	if ((*commit)->logmsg == NULL) {
		err = got_error_from_errno("strndup");
		goto done;
	}
done:
	if (err) {
		got_object_commit_close(*commit);
		*commit = NULL;
	}
	return err;
}

void
got_object_tree_entry_close(struct got_tree_entry *te)
{
	free(te->id);
	free(te->name);
	free(te);
}

void
got_object_tree_entries_close(struct got_tree_entries *entries)
{
	struct got_tree_entry *te;

	while (!SIMPLEQ_EMPTY(&entries->head)) {
		te = SIMPLEQ_FIRST(&entries->head);
		SIMPLEQ_REMOVE_HEAD(&entries->head, entry);
		got_object_tree_entry_close(te);
	}
}

void
got_object_tree_close(struct got_tree_object *tree)
{
	if (tree->refcnt > 0) {
		tree->refcnt--;
		if (tree->refcnt > 0)
			return;
	}

	got_object_tree_entries_close(&tree->entries);
	free(tree);
}

struct got_tree_entry *
got_alloc_tree_entry_partial(void)
{
	struct got_tree_entry *te;

	te = malloc(sizeof(*te));
	if (te == NULL)
		return NULL;

	te->id = malloc(sizeof(*te->id));
	if (te->id == NULL) {
		free(te);
		te = NULL;
	}
	return te;
}

static const struct got_error *
parse_tree_entry(struct got_tree_entry **te, size_t *elen, char *buf,
    size_t maxlen)
{
	char *p, *space;
	const struct got_error *err = NULL;

	*elen = 0;

	*te = got_alloc_tree_entry_partial();
	if (*te == NULL)
		return got_error_from_errno("got_alloc_tree_entry_partial");

	*elen = strnlen(buf, maxlen) + 1;
	if (*elen > maxlen) {
		free(*te);
		*te = NULL;
		return got_error(GOT_ERR_BAD_OBJ_DATA);
	}

	space = memchr(buf, ' ', *elen);
	if (space == NULL || space <= buf) {
		err = got_error(GOT_ERR_BAD_OBJ_DATA);
		free(*te);
		*te = NULL;
		return err;
	}
	(*te)->mode = 0;
	p = buf;
	while (p < space) {
		if (*p < '0' && *p > '7') {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		(*te)->mode <<= 3;
		(*te)->mode |= *p - '0';
		p++;
	}

	(*te)->name = strdup(space + 1);
	if (*elen > maxlen || maxlen - *elen < SHA1_DIGEST_LENGTH) {
		err = got_error(GOT_ERR_BAD_OBJ_DATA);
		goto done;
	}
	buf += *elen;
	memcpy((*te)->id->sha1, buf, SHA1_DIGEST_LENGTH);
	*elen += SHA1_DIGEST_LENGTH;
done:
	if (err) {
		got_object_tree_entry_close(*te);
		*te = NULL;
	}
	return err;
}

const struct got_error *
got_object_parse_tree(struct got_tree_object **tree, uint8_t *buf, size_t len)
{
	const struct got_error *err;
	size_t remain = len;
	struct got_pathlist_head pathlist;
	struct got_pathlist_entry *pe;

	TAILQ_INIT(&pathlist);

	if (remain == 0)
		return got_error(GOT_ERR_BAD_OBJ_DATA);

	*tree = calloc(1, sizeof(**tree));
	if (*tree == NULL)
		return got_error_from_errno("calloc");

	SIMPLEQ_INIT(&(*tree)->entries.head);

	while (remain > 0) {
		struct got_tree_entry *te;
		struct got_pathlist_entry *new = NULL;
		size_t elen;

		err = parse_tree_entry(&te, &elen, buf, remain);
		if (err)
			goto done;
		err = got_pathlist_insert(&new, &pathlist, te->name, te);
		if (err)
			goto done;
		if (new == NULL) {
			err = got_error(GOT_ERR_TREE_DUP_ENTRY);
			goto done;
		}
		buf += elen;
		remain -= elen;
	}

	if (remain != 0) {
		got_object_tree_close(*tree);
		*tree = NULL;
		err = got_error(GOT_ERR_BAD_OBJ_DATA);
		goto done;
	}

	TAILQ_FOREACH(pe, &pathlist, entry) {
		struct got_tree_entry *te = pe->data;
		(*tree)->entries.nentries++;
		SIMPLEQ_INSERT_TAIL(&(*tree)->entries.head, te, entry);
	}
done:
	got_pathlist_free(&pathlist);
	return err;
}

void
got_object_tag_close(struct got_tag_object *tag)
{
	if (tag->refcnt > 0) {
		tag->refcnt--;
		if (tag->refcnt > 0)
			return;
	}

	free(tag->tag);
	free(tag->tagger);
	free(tag->tagmsg);
	free(tag);
}

const struct got_error *
got_object_parse_tag(struct got_tag_object **tag, uint8_t *buf, size_t len)
{
	const struct got_error *err = NULL;
	size_t remain = len;
	char *s = buf;
	size_t label_len;

	if (remain == 0)
		return got_error(GOT_ERR_BAD_OBJ_DATA);

	*tag = calloc(1, sizeof(**tag));
	if (*tag == NULL)
		return got_error_from_errno("calloc");

	label_len = strlen(GOT_TAG_LABEL_OBJECT);
	if (strncmp(s, GOT_TAG_LABEL_OBJECT, label_len) == 0) {
		remain -= label_len;
		if (remain < SHA1_DIGEST_STRING_LENGTH) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		s += label_len;
		if (!got_parse_sha1_digest((*tag)->id.sha1, s)) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		remain -= SHA1_DIGEST_STRING_LENGTH;
		s += SHA1_DIGEST_STRING_LENGTH;
	} else {
		err = got_error(GOT_ERR_BAD_OBJ_DATA);
		goto done;
	}

	if (remain <= 0) {
		err = got_error(GOT_ERR_BAD_OBJ_DATA);
		goto done;
	}

	label_len = strlen(GOT_TAG_LABEL_TYPE);
	if (strncmp(s, GOT_TAG_LABEL_TYPE, label_len) == 0) {
		remain -= label_len;
		if (remain <= 0) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		s += label_len;
		if (strncmp(s, GOT_OBJ_LABEL_COMMIT,
		    strlen(GOT_OBJ_LABEL_COMMIT)) == 0) {
			(*tag)->obj_type = GOT_OBJ_TYPE_COMMIT;
			label_len = strlen(GOT_OBJ_LABEL_COMMIT);
			s += label_len;
			remain -= label_len;
		} else if (strncmp(s, GOT_OBJ_LABEL_TREE,
		    strlen(GOT_OBJ_LABEL_TREE)) == 0) {
			(*tag)->obj_type = GOT_OBJ_TYPE_TREE;
			label_len = strlen(GOT_OBJ_LABEL_TREE);
			s += label_len;
			remain -= label_len;
		} else if (strncmp(s, GOT_OBJ_LABEL_BLOB,
		    strlen(GOT_OBJ_LABEL_BLOB)) == 0) {
			(*tag)->obj_type = GOT_OBJ_TYPE_BLOB;
			label_len = strlen(GOT_OBJ_LABEL_BLOB);
			s += label_len;
			remain -= label_len;
		} else if (strncmp(s, GOT_OBJ_LABEL_TAG,
		    strlen(GOT_OBJ_LABEL_TAG)) == 0) {
			(*tag)->obj_type = GOT_OBJ_TYPE_TAG;
			label_len = strlen(GOT_OBJ_LABEL_TAG);
			s += label_len;
			remain -= label_len;
		} else {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}

		if (remain <= 0 || *s != '\n') {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		s++;
		remain--;
		if (remain <= 0) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
	} else {
		err = got_error(GOT_ERR_BAD_OBJ_DATA);
		goto done;
	}

	label_len = strlen(GOT_TAG_LABEL_TAG);
	if (strncmp(s, GOT_TAG_LABEL_TAG, label_len) == 0) {
		char *p;
		size_t slen;
		remain -= label_len;
		if (remain <= 0) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		s += label_len;
		p = memchr(s, '\n', remain);
		if (p == NULL) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		*p = '\0';
		slen = strlen(s);
		(*tag)->tag = strndup(s, slen);
		if ((*tag)->tag == NULL) {
			err = got_error_from_errno("strndup");
			goto done;
		}
		s += slen + 1;
		remain -= slen + 1;
		if (remain <= 0) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
	} else {
		err = got_error(GOT_ERR_BAD_OBJ_DATA);
		goto done;
	}

	label_len = strlen(GOT_TAG_LABEL_TAGGER);
	if (strncmp(s, GOT_TAG_LABEL_TAGGER, label_len) == 0) {
		char *p;
		size_t slen;

		remain -= label_len;
		if (remain <= 0) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		s += label_len;
		p = memchr(s, '\n', remain);
		if (p == NULL) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
		*p = '\0';
		slen = strlen(s);
		err = parse_commit_time(&(*tag)->tagger_time,
		    &(*tag)->tagger_gmtoff, s);
		if (err)
			goto done;
		(*tag)->tagger = strdup(s);
		if ((*tag)->tagger == NULL) {
			err = got_error_from_errno("strdup");
			goto done;
		}
		s += slen + 1;
		remain -= slen + 1;
		if (remain <= 0) {
			err = got_error(GOT_ERR_BAD_OBJ_DATA);
			goto done;
		}
	} else {
		/* Some old tags in the Linux git repo have no tagger. */
		(*tag)->tagger = strdup("");
		if ((*tag)->tagger == NULL) {
			err = got_error_from_errno("strdup");
			goto done;
		}
	}

	(*tag)->tagmsg = strndup(s, remain);
	if ((*tag)->tagmsg == NULL) {
		err = got_error_from_errno("strndup");
		goto done;
	}
done:
	if (err) {
		got_object_tag_close(*tag);
		*tag = NULL;
	}
	return err;
}

const struct got_error *
got_read_file_to_mem(uint8_t **outbuf, size_t *outlen, FILE *f)
{
	const struct got_error *err = NULL;
	static const size_t blocksize = 512;
	size_t n, total, remain;
	uint8_t *buf;

	*outbuf = NULL;
	*outlen = 0;

	buf = malloc(blocksize);
	if (buf == NULL)
		return got_error_from_errno("malloc");

	remain = blocksize;
	total = 0;
	for (;;) {
		if (remain == 0) {
			uint8_t *newbuf;
			newbuf = reallocarray(buf, 1, total + blocksize);
			if (newbuf == NULL) {
				err = got_error_from_errno("reallocarray");
				goto done;
			}
			buf = newbuf;
			remain += blocksize;
		}
		n = fread(buf + total, 1, remain, f);
		if (n == 0) {
			if (ferror(f)) {
				err = got_ferror(f, GOT_ERR_IO);
				goto done;
			}
			break; /* EOF */
		}
		remain -= n;
		total += n;
	};

done:
	if (err == NULL) {
		*outbuf = buf;
		*outlen = total;
	} else
		free(buf);
	return err;
}
