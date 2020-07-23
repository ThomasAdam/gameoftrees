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

#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/stat.h>

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sha1.h>
#include <endian.h>
#include <limits.h>
#include <unistd.h>
#include <uuid.h>

#include "got_error.h"
#include "got_object.h"
#include "got_path.h"

#include "got_lib_fileindex.h"
#include "got_lib_worktree.h"

/* got_fileindex_entry flags */
#define GOT_FILEIDX_F_PATH_LEN		0x00000fff
#define GOT_FILEIDX_F_STAGE		0x0000f000
#define GOT_FILEIDX_F_STAGE_SHIFT	12
#define GOT_FILEIDX_F_NOT_FLUSHED	0x00010000
#define GOT_FILEIDX_F_NO_BLOB		0x00020000
#define GOT_FILEIDX_F_NO_COMMIT		0x00040000
#define GOT_FILEIDX_F_NO_FILE_ON_DISK	0x00080000
#define GOT_FILEIDX_F_REMOVE_ON_FLUSH	0x00100000

struct got_fileindex {
	struct got_fileindex_tree entries;
	int nentries; /* Does not include entries marked for removal. */
#define GOT_FILEIDX_MAX_ENTRIES INT_MAX
};

uint16_t
got_fileindex_perms_from_st(struct stat *sb)
{
	uint16_t perms = (sb->st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));
	return (perms << GOT_FILEIDX_MODE_PERMS_SHIFT);
}

mode_t
got_fileindex_perms_to_st(struct got_fileindex_entry *ie)
{
	mode_t perms = (ie->mode >> GOT_FILEIDX_MODE_PERMS_SHIFT);
	return (S_IFREG | (perms & (S_IRWXU | S_IRWXG | S_IRWXO)));
}

const struct got_error *
got_fileindex_entry_update(struct got_fileindex_entry *ie,
    const char *ondisk_path, uint8_t *blob_sha1, uint8_t *commit_sha1,
    int update_timestamps)
{
	struct stat sb;

	if (lstat(ondisk_path, &sb) != 0) {
		if (!((ie->flags & GOT_FILEIDX_F_NO_FILE_ON_DISK) &&
		    errno == ENOENT))
			return got_error_from_errno2("lstat", ondisk_path);
	} else {
		if (sb.st_mode & S_IFDIR)
			return got_error_set_errno(EISDIR, ondisk_path);
		ie->flags &= ~GOT_FILEIDX_F_NO_FILE_ON_DISK;
	}


	if ((ie->flags & GOT_FILEIDX_F_NO_FILE_ON_DISK) == 0) {
		if (update_timestamps) {
			ie->ctime_sec = sb.st_ctime;
			ie->ctime_nsec = sb.st_ctimensec;
			ie->mtime_sec = sb.st_mtime;
			ie->mtime_nsec = sb.st_mtimensec;
		}
		ie->uid = sb.st_uid;
		ie->gid = sb.st_gid;
		ie->size = (sb.st_size & 0xffffffff);
		if (S_ISLNK(sb.st_mode))
			ie->mode = GOT_FILEIDX_MODE_SYMLINK;
		else {
			ie->mode = GOT_FILEIDX_MODE_REGULAR_FILE;
			ie->mode |= got_fileindex_perms_from_st(&sb);
		}
	}

	if (blob_sha1) {
		memcpy(ie->blob_sha1, blob_sha1, SHA1_DIGEST_LENGTH);
		ie->flags &= ~GOT_FILEIDX_F_NO_BLOB;
	} else
		ie->flags |= GOT_FILEIDX_F_NO_BLOB;

	if (commit_sha1) {
		memcpy(ie->commit_sha1, commit_sha1, SHA1_DIGEST_LENGTH);
		ie->flags &= ~GOT_FILEIDX_F_NO_COMMIT;
	} else
		ie->flags |= GOT_FILEIDX_F_NO_COMMIT;

	return NULL;
}

void
got_fileindex_entry_mark_deleted_from_disk(struct got_fileindex_entry *ie)
{
	ie->flags |= GOT_FILEIDX_F_NO_FILE_ON_DISK;
}

const struct got_error *
got_fileindex_entry_alloc(struct got_fileindex_entry **ie,
    const char *relpath)
{
	size_t len;

	*ie = calloc(1, sizeof(**ie));
	if (*ie == NULL)
		return got_error_from_errno("calloc");

	(*ie)->path = strdup(relpath);
	if ((*ie)->path == NULL) {
		const struct got_error *err = got_error_from_errno("strdup");
		free(*ie);
		*ie = NULL;
		return err;
	}

	len = strlen(relpath);
	if (len > GOT_FILEIDX_F_PATH_LEN)
		len = GOT_FILEIDX_F_PATH_LEN;
	(*ie)->flags |= len;

	return NULL;
}

void
got_fileindex_entry_free(struct got_fileindex_entry *ie)
{
	free(ie->path);
	free(ie);
}

size_t
got_fileindex_entry_path_len(const struct got_fileindex_entry *ie)
{
	return (size_t)(ie->flags & GOT_FILEIDX_F_PATH_LEN);
}

