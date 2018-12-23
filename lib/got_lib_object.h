/*
 * Copyright (c) 2018 Stefan Sperling <stsp@openbsd.org>
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

struct got_object_id {
	u_int8_t sha1[SHA1_DIGEST_LENGTH];
};

struct got_object {
	int type;

	int flags;
#define GOT_OBJ_FLAG_PACKED		0x01
#define GOT_OBJ_FLAG_DELTIFIED		0x02

	size_t hdrlen;
	size_t size;
	struct got_object_id id;

	char *path_packfile;	/* if packed */
	int pack_idx;		/* if packed */
	off_t pack_offset;	/* if packed */
	struct got_delta_chain deltas; /* if deltified */
	int refcnt;		/* > 0 if open and/or cached */
};

struct got_commit_object {
	struct got_object_id *tree_id;
	unsigned int nparents;
	struct got_object_id_queue parent_ids;
	char *author;
	time_t author_time;	/* UTC */
	time_t author_gmtoff;
	char *committer;
	time_t committer_time;	/* UTC */
	time_t committer_gmtoff;
	char *logmsg;
	int refcnt;		/* > 0 if open and/or cached */
};

struct got_tree_object {
	struct got_tree_entries entries;
	int refcnt;
};

struct got_blob_object {
	FILE *f;
	struct got_zstream_buf zb;
	size_t hdrlen;
	size_t blocksize;
	uint8_t *read_buf;
	struct got_object_id id;
};

struct got_tag_object {
	struct got_object_id id;
	int obj_type;
	char *tag;
	time_t tagger_time;
	time_t tagger_gmtoff;
	char *tagger;
	char *tagmsg;
	int refcnt;		/* > 0 if open and/or cached */
};

struct got_object_id *got_object_get_id(struct got_object *);
const struct got_error *got_object_get_id_str(char **, struct got_object *);
const struct got_error *got_object_open(struct got_object **,
    struct got_repository *, struct got_object_id *);
const struct got_error *got_object_open_by_id_str(struct got_object **,
    struct got_repository *, const char *);
void got_object_close(struct got_object *);
const struct got_error *got_object_commit_open(struct got_commit_object **,
    struct got_repository *, struct got_object *);
const struct got_error *got_object_tree_open(struct got_tree_object **,
    struct got_repository *, struct got_object *);
const struct got_error *got_object_blob_open(struct got_blob_object **,
    struct got_repository *, struct got_object *, size_t);
const struct got_error *got_object_tag_open(struct got_tag_object **,
    struct got_repository *, struct got_object *);
