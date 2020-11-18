/*
 * Copyright (c) 2020 Neels Hofmeyr <neels@hofmeyr.de>
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

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/queue.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "got_object.h"
#include "got_opentemp.h"
#include "got_error.h"

#include "got_lib_diff.h"

const struct diff_algo_config myers_then_patience;
const struct diff_algo_config myers_then_myers_divide;
const struct diff_algo_config patience;
const struct diff_algo_config myers_divide;

const struct diff_algo_config myers_then_patience = (struct diff_algo_config){
	.impl = diff_algo_myers,
	.permitted_state_size = 1024 * 1024 * sizeof(int),
	.fallback_algo = &patience,
};

const struct diff_algo_config myers_then_myers_divide =
	(struct diff_algo_config){
	.impl = diff_algo_myers,
	.permitted_state_size = 1024 * 1024 * sizeof(int),
	.fallback_algo = &myers_divide,
};

const struct diff_algo_config patience = (struct diff_algo_config){
	.impl = diff_algo_patience,
	/* After subdivision, do Patience again: */
	.inner_algo = &patience,
	/* If subdivision failed, do Myers Divide et Impera: */
	.fallback_algo = &myers_then_myers_divide,
};

const struct diff_algo_config myers_divide = (struct diff_algo_config){
	.impl = diff_algo_myers_divide,
	/* When division succeeded, start from the top: */
	.inner_algo = &myers_then_myers_divide,
	/* (fallback_algo = NULL implies diff_algo_none). */
};

/* If the state for a forward-Myers is small enough, use Myers, otherwise first
 * do a Myers-divide. */
const struct diff_config diff_config_myers_then_myers_divide = {
	.atomize_func = diff_atomize_text_by_line,
	.algo = &myers_then_myers_divide,
};

/* If the state for a forward-Myers is small enough, use Myers, otherwise first
 * do a Patience. */
const struct diff_config diff_config_myers_then_patience = {
	.atomize_func = diff_atomize_text_by_line,
	.algo = &myers_then_patience,
};

/* Directly force Patience as a first divider of the source file. */
const struct diff_config diff_config_patience = {
	.atomize_func = diff_atomize_text_by_line,
	.algo = &patience,
};

/* Directly force Patience as a first divider of the source file. */
const struct diff_config diff_config_no_algo = {
	.atomize_func = diff_atomize_text_by_line,
};

const struct got_error *
got_diffreg_close(FILE *f1, char *p1, size_t size1,
    FILE *f2, char *p2, size_t size2)
{
	const struct got_error *err = NULL;

	if (p1 && munmap(p1, size1) == -1 && err == NULL)
		err = got_error_from_errno("munmap");
	if (p2 && munmap(p2, size2) == -1 && err == NULL)
		err = got_error_from_errno("munmap");
	if (f1 && fclose(f1) != 0 && err == NULL)
		err = got_error_from_errno("fclose");
	if (f2 && fclose(f2) != 0 && err == NULL)
		err = got_error_from_errno("fclose");
	return err;
}

const struct diff_config *
got_diff_get_config(enum got_diff_algorithm algorithm)
{
	switch (algorithm) {
	case GOT_DIFF_ALGORITHM_PATIENCE:
		return &diff_config_patience;
	case GOT_DIFF_ALGORITHM_MYERS:
		return &diff_config_myers_then_myers_divide;
	}
	return NULL; /* should not happen */
}

const struct got_error *
got_diff_prepare_file(FILE *f, char **p, size_t *size,
    struct diff_data *diff_data, const struct diff_config *cfg,
    int ignore_whitespace)
{
	const struct got_error *err = NULL;
	struct stat st;
	int diff_flags = 0, rc;

	*size = 0;

	diff_flags |= DIFF_FLAG_SHOW_PROTOTYPES;
	if (ignore_whitespace)
		diff_flags |= DIFF_FLAG_IGNORE_WHITESPACE;

	if (fstat(fileno(f), &st) == -1) {
		err = got_error_from_errno("fstat");
		goto done;
	}
#ifndef GOT_DIFF_NO_MMAP
	*p = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE,
	    fileno(f), 0);
	if (*p == MAP_FAILED)
#endif
		*p = NULL; /* fall back on file I/O */

	rc = diff_atomize_file(diff_data, cfg, f, *p, st.st_size, diff_flags);
	if (rc) {
		err = got_error_set_errno(rc, "diff_atomize_file");
		goto done;
	}
done:
	if (err)
		diff_data_free(diff_data);
	else
		*size = st.st_size;
	return err;
}

const struct got_error *
got_diffreg(struct got_diffreg_result **diffreg_result, FILE *f1, FILE *f2,
    enum got_diff_algorithm algorithm, int ignore_whitespace)
{
	const struct got_error *err = NULL;
	const struct diff_config *cfg;
	char *p1 = NULL, *p2 = NULL;
	int f1_created = 0, f2_created = 0;
	size_t size1, size2;
	struct diff_data d_left, d_right;
	struct diff_data *left, *right;
	struct diff_result *diff_result;

	if (diffreg_result) {
		*diffreg_result = calloc(1, sizeof(**diffreg_result));
		if (*diffreg_result == NULL)
			return got_error_from_errno("calloc");
		left = &(*diffreg_result)->left;
		right = &(*diffreg_result)->right;
	} else {
		memset(&d_left, 0, sizeof(d_left));
		memset(&d_right, 0, sizeof(d_right));
		left = &d_left;
		right = &d_right;
	}