uint32_t
got_fileindex_entry_stage_get(const struct got_fileindex_entry *ie)
{
	return ((ie->flags & GOT_FILEIDX_F_STAGE) >> GOT_FILEIDX_F_STAGE_SHIFT);
}

void
got_fileindex_entry_stage_set(struct got_fileindex_entry *ie, uint32_t stage)
{
	ie->flags &= ~GOT_FILEIDX_F_STAGE;
	ie->flags |= ((stage << GOT_FILEIDX_F_STAGE_SHIFT) &
	    GOT_FILEIDX_F_STAGE);
}

int
got_fileindex_entry_filetype_get(struct got_fileindex_entry *ie)
{
	return (ie->mode & GOT_FILEIDX_MODE_FILE_TYPE_ONDISK);
}

const struct got_error *
got_fileindex_entry_filetype_set(struct got_fileindex_entry *ie, int type)
{
	switch (type) {
	case GOT_FILEIDX_MODE_REGULAR_FILE:
	case GOT_FILEIDX_MODE_SYMLINK:
	case GOT_FILEIDX_MODE_BAD_SYMLINK:
		break;
	default:
		return got_error(GOT_ERR_BAD_FILETYPE);
	}

	ie->mode &= ~GOT_FILEIDX_MODE_FILE_TYPE_ONDISK;
	ie->mode |= type;
	return NULL;
}

void
got_fileindex_entry_staged_filetype_set(struct got_fileindex_entry *ie, int type)
{
	ie->mode &= ~GOT_FILEIDX_MODE_FILE_TYPE_STAGED;
	ie->mode |= ((type << GOT_FILEIDX_MODE_FILE_TYPE_STAGED_SHIFT) &
	    GOT_FILEIDX_MODE_FILE_TYPE_STAGED);
}

int
got_fileindex_entry_staged_filetype_get(struct got_fileindex_entry *ie)
{
	return (ie->mode & GOT_FILEIDX_MODE_FILE_TYPE_STAGED) >>
	    GOT_FILEIDX_MODE_FILE_TYPE_STAGED_SHIFT;
}

int
got_fileindex_entry_has_blob(struct got_fileindex_entry *ie)
{
	return (ie->flags & GOT_FILEIDX_F_NO_BLOB) == 0;
}

int
got_fileindex_entry_has_commit(struct got_fileindex_entry *ie)
{
	return (ie->flags & GOT_FILEIDX_F_NO_COMMIT) == 0;
}

int
got_fileindex_entry_has_file_on_disk(struct got_fileindex_entry *ie)
{
	return (ie->flags & GOT_FILEIDX_F_NO_FILE_ON_DISK) == 0;
}

static const struct got_error *
add_entry(struct got_fileindex *fileindex, struct got_fileindex_entry *ie)
{
	if (fileindex->nentries >= GOT_FILEIDX_MAX_ENTRIES)
		return got_error(GOT_ERR_NO_SPACE);

	RB_INSERT(got_fileindex_tree, &fileindex->entries, ie);
	fileindex->nentries++;
	return NULL;
}

const struct got_error *
got_fileindex_entry_add(struct got_fileindex *fileindex,
    struct got_fileindex_entry *ie)
{
	/* Flag this entry until it gets written out to disk. */
	ie->flags |= GOT_FILEIDX_F_NOT_FLUSHED;

	return add_entry(fileindex, ie);
}

void
got_fileindex_entry_remove(struct got_fileindex *fileindex,
    struct got_fileindex_entry *ie)
{
	/*
	 * Removing an entry from the RB tree immediately breaks
	 * in-progress iterations over file index entries.
	 * So flag this entry for removal and remove it once the index
	 * is written out to disk. Meanwhile, pretend this entry no longer
	 * exists if we get queried for it again before then.
	 */
	ie->flags |= GOT_FILEIDX_F_REMOVE_ON_FLUSH;
	fileindex->nentries--;
}

struct got_fileindex_entry *
got_fileindex_entry_get(struct got_fileindex *fileindex, const char *path,
    size_t path_len)
{
	struct got_fileindex_entry *ie;
	struct got_fileindex_entry key;
	memset(&key, 0, sizeof(key));
	key.path = (char *)path;
	key.flags = (path_len & GOT_FILEIDX_F_PATH_LEN);
	ie = RB_FIND(got_fileindex_tree, &fileindex->entries, &key);
	if (ie && (ie->flags & GOT_FILEIDX_F_REMOVE_ON_FLUSH))
		return NULL;
	return ie;
}

const struct got_error *
got_fileindex_for_each_entry_safe(struct got_fileindex *fileindex,
    got_fileindex_cb cb, void *cb_arg)
{
	const struct got_error *err;
	struct got_fileindex_entry *ie, *tmp;

	RB_FOREACH_SAFE(ie, got_fileindex_tree, &fileindex->entries, tmp) {
		if (ie->flags & GOT_FILEIDX_F_REMOVE_ON_FLUSH)
			continue;
		err = (*cb)(cb_arg, ie);
		if (err)
			return err;
	}
	return NULL;
}

