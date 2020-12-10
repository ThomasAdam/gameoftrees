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

/*
 * State information for a tracked file in a work tree.
 * When written to disk, multi-byte fields are written in big-endian.
 * Some fields are based on results from stat(2). These are only used in
 * order to detect modifications made to on-disk files, they are never
 * applied back to the filesystem.
 */
struct got_fileindex_entry {
	RB_ENTRY(got_fileindex_entry) entry;
	uint64_t ctime_sec;
	uint64_t ctime_nsec;
	uint64_t mtime_sec;
	uint64_t mtime_nsec;
	uint32_t uid;
	uint32_t gid;
	/*
	 * On-disk size is truncated to the lower 32 bits.
	 * The value is only used to check for modifications anyway.
	 */
	uint32_t size;

	uint16_t mode;
#define GOT_FILEIDX_MODE_FILE_TYPE	0x000f
#define GOT_FILEIDX_MODE_FILE_TYPE_ONDISK	0x0003
#define GOT_FILEIDX_MODE_FILE_TYPE_STAGED	0x000c
#define GOT_FILEIDX_MODE_FILE_TYPE_STAGED_SHIFT	2
#define GOT_FILEIDX_MODE_REGULAR_FILE	1
#define GOT_FILEIDX_MODE_SYMLINK	2
#define GOT_FILEIDX_MODE_BAD_SYMLINK	3
#define GOT_FILEIDX_MODE_PERMS		0xfff0
#define GOT_FILEIDX_MODE_PERMS_SHIFT	4

	/* SHA1 of corresponding blob in repository. */
	uint8_t blob_sha1[SHA1_DIGEST_LENGTH];

	/* SHA1 of corresponding base commit in repository. */
	uint8_t commit_sha1[SHA1_DIGEST_LENGTH];

	uint32_t flags;

	/*
	 * UNIX-style path, relative to work tree root.
	 * Variable length, and NUL-padded to a multiple of 8 on disk.
	 */
	char *path;

	/*
	 * (since GOT_FILE_INDEX_VERSION 2)
	 * SHA1 of staged blob in repository if stage equals either
	 * GOT_FILEIDX_STAGE_MODIFY or GOT_FILEIDX_STAGE_ADD.
	 * Otherwise, this field is not written to disk.
	 */
	uint8_t staged_blob_sha1[SHA1_DIGEST_LENGTH];
};

/* Modifications explicitly staged for commit. */
#define GOT_FILEIDX_STAGE_NONE		0
#define GOT_FILEIDX_STAGE_MODIFY	1
#define GOT_FILEIDX_STAGE_ADD		2
#define GOT_FILEIDX_STAGE_DELETE	3

struct got_fileindex;

RB_HEAD(got_fileindex_tree, got_fileindex_entry);

size_t got_fileindex_entry_path_len(const struct got_fileindex_entry *);

static inline int
got_fileindex_cmp(const struct got_fileindex_entry *e1,
    const struct got_fileindex_entry *e2)
{
	return got_path_cmp(e1->path, e2->path,
	    got_fileindex_entry_path_len(e1),
	    got_fileindex_entry_path_len(e2));
}

RB_PROTOTYPE(got_fileindex_tree, got_fileindex_entry, entry, got_fileindex_cmp);

/* On-disk file index header structure. */
struct got_fileindex_hdr {
	uint32_t signature;	/* big-endian */
#define GOT_FILE_INDEX_SIGNATURE	0x676f7449 /* 'g', 'o', 't', 'I' */
	uint32_t version;	/* big-endian */
#define GOT_FILE_INDEX_VERSION	2
	uint32_t nentries;	/* big-endian */
	/* list of concatenated fileindex entries */
	uint8_t sha1[SHA1_DIGEST_LENGTH]; /* checksum of above on-disk data */
};

mode_t got_fileindex_entry_perms_get(struct got_fileindex_entry *);
uint16_t got_fileindex_perms_from_st(struct stat *);
mode_t got_fileindex_perms_to_st(struct got_fileindex_entry *);

