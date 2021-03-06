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


#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sha1.h>
#include <zlib.h>
#include <uuid.h>

#include "got_compat.h"

#include "got_error.h"
#include "got_object.h"

#include "got_lib_delta.h"
#include "got_lib_inflate.h"
#include "got_lib_object.h"
#include "got_lib_sha1.h"

#ifndef nitems
#define nitems(_a) (sizeof(_a) / sizeof((_a)[0]))
#endif

static struct got_custom_error {
	struct got_error err;
	char msg[4080];
} custom_errors[16];

static struct got_custom_error *
get_custom_err(void)
{
	static unsigned int idx;
	return &custom_errors[(idx++) % nitems(custom_errors)];
}

const struct got_error *
got_error(int code)
{
	size_t i;

	for (i = 0; i < nitems(got_errors); i++) {
		if (code == got_errors[i].code)
			return &got_errors[i];
	}

	abort();
}

const struct got_error *
got_error_msg(int code, const char *msg)
{
	struct got_custom_error *cerr = get_custom_err();
	struct got_error *err = &cerr->err;
	size_t i;

	for (i = 0; i < nitems(got_errors); i++) {
		if (code == got_errors[i].code) {
			err->code = code;
			strlcpy(cerr->msg, msg, sizeof(cerr->msg));
			err->msg = cerr->msg;
			return err;
		}
	}

	abort();
}

const struct got_error *
got_error_from_errno(const char *prefix)
{
	struct got_custom_error *cerr = get_custom_err();
	struct got_error *err = &cerr->err;
	char strerr[128];

	strerror_r(errno, strerr, sizeof(strerr));
	snprintf(cerr->msg, sizeof(cerr->msg), "%s: %s", prefix, strerr);

	err->code = GOT_ERR_ERRNO;
	err->msg = cerr->msg;
	return err;
}

const struct got_error *
got_error_from_errno2(const char *prefix, const char *prefix2)
{
	struct got_custom_error *cerr = get_custom_err();
	struct got_error *err = &cerr->err;
	char strerr[128];

	strerror_r(errno, strerr, sizeof(strerr));
	snprintf(cerr->msg, sizeof(cerr->msg), "%s: %s: %s", prefix, prefix2,
	    strerr);

	err->code = GOT_ERR_ERRNO;
	err->msg = cerr->msg;
	return err;
}

const struct got_error *
got_error_from_errno3(const char *prefix, const char *prefix2,
    const char *prefix3)
{
	struct got_custom_error *cerr = get_custom_err();
	struct got_error *err = &cerr->err;
	char strerr[128];

	strerror_r(errno, strerr, sizeof(strerr));
	snprintf(cerr->msg, sizeof(cerr->msg), "%s: %s: %s: %s", prefix,
	    prefix2, prefix3, strerr);

	err->code = GOT_ERR_ERRNO;
	err->msg = cerr->msg;
	return err;
}

const struct got_error *
got_error_from_errno_fmt(const char *fmt, ...)
{
	struct got_custom_error *cerr = get_custom_err();
	struct got_error *err = &cerr->err;
	char buf[PATH_MAX * 4];
	char strerr[128];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	strerror_r(errno, strerr, sizeof(strerr));
	snprintf(cerr->msg, sizeof(cerr->msg), "%s: %s", buf, strerr);

	err->code = GOT_ERR_ERRNO;
	err->msg = cerr->msg;
	return err;
}

const struct got_error *
got_error_set_errno(int code, const char *prefix)
{
	errno = code;
	return got_error_from_errno(prefix);
}

const struct got_error *
got_ferror(FILE *f, int code)
{
	if (ferror(f))
		return got_error_from_errno("");
	return got_error(code);
}

const struct got_error *
got_error_no_obj(struct got_object_id *id)
{
	char msg[sizeof("object   not found") + SHA1_DIGEST_STRING_LENGTH];
	char id_str[SHA1_DIGEST_STRING_LENGTH];
	int ret;

	if (!got_sha1_digest_to_str(id->sha1, id_str, sizeof(id_str)))
		return got_error(GOT_ERR_NO_OBJ);

	ret = snprintf(msg, sizeof(msg), "object %s not found", id_str);
	if (ret == -1 || ret >= sizeof(msg))
		return got_error(GOT_ERR_NO_OBJ);

	return got_error_msg(GOT_ERR_NO_OBJ, msg);
}

const struct got_error *
got_error_not_ref(const char *refname)
{
	char msg[sizeof("reference   not found") + 1004];
	int ret;

	ret = snprintf(msg, sizeof(msg), "reference %s not found", refname);
	if (ret == -1 || ret >= sizeof(msg))
		return got_error(GOT_ERR_NOT_REF);

	return got_error_msg(GOT_ERR_NOT_REF, msg);
}

const struct got_error *
got_error_uuid(uint32_t uuid_status, const char *prefix)
{
	switch (uuid_status) {
	case uuid_s_ok:
		return NULL;
	case uuid_s_bad_version:
		return got_error(GOT_ERR_UUID_VERSION);
	case uuid_s_invalid_string_uuid:
		return got_error(GOT_ERR_UUID_INVALID);
	case uuid_s_no_memory:
		return got_error_set_errno(ENOMEM, prefix);
	default:
		return got_error(GOT_ERR_UUID);
	}
}

const struct got_error *
got_error_path(const char *path, int code)
{
	struct got_custom_error *cerr = get_custom_err();
	struct got_error *err = &cerr->err;
	size_t i;

	for (i = 0; i < nitems(got_errors); i++) {
		if (code == got_errors[i].code) {
			err->code = code;
			snprintf(cerr->msg, sizeof(cerr->msg), "%s: %s", path,
			    got_errors[i].msg);
			err->msg = cerr->msg;
			return err;
		}
	}

	abort();
}

const struct got_error *
got_error_fmt(int code, const char *fmt, ...)
{
	struct got_custom_error *cerr = get_custom_err();
	struct got_error *err = &cerr->err;
	char buf[PATH_MAX * 4];
	va_list ap;
	size_t i;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	for (i = 0; i < nitems(got_errors); i++) {
		if (code == got_errors[i].code) {
			err->code = code;
			snprintf(cerr->msg, sizeof(cerr->msg), "%s: %s", buf,
			    got_errors[i].msg);
			err->msg = cerr->msg;
			return err;
		}
	}

	abort();
}
