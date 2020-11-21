/* Diff output generators and invocation shims. */
/*
 * Copyright (c) 2020 Neels Hofmeyr <neels@hofmeyr.de>
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

struct diff_input_info {
	const char *left_path;
	const char *right_path;

	/* Set by caller of diff_output_* functions. */
	int flags;
#define DIFF_INPUT_LEFT_NONEXISTENT	0x00000001
#define DIFF_INPUT_RIGHT_NONEXISTENT	0x00000002
};

struct diff_output_info {
	/*
	 * Byte offset to each line in the generated output file.
	 * The total number of lines in the file is line_offsets.len - 1.
	 * The last offset in this array corresponds to end-of-file.
	 */
	ARRAYLIST(off_t) line_offsets;
};

void diff_output_info_free(struct diff_output_info *output_info);

struct diff_chunk_context {
	struct diff_range chunk;
	struct diff_range left, right;
};

int diff_output_plain(struct diff_output_info **output_info, FILE *dest,
			const struct diff_input_info *info,
			const struct diff_result *result);
int diff_output_unidiff(struct diff_output_info **output_info,
			FILE *dest, const struct diff_input_info *info,
			const struct diff_result *result,
			unsigned int context_lines);
int diff_output_edscript(struct diff_output_info **output_info,
			 FILE *dest, const struct diff_input_info *info,
			 const struct diff_result *result);
int diff_chunk_get_left_start(const struct diff_chunk *c,
			      const struct diff_result *r,
			      int context_lines);
int diff_chunk_get_left_end(const struct diff_chunk *c,
			    const struct diff_result *r,
			    int context_lines);
int diff_chunk_get_right_start(const struct diff_chunk *c,
			       const struct diff_result *r,
			       int context_lines);
int diff_chunk_get_right_end(const struct diff_chunk *c,
			     const struct diff_result *r,
			     int context_lines);
struct diff_chunk *diff_chunk_get(const struct diff_result *r, int chunk_idx);
int diff_chunk_get_left_count(struct diff_chunk *c);
int diff_chunk_get_right_count(struct diff_chunk *c);
void diff_chunk_context_get(struct diff_chunk_context *cc,
				 const struct diff_result *r,
				 int chunk_idx, int context_lines);
void diff_chunk_context_load_change(struct diff_chunk_context *cc,
				    int *nchunks_used,
				    struct diff_result *result,
				    int start_chunk_idx,
				    int context_lines);

struct diff_output_unidiff_state;
struct diff_output_unidiff_state *diff_output_unidiff_state_alloc(void);
void diff_output_unidiff_state_reset(struct diff_output_unidiff_state *state);
void diff_output_unidiff_state_free(struct diff_output_unidiff_state *state);
int diff_output_unidiff_chunk(struct diff_output_info **output_info, FILE *dest,
			  struct diff_output_unidiff_state *state,
			  const struct diff_input_info *info,
			  const struct diff_result *result,
			  const struct diff_chunk_context *cc);
int diff_output_chunk_left_version(struct diff_output_info **output_info,
			       FILE *dest,
			       const struct diff_input_info *info,
			       const struct diff_result *result,
			       const struct diff_chunk_context *cc);
int diff_output_chunk_right_version(struct diff_output_info **output_info,
				FILE *dest,
				const struct diff_input_info *info,
				const struct diff_result *result,
				const struct diff_chunk_context *cc);

const char *diff_output_get_label_left(const struct diff_input_info *info);
const char *diff_output_get_label_right(const struct diff_input_info *info);
