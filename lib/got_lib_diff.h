/*
 * Copyright (c) 2020 Stefan Sperling <stsp@openbsd.org>
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

#include "arraylist.h"
#include "diff_main.h"
#include "diff_output.h"

enum got_diff_algorithm {
	GOT_DIFF_ALGORITHM_MYERS,
	GOT_DIFF_ALGORITHM_PATIENCE,
};

enum got_diff_output_format {
	GOT_DIFF_OUTPUT_UNIDIFF,
	GOT_DIFF_OUTPUT_EDSCRIPT,
};

struct got_diffreg_result {
	struct diff_result *result;
	FILE *f1;
	char *map1;
	size_t size1;
	FILE *f2;
	char *map2;
	size_t size2;
	struct diff_data left;
	struct diff_data right;
};

#define GOT_DIFF_CONFLICT_MARKER_BEGIN	"<<<<<<<"
#define GOT_DIFF_CONFLICT_MARKER_ORIG	"|||||||"
#define GOT_DIFF_CONFLICT_MARKER_SEP	"======="
#define GOT_DIFF_CONFLICT_MARKER_END	">>>>>>>"

const struct diff_config *got_diff_get_config(enum got_diff_algorithm);
const struct got_error *got_diff_prepare_file(FILE **, char **, int *,
    size_t *, struct diff_data *, const struct diff_config *, int); 
const struct got_error *got_diffreg_prepared_files(struct got_diffreg_result **,
    const struct diff_config *, struct diff_data *, FILE *, char *, size_t,
    struct diff_data *, FILE *, char *, size_t);
const struct got_error *got_diff_blob_prepared_file(
    struct got_diffreg_result **, struct diff_data *, struct got_blob_object *,
    struct diff_data *, FILE *, char *, size_t, const struct diff_config *,
    int);
const struct got_error *got_diffreg(struct got_diffreg_result **, FILE *, FILE *,
    enum got_diff_algorithm, int);
const struct got_error *got_diffreg_output(off_t **, size_t *,
    struct got_diffreg_result *, FILE *, FILE *, const char *, const char *,
    enum got_diff_output_format, int, FILE *);
const struct got_error *got_diffreg_result_free(struct got_diffreg_result *);
const struct got_error *got_diffreg_result_free_left(
    struct got_diffreg_result *);
const struct got_error *got_diffreg_result_free_right(
    struct got_diffreg_result *);
const struct got_error *got_diffreg_close(FILE *, char *, size_t,
    FILE *, char *, size_t);

const struct got_error *got_merge_diff3(int *, int, const char *, const char *,
    const char *, const char *, const char *, const char *);

const struct got_error *got_diff_files(struct got_diffreg_result **, FILE *,
    const char *, FILE *, const char *, int, int, FILE *);