struct got_fileindex *
got_fileindex_alloc(void)
{
	struct got_fileindex *fileindex;

	fileindex = calloc(1, sizeof(*fileindex));
	if (fileindex == NULL)
		return NULL;

	RB_INIT(&fileindex->entries);
	return fileindex;
}

void
got_fileindex_free(struct got_fileindex *fileindex)
{
	struct got_fileindex_entry *ie;

	while ((ie = RB_MIN(got_fileindex_tree, &fileindex->entries))) {
		RB_REMOVE(got_fileindex_tree, &fileindex->entries, ie);
		got_fileindex_entry_free(ie);
	}
	free(fileindex);
}

static const struct got_error *
write_fileindex_val64(SHA1_CTX *ctx, uint64_t val, FILE *outfile)
{
	size_t n;

	val = htobe64(val);
	SHA1Update(ctx, (uint8_t *)&val, sizeof(val));
	n = fwrite(&val, 1, sizeof(val), outfile);
	if (n != sizeof(val))
		return got_ferror(outfile, GOT_ERR_IO);
	return NULL;
}

static const struct got_error *
write_fileindex_val32(SHA1_CTX *ctx, uint32_t val, FILE *outfile)
{
	size_t n;

	val = htobe32(val);
	SHA1Update(ctx, (uint8_t *)&val, sizeof(val));
	n = fwrite(&val, 1, sizeof(val), outfile);
	if (n != sizeof(val))
		return got_ferror(outfile, GOT_ERR_IO);
	return NULL;
}

static const struct got_error *
write_fileindex_val16(SHA1_CTX *ctx, uint16_t val, FILE *outfile)
{
	size_t n;

	val = htobe16(val);
	SHA1Update(ctx, (uint8_t *)&val, sizeof(val));
	n = fwrite(&val, 1, sizeof(val), outfile);
	if (n != sizeof(val))
		return got_ferror(outfile, GOT_ERR_IO);
	return NULL;
}

static const struct got_error *
write_fileindex_path(SHA1_CTX *ctx, const char *path, FILE *outfile)
{
	size_t n, len, pad = 0;
	static const uint8_t zero[8] = { 0 };

	len = strlen(path);
	while ((len + pad) % 8 != 0)
		pad++;
	if (pad == 0)
		pad = 8; /* NUL-terminate */

	SHA1Update(ctx, path, len);
	n = fwrite(path, 1, len, outfile);
	if (n != len)
		return got_ferror(outfile, GOT_ERR_IO);
	SHA1Update(ctx, zero, pad);
	n = fwrite(zero, 1, pad, outfile);
	if (n != pad)
		return got_ferror(outfile, GOT_ERR_IO);
	return NULL;
}

static const struct got_error *
write_fileindex_entry(SHA1_CTX *ctx, struct got_fileindex_entry *ie,
    FILE *outfile)
{
	const struct got_error *err;
	size_t n;
	uint32_t stage;

	err = write_fileindex_val64(ctx, ie->ctime_sec, outfile);
	if (err)
		return err;
	err = write_fileindex_val64(ctx, ie->ctime_nsec, outfile);
	if (err)
		return err;
	err = write_fileindex_val64(ctx, ie->mtime_sec, outfile);
	if (err)
		return err;
	err = write_fileindex_val64(ctx, ie->mtime_nsec, outfile);
	if (err)
		return err;

	err = write_fileindex_val32(ctx, ie->uid, outfile);
	if (err)
		return err;
	err = write_fileindex_val32(ctx, ie->gid, outfile);
	if (err)
		return err;
	err = write_fileindex_val32(ctx, ie->size, outfile);
	if (err)
		return err;

	err = write_fileindex_val16(ctx, ie->mode, outfile);
	if (err)
		return err;

	SHA1Update(ctx, ie->blob_sha1, SHA1_DIGEST_LENGTH);
	n = fwrite(ie->blob_sha1, 1, SHA1_DIGEST_LENGTH, outfile);
	if (n != SHA1_DIGEST_LENGTH)
		return got_ferror(outfile, GOT_ERR_IO);

	SHA1Update(ctx, ie->commit_sha1, SHA1_DIGEST_LENGTH);
	n = fwrite(ie->commit_sha1, 1, SHA1_DIGEST_LENGTH, outfile);
	if (n != SHA1_DIGEST_LENGTH)
		return got_ferror(outfile, GOT_ERR_IO);

	err = write_fileindex_val32(ctx, ie->flags, outfile);
	if (err)
		return err;

	err = write_fileindex_path(ctx, ie->path, outfile);
	if (err)
		return err;

	stage = got_fileindex_entry_stage_get(ie);
	if (stage == GOT_FILEIDX_STAGE_MODIFY ||
	    stage == GOT_FILEIDX_STAGE_ADD) {
		SHA1Update(ctx, ie->staged_blob_sha1, SHA1_DIGEST_LENGTH);
		n = fwrite(ie->staged_blob_sha1, 1, SHA1_DIGEST_LENGTH,
		    outfile);
		if (n != SHA1_DIGEST_LENGTH)
			return got_ferror(outfile, GOT_ERR_IO);
	}

	return NULL;
}