	cfg = got_diff_get_config(algorithm);
	if (cfg == NULL) {
		err = got_error(GOT_ERR_NOT_IMPL);
		goto done;
	}

	if (f1 == NULL) {
		f1_created = 1;
		f1 = got_opentemp();
		if (f1 == NULL) {
			err = got_error_from_errno("got_opentemp");
			goto done;
		}
	}
	if (f2 == NULL) {
		f2_created = 1;
		f2 = got_opentemp();
		if (f2 == NULL) {
			err = got_error_from_errno("got_opentemp");
			goto done;
		}
	}

	err = got_diff_prepare_file(f1, &p1, &size1, left, cfg,
	    ignore_whitespace);
	if (err)
		goto done;

	err = got_diff_prepare_file(f2, &p2, &size2, right, cfg,
	    ignore_whitespace);
	if (err)
		goto done;

	diff_result = diff_main(cfg, left, right);
	if (diff_result == NULL) {
		err = got_error_set_errno(ENOMEM, "malloc");
		goto done;
	}
	if (diff_result->rc != DIFF_RC_OK) {
		err = got_error_set_errno(diff_result->rc, "diff");
		goto done;
	}

	if (diffreg_result) {
		(*diffreg_result)->result = diff_result;
		if (f1_created)
			(*diffreg_result)->f1 = f1;
		(*diffreg_result)->map1 = p1;
		(*diffreg_result)->size1 = size1;
		if (f2_created)
			(*diffreg_result)->f2 = f2;
		(*diffreg_result)->map2 = p2;
		(*diffreg_result)->size2 = size2;
	}
done:
	if (diffreg_result == NULL) {
		diff_data_free(left);
		diff_data_free(right);
	}
	if (err) {
		got_diffreg_close(f1_created ? f1 : NULL, p1, size1,
		    f2_created ? f2 : NULL, p2, size2);
		if (diffreg_result) {
			diff_data_free(left);
			diff_data_free(right);
			free(*diffreg_result);
			*diffreg_result = NULL;
		}
	}
	
	return err;
}

const struct got_error *
got_diffreg_output(off_t **line_offsets, size_t *nlines,
    struct got_diffreg_result *diff_result, FILE *f1, FILE *f2,
    const char *path1, const char *path2,
    enum got_diff_output_format output_format, int context_lines, FILE *outfile)
{
	struct diff_input_info info = {
		.left_path = path1,
		.right_path = path2,
	};
	int rc;
	struct diff_output_info *output_info;

	switch (output_format) {
	case GOT_DIFF_OUTPUT_UNIDIFF:
		rc = diff_output_unidiff(
		    line_offsets ? &output_info : NULL, outfile, &info,
		    diff_result->result, context_lines);
		if (rc != DIFF_RC_OK)
			return got_error_set_errno(rc, "diff_output_unidiff");
		break;
	case GOT_DIFF_OUTPUT_EDSCRIPT:
		rc = diff_output_edscript(line_offsets ? &output_info : NULL,
		    outfile, &info, diff_result->result);
		if (rc != DIFF_RC_OK)
			return got_error_set_errno(rc, "diff_output_edscript");
		break;

	}

	if (line_offsets && *line_offsets) {
		if (output_info->line_offsets.len > 0) {
			off_t prev_offset = 0, *p, *o;
			int i, len;
			if (*nlines > 0) {
				prev_offset = (*line_offsets)[*nlines - 1];
				/*
				 * First line offset is always zero. Skip it
				 * when appending to a pre-populated array.
				 */
				o = &output_info->line_offsets.head[1];
				len = output_info->line_offsets.len - 1;
			} else {
				o = &output_info->line_offsets.head[0];
				len = output_info->line_offsets.len;
			}
			p = reallocarray(*line_offsets, *nlines + len,
			    sizeof(off_t));
			if (p == NULL)
				return got_error_from_errno("calloc");
			for (i = 0; i < len; i++)
				p[*nlines + i] = o[i] + prev_offset;
			*line_offsets = p;
			*nlines += len;
		}
		diff_output_info_free(output_info);
	}

	return NULL;
}

const struct got_error *
got_diffreg_result_free(struct got_diffreg_result *diffreg_result)
{
	const struct got_error *err;

	diff_result_free(diffreg_result->result);
	diff_data_free(&diffreg_result->left);
	diff_data_free(&diffreg_result->right);
	err = got_diffreg_close(diffreg_result->f1, diffreg_result->map1,
	    diffreg_result->size1, diffreg_result->f2,
	    diffreg_result->map2, diffreg_result->size2);
	free(diffreg_result);
	return err;
}

const struct got_error *
got_diffreg_result_free_left(struct got_diffreg_result *diffreg_result)
{
	diff_data_free(&diffreg_result->left);
	memset(&diffreg_result->left, 0, sizeof(diffreg_result->left));
	return got_diffreg_close(diffreg_result->f1, diffreg_result->map1,
	    diffreg_result->size1, NULL, NULL, 0);
}

const struct got_error *
got_diffreg_result_free_right(struct got_diffreg_result *diffreg_result)
{
	diff_data_free(&diffreg_result->right);
	memset(&diffreg_result->right, 0, sizeof(diffreg_result->right));
	return got_diffreg_close(NULL, NULL, 0, diffreg_result->f2,
	    diffreg_result->map2, diffreg_result->size2);
}
