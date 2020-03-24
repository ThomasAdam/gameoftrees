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

#include <sys/queue.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sha1.h>
#include <zlib.h>
#include <time.h>

#include "got_error.h"
#include "got_object.h"
#include "got_path.h"

#include "got_lib_inflate.h"

#ifndef MIN
#define	MIN(_a,_b) ((_a) < (_b) ? (_a) : (_b))
#endif

const struct got_error *
got_inflate_init(struct got_inflate_buf *zb, uint8_t *outbuf, size_t bufsize,
    struct got_inflate_checksum *csum)
{
	const struct got_error *err = NULL;
	int zerr;

	memset(&zb->z, 0, sizeof(zb->z));

	zb->z.zalloc = Z_NULL;
	zb->z.zfree = Z_NULL;
	zerr = inflateInit(&zb->z);
	if (zerr != Z_OK) {
		if  (zerr == Z_ERRNO)
			return got_error_from_errno("inflateInit");
		if  (zerr == Z_MEM_ERROR) {
			errno = ENOMEM;
			return got_error_from_errno("inflateInit");
		}
		return got_error(GOT_ERR_DECOMPRESSION);
	}

	zb->inlen = zb->outlen = bufsize;

	zb->inbuf = calloc(1, zb->inlen);
	if (zb->inbuf == NULL) {
		err = got_error_from_errno("calloc");
		goto done;
	}

	zb->flags = 0;
	if (outbuf == NULL) {
		zb->outbuf = calloc(1, zb->outlen);
		if (zb->outbuf == NULL) {
			err = got_error_from_errno("calloc");
			goto done;
		}
		zb->flags |= GOT_INFLATE_F_OWN_OUTBUF;
	} else
		zb->outbuf = outbuf;

	zb->csum = csum;
done:
	if (err)
		got_inflate_end(zb);
	return err;
}

static void
csum_input(struct got_inflate_checksum *csum, const char *buf, size_t len)
{
	if (csum->input_crc)
		*csum->input_crc = crc32(*csum->input_crc, buf, len);

	if (csum->input_sha1)
		SHA1Update(csum->input_sha1, buf, len);
}

const struct got_error *
got_inflate_read(struct got_inflate_buf *zb, FILE *f, size_t *outlenp,
   size_t *consumed)
{
	size_t last_total_out = zb->z.total_out;
	size_t last_total_in = zb->z.total_in;
	z_stream *z = &zb->z;
	int ret = Z_ERRNO;

	z->next_out = zb->outbuf;
	z->avail_out = zb->outlen;

	*outlenp = 0;
	if (consumed)
		*consumed = 0;
	do {
		char *csum_in = NULL;
		size_t csum_avail = 0;

		if (z->avail_in == 0) {
			size_t n = fread(zb->inbuf, 1, zb->inlen, f);
			if (n == 0) {
				if (ferror(f))
					return got_ferror(f, GOT_ERR_IO);
				/* EOF */
				ret = Z_STREAM_END;
				break;
			}
			z->next_in = zb->inbuf;
			z->avail_in = n;
		}
		if (zb->csum) {
			csum_in = z->next_in;
			csum_avail = z->avail_in;
		}
		ret = inflate(z, Z_SYNC_FLUSH);
		if (zb->csum)
			csum_input(zb->csum, csum_in, csum_avail - z->avail_in);
	} while (ret == Z_OK && z->avail_out > 0);

	if (ret == Z_OK || ret == Z_BUF_ERROR) {
		zb->flags |= GOT_INFLATE_F_HAVE_MORE;
	} else {
		if (ret != Z_STREAM_END)
			return got_error(GOT_ERR_DECOMPRESSION);
		zb->flags &= ~GOT_INFLATE_F_HAVE_MORE;
	}

	*outlenp = z->total_out - last_total_out;
	if (consumed)
		*consumed += z->total_in - last_total_in;
	return NULL;
}

