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

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/time.h>

#include <stdint.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sha1.h>
#include <unistd.h>
#include <zlib.h>

#include "got_compat.h"

#include "got_error.h"
#include "got_object.h"

#include "got_lib_delta.h"
#include "got_lib_inflate.h"
#include "got_lib_object.h"
#include "got_lib_object_parse.h"
#include "got_lib_privsep.h"

static volatile sig_atomic_t sigint_received;

static void
catch_sigint(int signo)
{
	sigint_received = 1;
}

static const struct got_error *
read_tag_object(struct got_tag_object **tag, FILE *f)
{
	const struct got_error *err = NULL;
	struct got_object *obj;
	size_t len;
	uint8_t *p;

	err = got_inflate_to_mem(&p, &len, NULL, f);
	if (err)
		return err;

	err = got_object_parse_header(&obj, p, len);
	if (err)
		return err;

	if (len < obj->hdrlen + obj->size) {
		err = got_error(GOT_ERR_BAD_OBJ_DATA);
		goto done;
	}

	/* Skip object header. */
	len -= obj->hdrlen;
	err = got_object_parse_tag(tag, p + obj->hdrlen, len);
done:
	free(p);
	got_object_close(obj);
	return err;
}

int
main(int argc, char *argv[])
{
	const struct got_error *err = NULL;
	struct imsgbuf ibuf;

	signal(SIGINT, catch_sigint);

	imsg_init(&ibuf, GOT_IMSG_FD_CHILD);

#ifndef PROFILE
	/* revoke access to most system calls */
	if (pledge("stdio recvfd", NULL) == -1) {
		err = got_error_from_errno("pledge");
		got_privsep_send_error(&ibuf, err);
		return 1;
	}
#endif

	for (;;) {
		struct imsg imsg;
		FILE *f = NULL;
		struct got_tag_object *tag = NULL;

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

		if (imsg.hdr.type != GOT_IMSG_TAG_REQUEST) {
			err = got_error(GOT_ERR_PRIVSEP_MSG);
			goto done;
		}

		if (imsg.fd == -1) {
			err = got_error(GOT_ERR_PRIVSEP_NO_FD);
			goto done;
		}

		/* Always assume file offset zero. */
		f = fdopen(imsg.fd, "rb");
		if (f == NULL) {
			err = got_error_from_errno("fdopen");
			goto done;
		}

		err = read_tag_object(&tag, f);
		if (err)
			goto done;

		err = got_privsep_send_tag(&ibuf, tag);
done:
		if (f) {
			if (fclose(f) != 0 && err == NULL)
				err = got_error_from_errno("fclose");
		} else if (imsg.fd != -1) {
			if (close(imsg.fd) != 0 && err == NULL)
				err = got_error_from_errno("close");
		}
		imsg_free(&imsg);
		if (err)
			break;
	}

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
