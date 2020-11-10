/* Generic infrastructure to implement various diff algorithms. */
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

#ifndef MAX
#define MAX(A,B) ((A)>(B)?(A):(B))
#endif
#ifndef MIN
#define MIN(A,B) ((A)<(B)?(A):(B))
#endif

static inline bool
diff_range_empty(const struct diff_range *r)
{
	return r->start == r->end;
}

static inline bool
diff_ranges_touch(const struct diff_range *a, const struct diff_range *b)
{
	return (a->end >= b->start) && (a->start <= b->end);
}

static inline void
diff_ranges_merge(struct diff_range *a, const struct diff_range *b)
{
	*a = (struct diff_range){
		.start = MIN(a->start, b->start),
		.end = MAX(a->end, b->end),
	};
}

static inline int
diff_range_len(const struct diff_range *r)
{
	if (!r)
		return 0;
	return r->end - r->start;
}

/* List of all possible return codes of a diff invocation. */
#define DIFF_RC_USE_DIFF_ALGO_FALLBACK	-1
#define DIFF_RC_OK			0
/* Any positive return values are errno values from sys/errno.h */

struct diff_data;

struct diff_atom {
	struct diff_data *root; /* back pointer to root diff data */

	off_t pos;		/* if not memory-mapped */
	const uint8_t *at;	/* if memory-mapped */
	off_t len;

	/* This hash is just a very cheap speed up for finding *mismatching*
	 * atoms. When hashes match, we still need to compare entire atoms to
	 * find out whether they are indeed identical or not. */
	unsigned int hash;
};

int
diff_atom_cmp(int *cmp,
	      const struct diff_atom *left,
	      const struct diff_atom *right);

/* Indicate whether two given diff atoms match. */
int
diff_atom_same(bool *same,
	       const struct diff_atom *left,
	       const struct diff_atom *right);

/* The atom's index in the entire file. For atoms divided by lines of text, this
 * yields the line number (starting with 0). Also works for diff_data that
 * reference only a subsection of a file, always reflecting the global position
 * in the file (and not the relative position within the subsection). */
#define diff_atom_root_idx(DIFF_DATA, ATOM) \
	((ATOM) && ((ATOM) >= (DIFF_DATA)->root->atoms.head) \
	 ? (unsigned int)((ATOM) - ((DIFF_DATA)->root->atoms.head)) \
	 : (DIFF_DATA)->root->atoms.len)

/* The atom's index within DIFF_DATA. For atoms divided by lines of text, this
 * yields the line number (starting with 0). */
#define diff_atom_idx(DIFF_DATA, ATOM) \
	((ATOM) && ((ATOM) >= (DIFF_DATA)->atoms.head) \
	 ? (unsigned int)((ATOM) - ((DIFF_DATA)->atoms.head)) \
	 : (DIFF_DATA)->atoms.len)

#define foreach_diff_atom(ATOM, FIRST_ATOM, COUNT) \
	for ((ATOM) = (FIRST_ATOM); \
	     (ATOM) \
	     && ((ATOM) >= (FIRST_ATOM)) \
	     && ((ATOM) - (FIRST_ATOM) < (COUNT)); \
	     (ATOM)++)

#define diff_data_foreach_atom(ATOM, DIFF_DATA) \
	foreach_diff_atom(ATOM, (DIFF_DATA)->atoms.head, (DIFF_DATA)->atoms.len)

#define diff_data_foreach_atom_from(FROM, ATOM, DIFF_DATA) \
	for ((ATOM) = (FROM); \
	     (ATOM) \
	     && ((ATOM) >= (DIFF_DATA)->atoms.head) \
	     && ((ATOM) - (DIFF_DATA)->atoms.head < (DIFF_DATA)->atoms.len); \
	     (ATOM)++)

#define diff_data_foreach_atom_backwards_from(FROM, ATOM, DIFF_DATA) \
	for ((ATOM) = (FROM); \
	     (ATOM) \
	     && ((ATOM) >= (DIFF_DATA)->atoms.head) \
	     && ((ATOM) - (DIFF_DATA)->atoms.head >= 0); \
	     (ATOM)--)