const struct got_error *
got_fileindex_write(struct got_fileindex *fileindex, FILE *outfile)
{
	const struct got_error *err = NULL;
	struct got_fileindex_hdr hdr;
	SHA1_CTX ctx;
	uint8_t sha1[SHA1_DIGEST_LENGTH];
	size_t n;
	struct got_fileindex_entry *ie, *tmp;

	SHA1Init(&ctx);

	hdr.signature = htobe32(GOT_FILE_INDEX_SIGNATURE);
	hdr.version = htobe32(GOT_FILE_INDEX_VERSION);
	hdr.nentries = htobe32(fileindex->nentries);

	SHA1Update(&ctx, (uint8_t *)&hdr.signature, sizeof(hdr.signature));
	SHA1Update(&ctx, (uint8_t *)&hdr.version, sizeof(hdr.version));
	SHA1Update(&ctx, (uint8_t *)&hdr.nentries, sizeof(hdr.nentries));
	n = fwrite(&hdr.signature, 1, sizeof(hdr.signature), outfile);
	if (n != sizeof(hdr.signature))
		return got_ferror(outfile, GOT_ERR_IO);
	n = fwrite(&hdr.version, 1, sizeof(hdr.version), outfile);
	if (n != sizeof(hdr.version))
		return got_ferror(outfile, GOT_ERR_IO);
	n = fwrite(&hdr.nentries, 1, sizeof(hdr.nentries), outfile);
	if (n != sizeof(hdr.nentries))
		return got_ferror(outfile, GOT_ERR_IO);

	RB_FOREACH_SAFE(ie, got_fileindex_tree, &fileindex->entries, tmp) {
		ie->flags &= ~GOT_FILEIDX_F_NOT_FLUSHED;
		if (ie->flags & GOT_FILEIDX_F_REMOVE_ON_FLUSH) {
			RB_REMOVE(got_fileindex_tree, &fileindex->entries, ie);
			got_fileindex_entry_free(ie);
			continue;
		}
		err = write_fileindex_entry(&ctx, ie, outfile);
		if (err)
			return err;
	}

	SHA1Final(sha1, &ctx);
	n = fwrite(sha1, 1, sizeof(sha1), outfile);
	if (n != sizeof(sha1))
		return got_ferror(outfile, GOT_ERR_IO);

	if (fflush(outfile) != 0)
		return got_error_from_errno("fflush");

	return NULL;
}

static const struct got_error *
read_fileindex_val64(uint64_t *val, SHA1_CTX *ctx, FILE *infile)
{
	size_t n;

	n = fread(val, 1, sizeof(*val), infile);
	if (n != sizeof(*val))
		return got_ferror(infile, GOT_ERR_FILEIDX_BAD);
	SHA1Update(ctx, (uint8_t *)val, sizeof(*val));
	*val = be64toh(*val);
	return NULL;
}

static const struct got_error *
read_fileindex_val32(uint32_t *val, SHA1_CTX *ctx, FILE *infile)
{
	size_t n;

	n = fread(val, 1, sizeof(*val), infile);
	if (n != sizeof(*val))
		return got_ferror(infile, GOT_ERR_FILEIDX_BAD);
	SHA1Update(ctx, (uint8_t *)val, sizeof(*val));
	*val = be32toh(*val);
	return NULL;
}

static const struct got_error *
read_fileindex_val16(uint16_t *val, SHA1_CTX *ctx, FILE *infile)
{
	size_t n;

	n = fread(val, 1, sizeof(*val), infile);
	if (n != sizeof(*val))
		return got_ferror(infile, GOT_ERR_FILEIDX_BAD);
	SHA1Update(ctx, (uint8_t *)val, sizeof(*val));
	*val = be16toh(*val);
	return NULL;
}

static const struct got_error *
read_fileindex_path(char **path, SHA1_CTX *ctx, FILE *infile)
{
	const struct got_error *err = NULL;
	const size_t chunk_size = 8;
	size_t n, len = 0, totlen = chunk_size;

	*path = malloc(totlen);
	if (*path == NULL)
		return got_error_from_errno("malloc");

	do {
		if (len + chunk_size > totlen) {
			char *p = reallocarray(*path, totlen + chunk_size, 1);
			if (p == NULL) {
				err = got_error_from_errno("reallocarray");
				break;
			}
			totlen += chunk_size;
			*path = p;
		}
		n = fread(*path + len, 1, chunk_size, infile);
		if (n != chunk_size) {
			err = got_ferror(infile, GOT_ERR_FILEIDX_BAD);
			break;
		}
		SHA1Update(ctx, *path + len, chunk_size);
		len += chunk_size;
	} while (memchr(*path + len - chunk_size, '\0', chunk_size) == NULL);

	if (err) {
		free(*path);
		*path = NULL;
	}
	return err;
}