const struct got_error *
got_inflate_read_fd(struct got_inflate_buf *zb, int fd, size_t *outlenp,
    size_t *consumed)
{
	size_t last_total_out = zb->z.total_out;
	size_t last_total_in = zb->z.total_in;
	z_stream *z = &zb->z;
	int ret = Z_ERRNO;

	z->next_out = zb->outbuf;
	z->avail_out = zb->outlen;

	*outlenp = 0;
	if (consumed)
		*consumed = 0;
	do {
		char *csum_in = NULL;
		size_t csum_avail = 0;

		if (z->avail_in == 0) {
			ssize_t n = read(fd, zb->inbuf, zb->inlen);
			if (n < 0)
				return got_error_from_errno("read");
			else if (n == 0) {
				/* EOF */
				ret = Z_STREAM_END;
				break;
			}
			z->next_in = zb->inbuf;
			z->avail_in = n;
		}
		if (zb->csum) {
			csum_in = z->next_in;
			csum_avail = z->avail_in;
		}
		ret = inflate(z, Z_SYNC_FLUSH);
		if (zb->csum)
			csum_input(zb->csum, csum_in, csum_avail - z->avail_in);
	} while (ret == Z_OK && z->avail_out > 0);

	if (ret == Z_OK || ret == Z_BUF_ERROR) {
		zb->flags |= GOT_INFLATE_F_HAVE_MORE;
	} else {
		if (ret != Z_STREAM_END)
			return got_error(GOT_ERR_DECOMPRESSION);
		zb->flags &= ~GOT_INFLATE_F_HAVE_MORE;
	}

	*outlenp = z->total_out - last_total_out;
	if (consumed)
		*consumed += z->total_in - last_total_in;
	return NULL;
}

const struct got_error *
got_inflate_read_mmap(struct got_inflate_buf *zb, uint8_t *map, size_t offset,
    size_t len, size_t *outlenp, size_t *consumed)
{
	size_t last_total_out = zb->z.total_out;
	z_stream *z = &zb->z;
	int ret = Z_ERRNO;

	z->next_out = zb->outbuf;
	z->avail_out = zb->outlen;

	*outlenp = 0;
	*consumed = 0;

	do {
		char *csum_in = NULL;
		size_t csum_avail = 0;
		size_t last_total_in = zb->z.total_in;

		if (z->avail_in == 0) {
			if (len == 0) {
				/* EOF */
				ret = Z_STREAM_END;
				break;
			}
			z->next_in = map + offset + *consumed;
			z->avail_in = len - *consumed;
		}
		if (zb->csum) {
			csum_in = z->next_in;
			csum_avail = z->avail_in;
		}
		ret = inflate(z, Z_SYNC_FLUSH);
		if (zb->csum)
			csum_input(zb->csum, csum_in, csum_avail - z->avail_in);
		*consumed += z->total_in - last_total_in;
	} while (ret == Z_OK && z->avail_out > 0);

	if (ret == Z_OK || ret == Z_BUF_ERROR) {
		zb->flags |= GOT_INFLATE_F_HAVE_MORE;
	} else {
		if (ret != Z_STREAM_END)
			return got_error(GOT_ERR_DECOMPRESSION);
		zb->flags &= ~GOT_INFLATE_F_HAVE_MORE;
	}

	*outlenp = z->total_out - last_total_out;
	return NULL;
}

void
got_inflate_end(struct got_inflate_buf *zb)
{
	free(zb->inbuf);
	if (zb->flags & GOT_INFLATE_F_OWN_OUTBUF)
		free(zb->outbuf);
	inflateEnd(&zb->z);
}

const struct got_error *
got_inflate_to_mem(uint8_t **outbuf, size_t *outlen,
    size_t *consumed_total, FILE *f)
{
	const struct got_error *err;
	size_t avail, consumed;
	struct got_inflate_buf zb;
	void *newbuf;
	int nbuf = 1;

	if (outbuf) {
		*outbuf = malloc(GOT_INFLATE_BUFSIZE);
		if (*outbuf == NULL)
			return got_error_from_errno("malloc");
		err = got_inflate_init(&zb, *outbuf, GOT_INFLATE_BUFSIZE, NULL);
	} else
		err = got_inflate_init(&zb, NULL, GOT_INFLATE_BUFSIZE, NULL);
	if (err)
		return err;

	*outlen = 0;
	if (consumed_total)
		*consumed_total = 0;

	do {
		err = got_inflate_read(&zb, f, &avail, &consumed);
		if (err)
			goto done;
		*outlen += avail;
		if (consumed_total)
			*consumed_total += consumed;
		if (zb.flags & GOT_INFLATE_F_HAVE_MORE) {
			if (outbuf == NULL)
				continue;
			zb.outlen = (nbuf * GOT_INFLATE_BUFSIZE) - *outlen;
			newbuf = reallocarray(*outbuf, ++nbuf,
			   GOT_INFLATE_BUFSIZE);
			if (newbuf == NULL) {
				err = got_error_from_errno("reallocarray");
				free(*outbuf);
				*outbuf = NULL;
				*outlen = 0;
				goto done;
			}
			*outbuf = newbuf;
			zb.outbuf = newbuf + *outlen;
		}
	} while (zb.flags & GOT_INFLATE_F_HAVE_MORE);

done:
	got_inflate_end(&zb);
	return err;
}