/* A diff chunk represents a set of atoms on the left and/or a set of atoms on
 * the right.
 *
 * If solved == false:
 * The diff algorithm has divided the source file, and this is a chunk that the
 * inner_algo should run on next.
 * The lines on the left should be diffed against the lines on the right.
 * (If there are no left lines or no right lines, it implies solved == true,
 * because there is nothing to diff.)
 *
 * If solved == true:
 * If there are only left atoms, it is a chunk removing atoms from the left ("a
 * minus chunk").
 * If there are only right atoms, it is a chunk adding atoms from the right ("a
 * plus chunk").
 * If there are both left and right lines, it is a chunk of equal content on
 * both sides, and left_count == right_count:
 *
 * - foo  }
 * - bar  }-- diff_chunk{ left_start = &left.atoms.head[0], left_count = 3,
 * - baz  }               right_start = NULL, right_count = 0 }
 *   moo    }
 *   goo    }-- diff_chunk{ left_start = &left.atoms.head[3], left_count = 3,
 *   zoo    }              right_start = &right.atoms.head[0], right_count = 3 }
 *  +loo      }
 *  +roo      }-- diff_chunk{ left_start = NULL, left_count = 0,
 *  +too      }            right_start = &right.atoms.head[3], right_count = 3 }
 *
 */
struct diff_chunk {
	bool solved;
	struct diff_atom *left_start;
	unsigned int left_count;
	struct diff_atom *right_start;
	unsigned int right_count;
};

#define DIFF_RESULT_ALLOC_BLOCKSIZE 128

enum diff_chunk_type {
	CHUNK_EMPTY,
	CHUNK_PLUS,
	CHUNK_MINUS,
	CHUNK_SAME,
	CHUNK_ERROR,
};

static inline enum diff_chunk_type
diff_chunk_type(const struct diff_chunk *chunk)
{
	if (!chunk->left_count && !chunk->right_count)
		return CHUNK_EMPTY;
	if (!chunk->solved)
		return CHUNK_ERROR;
	if (!chunk->right_count)
		return CHUNK_MINUS;
	if (!chunk->left_count)
		return CHUNK_PLUS;
	if (chunk->left_count != chunk->right_count)
		return CHUNK_ERROR;
	return CHUNK_SAME;
}

struct diff_chunk_context;

bool
diff_chunk_context_empty(const struct diff_chunk_context *cc);

bool
diff_chunk_contexts_touch(const struct diff_chunk_context *cc,
			  const struct diff_chunk_context *other);

void
diff_chunk_contexts_merge(struct diff_chunk_context *cc,
			  const struct diff_chunk_context *other);

struct diff_state {
	/* The final result passed to the original diff caller. */
	struct diff_result *result;

	/* The root diff_data is in result->left,right, these are (possibly)
	 * subsections of the root data. */
	struct diff_data left;
	struct diff_data right;

	unsigned int recursion_depth_left;

	/* Remaining chunks from one diff algorithm pass, if any solved == false
	 * chunks came up. */
	diff_chunk_arraylist_t temp_result;

 	/* State buffer used by Myers algorithm. */
	int *kd_buf;
	size_t kd_buf_size; /* in units of sizeof(int), not bytes */
};

struct diff_chunk *diff_state_add_chunk(struct diff_state *state, bool solved,
					struct diff_atom *left_start,
					unsigned int left_count,
					struct diff_atom *right_start,
					unsigned int right_count);

struct diff_output_info;

int diff_output_lines(struct diff_output_info *output_info, FILE *dest,
		       const char *prefix, struct diff_atom *start_atom,
		       unsigned int count);

int diff_output_trailing_newline_msg(struct diff_output_info *outinfo,
				     FILE *dest,
				     const struct diff_chunk *c);
int diff_output_match_function_prototype(char **prototype,
					 const struct diff_result *result,
					 const struct diff_chunk_context *cc);

struct diff_output_info *diff_output_info_alloc(void);

void
diff_data_init_subsection(struct diff_data *d, struct diff_data *parent,
			  struct diff_atom *from_atom, unsigned int atoms_count);