static const struct got_error *
read_fileindex_entry(struct got_fileindex_entry **iep, SHA1_CTX *ctx,
    FILE *infile, uint32_t version)
{
	const struct got_error *err;
	struct got_fileindex_entry *ie;
	size_t n;

	*iep = NULL;

	ie = calloc(1, sizeof(*ie));
	if (ie == NULL)
		return got_error_from_errno("calloc");

	err = read_fileindex_val64(&ie->ctime_sec, ctx, infile);
	if (err)
		goto done;
	err = read_fileindex_val64(&ie->ctime_nsec, ctx, infile);
	if (err)
		goto done;
	err = read_fileindex_val64(&ie->mtime_sec, ctx, infile);
	if (err)
		goto done;
	err = read_fileindex_val64(&ie->mtime_nsec, ctx, infile);
	if (err)
		goto done;

	err = read_fileindex_val32(&ie->uid, ctx, infile);
	if (err)
		goto done;
	err = read_fileindex_val32(&ie->gid, ctx, infile);
	if (err)
		goto done;
	err = read_fileindex_val32(&ie->size, ctx, infile);
	if (err)
		goto done;

	err = read_fileindex_val16(&ie->mode, ctx, infile);
	if (err)
		goto done;

	n = fread(ie->blob_sha1, 1, SHA1_DIGEST_LENGTH, infile);
	if (n != SHA1_DIGEST_LENGTH) {
		err = got_ferror(infile, GOT_ERR_FILEIDX_BAD);
		goto done;
	}
	SHA1Update(ctx, ie->blob_sha1, SHA1_DIGEST_LENGTH);

	n = fread(ie->commit_sha1, 1, SHA1_DIGEST_LENGTH, infile);
	if (n != SHA1_DIGEST_LENGTH) {
		err = got_ferror(infile, GOT_ERR_FILEIDX_BAD);
		goto done;
	}
	SHA1Update(ctx, ie->commit_sha1, SHA1_DIGEST_LENGTH);

	err = read_fileindex_val32(&ie->flags, ctx, infile);
	if (err)
		goto done;

	err = read_fileindex_path(&ie->path, ctx, infile);
	if (err)
		goto done;

	if (version >= 2) {
		uint32_t stage = got_fileindex_entry_stage_get(ie);
		if (stage == GOT_FILEIDX_STAGE_MODIFY ||
		    stage == GOT_FILEIDX_STAGE_ADD) {
			n = fread(ie->staged_blob_sha1, 1, SHA1_DIGEST_LENGTH,
			    infile);
			if (n != SHA1_DIGEST_LENGTH) {
				err = got_ferror(infile, GOT_ERR_FILEIDX_BAD);
				goto done;
			}
			SHA1Update(ctx, ie->staged_blob_sha1, SHA1_DIGEST_LENGTH);
		}
	} else {
		/* GOT_FILE_INDEX_VERSION 1 does not support staging. */
		ie->flags &= ~GOT_FILEIDX_F_STAGE;
	}

done:
	if (err)
		got_fileindex_entry_free(ie);
	else
		*iep = ie;
	return err;
}

const struct got_error *
got_fileindex_read(struct got_fileindex *fileindex, FILE *infile)
{
	const struct got_error *err = NULL;
	struct got_fileindex_hdr hdr;
	SHA1_CTX ctx;
	struct got_fileindex_entry *ie;
	uint8_t sha1_expected[SHA1_DIGEST_LENGTH];
	uint8_t sha1[SHA1_DIGEST_LENGTH];
	size_t n;
	int i;

	SHA1Init(&ctx);

	n = fread(&hdr.signature, 1, sizeof(hdr.signature), infile);
	if (n != sizeof(hdr.signature)) {
		if (n == 0) /* EOF */
			return NULL;
		return got_ferror(infile, GOT_ERR_FILEIDX_BAD);
	}
	n = fread(&hdr.version, 1, sizeof(hdr.version), infile);
	if (n != sizeof(hdr.version)) {
		if (n == 0) /* EOF */
			return NULL;
		return got_ferror(infile, GOT_ERR_FILEIDX_BAD);
	}
	n = fread(&hdr.nentries, 1, sizeof(hdr.nentries), infile);
	if (n != sizeof(hdr.nentries)) {
		if (n == 0) /* EOF */
			return NULL;
		return got_ferror(infile, GOT_ERR_FILEIDX_BAD);
	}

	SHA1Update(&ctx, (uint8_t *)&hdr.signature, sizeof(hdr.signature));
	SHA1Update(&ctx, (uint8_t *)&hdr.version, sizeof(hdr.version));
	SHA1Update(&ctx, (uint8_t *)&hdr.nentries, sizeof(hdr.nentries));

	hdr.signature = be32toh(hdr.signature);
	hdr.version = be32toh(hdr.version);
	hdr.nentries = be32toh(hdr.nentries);

	if (hdr.signature != GOT_FILE_INDEX_SIGNATURE)
		return got_error(GOT_ERR_FILEIDX_SIG);
	if (hdr.version > GOT_FILE_INDEX_VERSION)
		return got_error(GOT_ERR_FILEIDX_VER);

	for (i = 0; i < hdr.nentries; i++) {
		err = read_fileindex_entry(&ie, &ctx, infile, hdr.version);
		if (err)
			return err;
		err = add_entry(fileindex, ie);
		if (err)
			return err;
	}

	n = fread(sha1_expected, 1, sizeof(sha1_expected), infile);
	if (n != sizeof(sha1_expected))
		return got_ferror(infile, GOT_ERR_FILEIDX_BAD);
	SHA1Final(sha1, &ctx);
	if (memcmp(sha1, sha1_expected, SHA1_DIGEST_LENGTH) != 0)
		return got_error(GOT_ERR_FILEIDX_CSUM);

	return NULL;
}