const struct got_error *
got_inflate_to_mem_fd(uint8_t **outbuf, size_t *outlen,
    size_t *consumed_total, struct got_inflate_checksum *csum, 
    size_t expected_size, int infd)
{
	const struct got_error *err;
	size_t avail, consumed;
	struct got_inflate_buf zb;
	void *newbuf;
	int nbuf = 1;
	size_t bufsize = GOT_INFLATE_BUFSIZE;

	/* Optimize buffer size in case short reads should suffice. */
	if (expected_size > 0 && expected_size < bufsize)
		bufsize = expected_size;
		
	if (outbuf) {
		*outbuf = malloc(bufsize);
		if (*outbuf == NULL)
			return got_error_from_errno("malloc");
		err = got_inflate_init(&zb, *outbuf, GOT_INFLATE_BUFSIZE, csum);
	} else
		err = got_inflate_init(&zb, NULL, bufsize, csum);
	if (err)
		goto done;

	*outlen = 0;
	if (consumed_total)
		*consumed_total = 0;

	do {
		err = got_inflate_read_fd(&zb, infd, &avail, &consumed);
		if (err)
			goto done;
		*outlen += avail;
		if (consumed_total)
			*consumed_total += consumed;
		if (zb.flags & GOT_INFLATE_F_HAVE_MORE) {
			if (outbuf == NULL)
				continue;
			zb.outlen = (nbuf * GOT_INFLATE_BUFSIZE) - *outlen;
			newbuf = reallocarray(*outbuf, ++nbuf,
			    GOT_INFLATE_BUFSIZE);
			if (newbuf == NULL) {
				err = got_error_from_errno("reallocarray");
				free(*outbuf);
				*outbuf = NULL;
				*outlen = 0;
				goto done;
			}
			*outbuf = newbuf;
			zb.outbuf = newbuf + *outlen;
		}
	} while (zb.flags & GOT_INFLATE_F_HAVE_MORE);

done:
	got_inflate_end(&zb);
	return err;
}

const struct got_error *
got_inflate_to_mem_mmap(uint8_t **outbuf, size_t *outlen,
    size_t *consumed_total, struct got_inflate_checksum *csum, uint8_t *map,
    size_t offset, size_t len)
{
	const struct got_error *err;
	size_t avail, consumed;
	struct got_inflate_buf zb;
	void *newbuf;
	int nbuf = 1;

	if (outbuf) {
		*outbuf = malloc(GOT_INFLATE_BUFSIZE);
		if (*outbuf == NULL)
			return got_error_from_errno("malloc");
		err = got_inflate_init(&zb, *outbuf, GOT_INFLATE_BUFSIZE, csum);
		if (err) {
			free(*outbuf);
			*outbuf = NULL;
			return err;
		}
	} else {
		err = got_inflate_init(&zb, NULL, GOT_INFLATE_BUFSIZE, csum);
	}

	*outlen = 0;
	if (consumed_total)
		*consumed_total = 0;
	do {
		err = got_inflate_read_mmap(&zb, map, offset, len, &avail,
		    &consumed);
		if (err)
			goto done;
		offset += consumed;
		if (consumed_total)
			*consumed_total += consumed;
		len -= consumed;
		*outlen += avail;
		if (len == 0)
			break;
		if (zb.flags & GOT_INFLATE_F_HAVE_MORE) {
			if (outbuf == NULL)
				continue;
			newbuf = reallocarray(*outbuf, ++nbuf,
			    GOT_INFLATE_BUFSIZE);
			if (newbuf == NULL) {
				err = got_error_from_errno("reallocarray");
				free(*outbuf);
				*outbuf = NULL;
				*outlen = 0;
				goto done;
			}
			*outbuf = newbuf;
			zb.outbuf = newbuf + *outlen;
			zb.outlen = (nbuf * GOT_INFLATE_BUFSIZE) - *outlen;
		}
	} while (zb.flags & GOT_INFLATE_F_HAVE_MORE);
done:
	got_inflate_end(&zb);
	return err;
}