const struct got_error *got_fileindex_entry_update(struct got_fileindex_entry *,
    int, const char *, uint8_t *, uint8_t *, int);
const struct got_error *got_fileindex_entry_alloc(struct got_fileindex_entry **,
    const char *);
void got_fileindex_entry_free(struct got_fileindex_entry *);

struct got_fileindex *got_fileindex_alloc(void);
void got_fileindex_free(struct got_fileindex *);
const struct got_error *got_fileindex_write(struct got_fileindex *, FILE *);
const struct got_error *got_fileindex_entry_add(struct got_fileindex *,
    struct got_fileindex_entry *);
void got_fileindex_entry_remove(struct got_fileindex *,
    struct got_fileindex_entry *);
struct got_fileindex_entry *got_fileindex_entry_get(struct got_fileindex *,
    const char *, size_t);
const struct got_error *got_fileindex_read(struct got_fileindex *, FILE *);
typedef const struct got_error *(*got_fileindex_cb)(void *,
    struct got_fileindex_entry *);
const struct got_error *got_fileindex_for_each_entry_safe(
    struct got_fileindex *, got_fileindex_cb cb, void *);

typedef const struct got_error *(*got_fileindex_diff_tree_old_new_cb)(void *,
    struct got_fileindex_entry *, struct got_tree_entry *, const char *);
typedef const struct got_error *(*got_fileindex_diff_tree_old_cb)(void *,
    struct got_fileindex_entry *, const char *);
typedef const struct got_error *(*got_fileindex_diff_tree_new_cb)(void *,
    struct got_tree_entry *, const char *);
struct got_fileindex_diff_tree_cb {
	got_fileindex_diff_tree_old_new_cb diff_old_new;
	got_fileindex_diff_tree_old_cb diff_old;
	got_fileindex_diff_tree_new_cb diff_new;
};
const struct got_error *got_fileindex_diff_tree(struct got_fileindex *,
    struct got_tree_object *, const char *, const char *,
    struct got_repository *, struct got_fileindex_diff_tree_cb *, void *);

typedef const struct got_error *(*got_fileindex_diff_dir_old_new_cb)(void *,
    struct got_fileindex_entry *, struct dirent *, const char *, int);
typedef const struct got_error *(*got_fileindex_diff_dir_old_cb)(void *,
    struct got_fileindex_entry *, const char *);
typedef const struct got_error *(*got_fileindex_diff_dir_new_cb)(void *,
    struct dirent *, const char *, int);
typedef const struct got_error *(*got_fileindex_diff_dir_traverse)(void *,
    const char *, int);
struct got_fileindex_diff_dir_cb {
	got_fileindex_diff_dir_old_new_cb diff_old_new;
	got_fileindex_diff_dir_old_cb diff_old;
	got_fileindex_diff_dir_new_cb diff_new;
	got_fileindex_diff_dir_traverse diff_traverse;
};
const struct got_error *got_fileindex_diff_dir(struct got_fileindex *, int,
    const char *, const char *, struct got_repository *,
    struct got_fileindex_diff_dir_cb *, void *);

int got_fileindex_entry_has_blob(struct got_fileindex_entry *);
int got_fileindex_entry_has_commit(struct got_fileindex_entry *);
int got_fileindex_entry_has_file_on_disk(struct got_fileindex_entry *);
uint32_t got_fileindex_entry_stage_get(const struct got_fileindex_entry *);
void got_fileindex_entry_stage_set(struct got_fileindex_entry *ie, uint32_t);
int got_fileindex_entry_filetype_get(struct got_fileindex_entry *);
void got_fileindex_entry_filetype_set(struct got_fileindex_entry *, int);
void got_fileindex_entry_staged_filetype_set(struct got_fileindex_entry *, int);
int got_fileindex_entry_staged_filetype_get(struct got_fileindex_entry *);

void got_fileindex_entry_mark_deleted_from_disk(struct got_fileindex_entry *);