static struct got_fileindex_entry *
walk_fileindex(struct got_fileindex *fileindex, struct got_fileindex_entry *ie)
{
	struct got_fileindex_entry *next;

	next = RB_NEXT(got_fileindex_tree, &fileindex->entries, ie);

	/* Skip entries which were added or removed by diff callbacks. */
	while (next && (next->flags & (GOT_FILEIDX_F_NOT_FLUSHED |
	    GOT_FILEIDX_F_REMOVE_ON_FLUSH)))
		next = RB_NEXT(got_fileindex_tree, &fileindex->entries, next);

	return next;
}

static const struct got_error *
diff_fileindex_tree(struct got_fileindex *, struct got_fileindex_entry **ie,
    struct got_tree_object *tree, const char *, const char *,
    struct got_repository *, struct got_fileindex_diff_tree_cb *, void *);

static const struct got_error *
walk_tree(struct got_tree_entry **next, struct got_fileindex *fileindex,
    struct got_fileindex_entry **ie, struct got_tree_object *tree, int *tidx,
    const char *path, const char *entry_name, struct got_repository *repo,
    struct got_fileindex_diff_tree_cb *cb, void *cb_arg)
{
	const struct got_error *err = NULL;
	struct got_tree_entry *te = got_object_tree_get_entry(tree, *tidx);

	if (!got_object_tree_entry_is_submodule(te) &&
	    S_ISDIR(got_tree_entry_get_mode(te))) {
		char *subpath;
		struct got_tree_object *subtree;

		if (asprintf(&subpath, "%s%s%s", path,
		    path[0] == '\0' ? "" : "/",
		    got_tree_entry_get_name(te)) == -1)
			return got_error_from_errno("asprintf");

		err = got_object_open_as_tree(&subtree, repo,
		    got_tree_entry_get_id(te));
		if (err) {
			free(subpath);
			return err;
		}

		err = diff_fileindex_tree(fileindex, ie, subtree, subpath,
		    entry_name, repo, cb, cb_arg);
		free(subpath);
		got_object_tree_close(subtree);
		if (err)
			return err;
	}

	(*tidx)++;
	*next = got_object_tree_get_entry(tree, *tidx);
	return NULL;
}

static const struct got_error *
diff_fileindex_tree(struct got_fileindex *fileindex,
    struct got_fileindex_entry **ie, struct got_tree_object *tree,
    const char *path, const char *entry_name, struct got_repository *repo,
    struct got_fileindex_diff_tree_cb *cb, void *cb_arg)
{
	const struct got_error *err = NULL;
	struct got_tree_entry *te = NULL;
	size_t path_len = strlen(path);
	struct got_fileindex_entry *next;
	int tidx = 0;

	te = got_object_tree_get_entry(tree, tidx);
	while ((*ie && got_path_is_child((*ie)->path, path, path_len)) || te) {
		if (te && *ie) {
			char *te_path;
			const char *te_name = got_tree_entry_get_name(te);
			int cmp;
			if (asprintf(&te_path, "%s/%s", path, te_name) == -1) {
				err = got_error_from_errno("asprintf");
				break;
			}
			cmp = got_path_cmp((*ie)->path, te_path,
			    got_fileindex_entry_path_len(*ie), strlen(te_path));
			free(te_path);
			if (cmp == 0) {
				if (got_path_is_child((*ie)->path, path,
				    path_len) &&
				    !got_object_tree_entry_is_submodule(te) &&
				    (entry_name == NULL ||
				    strcmp(te_name, entry_name) == 0)) {
					err = cb->diff_old_new(cb_arg, *ie, te,
					    path);
					if (err || entry_name)
						break;
				}
				*ie = walk_fileindex(fileindex, *ie);
				err = walk_tree(&te, fileindex, ie, tree, &tidx,
				    path, entry_name, repo, cb, cb_arg);
			} else if (cmp < 0) {
				next = walk_fileindex(fileindex, *ie);
				if (got_path_is_child((*ie)->path, path,
				    path_len) && (entry_name == NULL ||
				    strcmp(te_name, entry_name) == 0)) {
					err = cb->diff_old(cb_arg, *ie, path);
					if (err || entry_name)
						break;
				}
				*ie = next;
			} else {
				if ((entry_name == NULL ||
				    strcmp(te_name, entry_name) == 0)) {
					err = cb->diff_new(cb_arg, te, path);
					if (err || entry_name)
						break;
				}
				err = walk_tree(&te, fileindex, ie, tree, &tidx,
				    path, entry_name, repo, cb, cb_arg);
			}
			if (err)
				break;
		} else if (*ie) {
			next = walk_fileindex(fileindex, *ie);
			if (got_path_is_child((*ie)->path, path, path_len) &&
			    (entry_name == NULL ||
			    (te && strcmp(got_tree_entry_get_name(te),
			    entry_name) == 0))) {
				err = cb->diff_old(cb_arg, *ie, path);
				if (err || entry_name)
					break;
			}
			*ie = next;
		} else if (te) {
			if (!got_object_tree_entry_is_submodule(te) &&
			    (entry_name == NULL ||
			    strcmp(got_tree_entry_get_name(te), entry_name)
			    == 0)) {
				err = cb->diff_new(cb_arg, te, path);
				if (err || entry_name)
					break;
			}
			err = walk_tree(&te, fileindex, ie, tree, &tidx, path,
			    entry_name, repo, cb, cb_arg);
			if (err)
				break;
		}
	}