const struct got_error *
got_inflate_to_fd(size_t *outlen, FILE *infile, int outfd)
{
	const struct got_error *err = NULL;
	size_t avail;
	struct got_inflate_buf zb;

	err = got_inflate_init(&zb, NULL, GOT_INFLATE_BUFSIZE, NULL);
	if (err)
		goto done;

	*outlen = 0;

	do {
		err = got_inflate_read(&zb, infile, &avail, NULL);
		if (err)
			goto done;
		if (avail > 0) {
			ssize_t n;
			n = write(outfd, zb.outbuf, avail);
			if (n != avail) {
				err = got_error_from_errno("write");
				goto done;
			}
			*outlen += avail;
		}
	} while (zb.flags & GOT_INFLATE_F_HAVE_MORE);

done:
	if (err == NULL) {
		if (lseek(outfd, SEEK_SET, 0) == -1)
			err = got_error_from_errno("lseek");
	}
	got_inflate_end(&zb);
	return err;
}

const struct got_error *
got_inflate_to_file(size_t *outlen, FILE *infile, FILE *outfile)
{
	const struct got_error *err;
	size_t avail;
	struct got_inflate_buf zb;

	err = got_inflate_init(&zb, NULL, GOT_INFLATE_BUFSIZE, NULL);
	if (err)
		goto done;

	*outlen = 0;

	do {
		err = got_inflate_read(&zb, infile, &avail, NULL);
		if (err)
			goto done;
		if (avail > 0) {
			size_t n;
			n = fwrite(zb.outbuf, avail, 1, outfile);
			if (n != 1) {
				err = got_ferror(outfile, GOT_ERR_IO);
				goto done;
			}
			*outlen += avail;
		}
	} while (zb.flags & GOT_INFLATE_F_HAVE_MORE);

done:
	if (err == NULL)
		rewind(outfile);
	got_inflate_end(&zb);
	return err;
}

const struct got_error *
got_inflate_to_file_fd(size_t *outlen, size_t *consumed_total,
    struct got_inflate_checksum *csum, int infd, FILE *outfile)
{
	const struct got_error *err;
	size_t avail, consumed;
	struct got_inflate_buf zb;

	err = got_inflate_init(&zb, NULL, GOT_INFLATE_BUFSIZE, csum);
	if (err)
		goto done;

	*outlen = 0;
	if (consumed_total)
		*consumed_total = 0;
	do {
		err = got_inflate_read_fd(&zb, infd, &avail, &consumed);
		if (err)
			goto done;
		if (avail > 0) {
			size_t n;
			n = fwrite(zb.outbuf, avail, 1, outfile);
			if (n != 1) {
				err = got_ferror(outfile, GOT_ERR_IO);
				goto done;
			}
			*outlen += avail;
			if (consumed_total)
				*consumed_total += consumed;
		}
	} while (zb.flags & GOT_INFLATE_F_HAVE_MORE);

done:
	if (err == NULL)
		rewind(outfile);
	got_inflate_end(&zb);
	return err;
}

const struct got_error *
got_inflate_to_file_mmap(size_t *outlen, size_t *consumed_total,
    struct got_inflate_checksum *csum, uint8_t *map, size_t offset,
    size_t len, FILE *outfile)
{
	const struct got_error *err;
	size_t avail, consumed;
	struct got_inflate_buf zb;

	err = got_inflate_init(&zb, NULL, GOT_INFLATE_BUFSIZE, csum);
	if (err)
		goto done;

	*outlen = 0;
	if (consumed_total)
		*consumed_total = 0;
	do {
		err = got_inflate_read_mmap(&zb, map, offset, len, &avail,
		    &consumed);
		if (err)
			goto done;
		offset += consumed;
		if (consumed_total)
			*consumed_total += consumed;
		len -= consumed;
		if (avail > 0) {
			size_t n;
			n = fwrite(zb.outbuf, avail, 1, outfile);
			if (n != 1) {
				err = got_ferror(outfile, GOT_ERR_IO);
				goto done;
			}
			*outlen += avail;
		}
	} while (zb.flags & GOT_INFLATE_F_HAVE_MORE);

done:
	if (err == NULL)
		rewind(outfile);
	got_inflate_end(&zb);
	return err;
}