	return err;
}

const struct got_error *
got_fileindex_diff_tree(struct got_fileindex *fileindex,
    struct got_tree_object *tree, const char *path, const char *entry_name,
    struct got_repository *repo,
    struct got_fileindex_diff_tree_cb *cb, void *cb_arg)
{
	struct got_fileindex_entry *ie;
	ie = RB_MIN(got_fileindex_tree, &fileindex->entries);
	while (ie && !got_path_is_child(ie->path, path, strlen(path)))
		ie = walk_fileindex(fileindex, ie);
	return diff_fileindex_tree(fileindex, &ie, tree, path, entry_name, repo,
	    cb, cb_arg);
}

static const struct got_error *
diff_fileindex_dir(struct got_fileindex *, struct got_fileindex_entry **,
    struct got_pathlist_head *, int, const char *, const char *,
    struct got_repository *, struct got_fileindex_diff_dir_cb *, void *);

static const struct got_error *
read_dirlist(struct got_pathlist_head *dirlist, DIR *dir, const char *path)
{
	const struct got_error *err = NULL;
	struct got_pathlist_entry *new = NULL;
	struct dirent *dep = NULL;
	struct dirent *de = NULL;

	for (;;) {
		de = malloc(sizeof(struct dirent) + NAME_MAX + 1);
		if (de == NULL) {
			err = got_error_from_errno("malloc");
			break;
		}

		if (readdir_r(dir, de, &dep) != 0) {
			err = got_error_from_errno("readdir_r");
			free(de);
			break;
		}
		if (dep == NULL) {
			free(de);
			break;
		}

		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0 ||
		    (path[0] == '\0' &&
		    strcmp(de->d_name, GOT_WORKTREE_GOT_DIR) == 0)) {
			free(de);
			continue;
		}

		err = got_pathlist_insert(&new, dirlist, de->d_name, de);
		if (err) {
			free(de);
			break;
		}
		if (new == NULL) {
			err = got_error(GOT_ERR_DIR_DUP_ENTRY);
			free(de);
			break;
		}
	}

	return err;
}

void
free_dirlist(struct got_pathlist_head *dirlist)
{
	struct got_pathlist_entry *dle;

	TAILQ_FOREACH(dle, dirlist, entry)
		free(dle->data);
	got_pathlist_free(dirlist);
}

static const struct got_error *
walk_dir(struct got_pathlist_entry **next, struct got_fileindex *fileindex,
    struct got_fileindex_entry **ie, struct got_pathlist_entry *dle, int fd,
    const char *path, const char *rootpath, struct got_repository *repo,
    struct got_fileindex_diff_dir_cb *cb, void *cb_arg)
{
	const struct got_error *err = NULL;
	struct dirent *de = dle->data;
	DIR *subdir = NULL;
	int subdirfd = -1;
	int type;

	*next = NULL;

	if (de->d_type == DT_UNKNOWN) {
		/* Occurs on NFS mounts without "readdir plus" RPC. */
		char *dir_path;
		if (asprintf(&dir_path, "%s/%s", rootpath, path) == -1)
			return got_error_from_errno("asprintf");
		err = got_path_dirent_type(&type, dir_path, de);
		free(dir_path);
		if (err)
			return err;
	} else
		type = de->d_type;

	if (type == DT_DIR) {
		char *subpath;
		char *subdirpath;
		struct got_pathlist_head subdirlist;

		TAILQ_INIT(&subdirlist);

		if (asprintf(&subpath, "%s%s%s", path,
		    path[0] == '\0' ? "" : "/", de->d_name) == -1)
			return got_error_from_errno("asprintf");

		if (asprintf(&subdirpath, "%s/%s", rootpath, subpath) == -1) {
			free(subpath);
			return got_error_from_errno("asprintf");
		}

		subdirfd = openat(fd, de->d_name,
		    O_RDONLY | O_NOFOLLOW | O_DIRECTORY);
		if (subdirfd == -1) {
			if (errno == EACCES) {
				*next = TAILQ_NEXT(dle, entry);
				return NULL;
			}
			err = got_error_from_errno2("openat", subdirpath);
			free(subpath);
			free(subdirpath);
			return err;
		}

		subdir = fdopendir(subdirfd);
		if (subdir == NULL)
			return got_error_from_errno2("fdopendir", path);
		subdirfd = -1;
		err = read_dirlist(&subdirlist, subdir, subdirpath);
		if (err) {
			free(subpath);
			free(subdirpath);
			closedir(subdir);
			return err;
		}
		err = diff_fileindex_dir(fileindex, ie, &subdirlist,
		    dirfd(subdir), rootpath, subpath, repo, cb, cb_arg);
		if (subdir && closedir(subdir) == -1 && err == NULL)
			err = got_error_from_errno2("closedir", subdirpath);
		free(subpath);
		free(subdirpath);
		free_dirlist(&subdirlist);
		if (err)
			return err;
	}

	*next = TAILQ_NEXT(dle, entry);
	return NULL;
}

static const struct got_error *
diff_fileindex_dir(struct got_fileindex *fileindex,
    struct got_fileindex_entry **ie, struct got_pathlist_head *dirlist,
    int dirfd, const char *rootpath, const char *path,
    struct got_repository *repo,
    struct got_fileindex_diff_dir_cb *cb, void *cb_arg)
{
	const struct got_error *err = NULL;
	struct dirent *de = NULL;
	size_t path_len = strlen(path);
	struct got_pathlist_entry *dle;

	if (cb->diff_traverse) {
		err = cb->diff_traverse(cb_arg, path, dirfd);
		if (err)
			return err;
	}

	dle = TAILQ_FIRST(dirlist);
	while ((*ie && got_path_is_child((*ie)->path, path, path_len)) || dle) {
		if (dle && *ie) {
			char *de_path;
			int cmp;
			de = dle->data;
			if (asprintf(&de_path, "%s/%s", path,
			    de->d_name) == -1) {
				err = got_error_from_errno("asprintf");
				break;
			}
			cmp = got_path_cmp((*ie)->path, de_path,
			    got_fileindex_entry_path_len(*ie),
			    strlen(path) + 1 + de->d_namlen);
			free(de_path);
			if (cmp == 0) {
				err = cb->diff_old_new(cb_arg, *ie, de, path,
				    dirfd);
				if (err)
					break;
				*ie = walk_fileindex(fileindex, *ie);
				err = walk_dir(&dle, fileindex, ie, dle, dirfd,
				    path, rootpath, repo, cb, cb_arg);
			} else if (cmp < 0 ) {
				err = cb->diff_old(cb_arg, *ie, path);
				if (err)
					break;
				*ie = walk_fileindex(fileindex, *ie);
			} else {
				err = cb->diff_new(cb_arg, de, path, dirfd);
				if (err)
					break;
				err = walk_dir(&dle, fileindex, ie, dle, dirfd,
				    path, rootpath, repo, cb, cb_arg);
			}
			if (err)
				break;
		} else if (*ie) {
			err = cb->diff_old(cb_arg, *ie, path);
			if (err)
				break;
			*ie = walk_fileindex(fileindex, *ie);
		} else if (dle) {
			de = dle->data;
			err = cb->diff_new(cb_arg, de, path, dirfd);
			if (err)
				break;
			err = walk_dir(&dle, fileindex, ie, dle, dirfd, path,
			    rootpath, repo, cb, cb_arg);
			if (err)
				break;
		}
	}

	return err;
}

const struct got_error *
got_fileindex_diff_dir(struct got_fileindex *fileindex, int fd,
    const char *rootpath, const char *path, struct got_repository *repo,
    struct got_fileindex_diff_dir_cb *cb, void *cb_arg)
{
	const struct got_error *err;
	struct got_fileindex_entry *ie;
	struct got_pathlist_head dirlist;
	int fd2;
	DIR *dir;

	TAILQ_INIT(&dirlist);

	/* 
	 * Duplicate the file descriptor so we can call closedir() below
	 * without closing the file descriptor passed in by our caller.
	 */
	fd2 = dup(fd);
	if (fd2 == -1)
		return got_error_from_errno2("dup", path);
	if (lseek(fd2, 0, SEEK_SET) == -1) {
		err = got_error_from_errno2("lseek", path);
		close(fd2);
		return err;
	}
	dir = fdopendir(fd2);
	if (dir == NULL) {
		err = got_error_from_errno2("fdopendir", path);
		close(fd2);
		return err;
	}
	err = read_dirlist(&dirlist, dir, path);
	if (err) {
		closedir(dir);
		return err;
	}

	ie = RB_MIN(got_fileindex_tree, &fileindex->entries);
	while (ie && !got_path_is_child(ie->path, path, strlen(path)))
		ie = walk_fileindex(fileindex, ie);
	err = diff_fileindex_dir(fileindex, &ie, &dirlist, dirfd(dir),
	    rootpath, path, repo, cb, cb_arg);

	if (closedir(dir) == -1 && err == NULL)
		err = got_error_from_errno2("closedir", path);
	free_dirlist(&dirlist);
	return err;
}

RB_GENERATE(got_fileindex_tree, got_fileindex_entry, entry, got_fileindex_cmp);
