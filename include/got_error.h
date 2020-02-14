/*
 * Copyright (c) 2018, 2019, 2020 Stefan Sperling <stsp@openbsd.org>
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

/* Error codes */
#define GOT_ERR_OK		0
#define GOT_ERR_ERRNO		1
#define GOT_ERR_NOT_GIT_REPO	2
#define GOT_ERR_NOT_ABSPATH	3
#define GOT_ERR_BAD_PATH	4
#define GOT_ERR_NOT_REF		5
#define GOT_ERR_IO		6
#define GOT_ERR_EOF		7
#define GOT_ERR_DECOMPRESSION	8
#define GOT_ERR_NO_SPACE	9
#define GOT_ERR_BAD_OBJ_HDR	10
#define GOT_ERR_OBJ_TYPE	11
#define GOT_ERR_BAD_OBJ_DATA	12
#define GOT_ERR_AMBIGUOUS_ID	13
#define GOT_ERR_BAD_PACKIDX	14
#define GOT_ERR_PACKIDX_CSUM	15
#define GOT_ERR_BAD_PACKFILE	16
#define GOT_ERR_NO_OBJ		17
#define GOT_ERR_NOT_IMPL	18
#define GOT_ERR_OBJ_NOT_PACKED	19
#define GOT_ERR_BAD_DELTA_CHAIN	20
#define GOT_ERR_BAD_DELTA	21
#define GOT_ERR_COMPRESSION	22
#define GOT_ERR_BAD_OBJ_ID_STR	23
#define GOT_ERR_WORKTREE_EXISTS	26
#define GOT_ERR_WORKTREE_META	27
#define GOT_ERR_WORKTREE_VERS	28
#define GOT_ERR_WORKTREE_BUSY	29
#define GOT_ERR_DIR_OBSTRUCTED	30
#define GOT_ERR_FILE_OBSTRUCTED	31
#define GOT_ERR_RECURSION	32
#define GOT_ERR_TIMEOUT		33
#define GOT_ERR_INTERRUPT	34
#define GOT_ERR_PRIVSEP_READ	35
#define GOT_ERR_PRIVSEP_LEN	36
#define GOT_ERR_PRIVSEP_PIPE	37
#define GOT_ERR_PRIVSEP_NO_FD	38
#define GOT_ERR_PRIVSEP_MSG	39
#define GOT_ERR_PRIVSEP_DIED	40
#define GOT_ERR_PRIVSEP_EXIT	41
#define GOT_ERR_PACK_OFFSET	42
#define GOT_ERR_OBJ_EXISTS	43
#define GOT_ERR_BAD_OBJ_ID	44
#define GOT_ERR_ITER_BUSY	45
#define GOT_ERR_ITER_COMPLETED	46
#define GOT_ERR_RANGE		47
#define GOT_ERR_EXPECTED	48 /* for use in regress tests only */
#define GOT_ERR_CANCELLED	49
#define GOT_ERR_NO_TREE_ENTRY	50
#define GOT_ERR_FILEIDX_SIG	51
#define GOT_ERR_FILEIDX_VER	52
#define GOT_ERR_FILEIDX_CSUM	53
#define GOT_ERR_PATH_PREFIX	54
#define GOT_ERR_ANCESTRY	55
#define GOT_ERR_FILEIDX_BAD	56
#define GOT_ERR_BAD_REF_DATA	57
#define GOT_ERR_TREE_DUP_ENTRY	58
#define GOT_ERR_DIR_DUP_ENTRY	59
#define GOT_ERR_NOT_WORKTREE	60
#define GOT_ERR_UUID_VERSION	61
#define GOT_ERR_UUID_INVALID	62
#define GOT_ERR_UUID		63
#define GOT_ERR_LOCKFILE_TIMEOUT 64
#define GOT_ERR_BAD_REF_NAME	65
#define GOT_ERR_WORKTREE_REPO	66
#define GOT_ERR_FILE_MODIFIED	67
#define GOT_ERR_FILE_STATUS	68
#define GOT_ERR_COMMIT_CONFLICT	69
#define GOT_ERR_BAD_REF_TYPE	70
#define GOT_ERR_COMMIT_NO_AUTHOR 71
#define GOT_ERR_COMMIT_HEAD_CHANGED 72
#define GOT_ERR_COMMIT_OUT_OF_DATE 73
#define GOT_ERR_COMMIT_MSG_EMPTY 74
#define GOT_ERR_DIR_NOT_EMPTY	75
#define GOT_ERR_COMMIT_NO_CHANGES 76
#define GOT_ERR_BRANCH_MOVED	77
#define GOT_ERR_OBJ_TOO_LARGE	78
#define GOT_ERR_SAME_BRANCH	79
#define GOT_ERR_ROOT_COMMIT	80
#define GOT_ERR_MIXED_COMMITS	81
#define GOT_ERR_CONFLICTS	82
#define GOT_ERR_BRANCH_EXISTS	83
#define GOT_ERR_MODIFIED	84
#define GOT_ERR_NOT_REBASING	85
#define GOT_ERR_EMPTY_REBASE	86
#define GOT_ERR_REBASE_COMMITID	87
#define GOT_ERR_REBASING	88
#define GOT_ERR_REBASE_PATH	89
#define GOT_ERR_NOT_HISTEDIT	90
#define GOT_ERR_EMPTY_HISTEDIT	91
#define GOT_ERR_NO_HISTEDIT_CMD	92
#define GOT_ERR_HISTEDIT_SYNTAX	93
#define GOT_ERR_HISTEDIT_CANCEL	94
#define GOT_ERR_HISTEDIT_COMMITID 95
#define GOT_ERR_HISTEDIT_BUSY	96
#define GOT_ERR_HISTEDIT_CMD	97
#define GOT_ERR_HISTEDIT_PATH	98
#define GOT_ERR_NO_MERGED_PATHS 99
#define GOT_ERR_COMMIT_BRANCH	100
#define GOT_ERR_FILE_STAGED	101
#define GOT_ERR_STAGE_NO_CHANGE	102
#define GOT_ERR_STAGE_CONFLICT	103
#define GOT_ERR_STAGE_OUT_OF_DATE 104
#define GOT_ERR_FILE_NOT_STAGED 105
#define GOT_ERR_STAGED_PATHS	106
#define GOT_ERR_PATCH_CHOICE	107
#define GOT_ERR_COMMIT_NO_EMAIL	108
#define GOT_ERR_TAG_EXISTS	109
#define GOT_ERR_GIT_REPO_FORMAT	110
#define GOT_ERR_REBASE_REQUIRED	111
#define GOT_ERR_REGEX		112
#define GOT_ERR_REF_NAME_MINUS	113
#define GOT_ERR_GITCONFIG_SYNTAX 114
#define GOT_ERR_REBASE_OUT_OF_DATE 115
#define GOT_ERR_CACHE_DUP_ENTRY	116
#define GOT_ERR_QUERYSTRING	117

static const struct got_error {
	int code;
	const char *msg;
} got_errors[] = {
	{ GOT_ERR_OK,		"no error occured?!?" },
	{ GOT_ERR_ERRNO,	"see errno" },
	{ GOT_ERR_NOT_GIT_REPO, "no git repository found" },
	{ GOT_ERR_NOT_ABSPATH,	"absolute path expected" },
	{ GOT_ERR_BAD_PATH,	"bad path" },
	{ GOT_ERR_NOT_REF,	"no such reference found" },
	{ GOT_ERR_IO,		"input/output error" },
	{ GOT_ERR_EOF,		"unexpected end of file" },
	{ GOT_ERR_DECOMPRESSION,"decompression failed" },
	{ GOT_ERR_NO_SPACE,	"buffer too small" },
	{ GOT_ERR_BAD_OBJ_HDR,	"bad object header" },
	{ GOT_ERR_OBJ_TYPE,	"wrong type of object" },
	{ GOT_ERR_BAD_OBJ_DATA,	"bad object data" },
	{ GOT_ERR_AMBIGUOUS_ID, "ambiguous object ID" },
	{ GOT_ERR_BAD_PACKIDX,	"bad pack index file" },
	{ GOT_ERR_PACKIDX_CSUM, "pack index file checksum error" },
	{ GOT_ERR_BAD_PACKFILE,	"bad pack file" },
	{ GOT_ERR_NO_OBJ,	"object not found" },
	{ GOT_ERR_NOT_IMPL,	"feature not implemented" },
	{ GOT_ERR_OBJ_NOT_PACKED,"object is not packed" },
	{ GOT_ERR_BAD_DELTA_CHAIN,"bad delta chain" },
	{ GOT_ERR_BAD_DELTA,	"bad delta" },
	{ GOT_ERR_COMPRESSION,	"compression failed" },
	{ GOT_ERR_BAD_OBJ_ID_STR,"bad object id string" },
	{ GOT_ERR_WORKTREE_EXISTS,"worktree already exists" },
	{ GOT_ERR_WORKTREE_META,"bad worktree meta data" },
	{ GOT_ERR_WORKTREE_VERS,"unsupported worktree format version" },
	{ GOT_ERR_WORKTREE_BUSY,"worktree already locked" },
	{ GOT_ERR_FILE_OBSTRUCTED,"file is obstructed" },
	{ GOT_ERR_RECURSION,	"recursion limit reached" },
	{ GOT_ERR_TIMEOUT,	"operation timed out" },
	{ GOT_ERR_INTERRUPT,	"operation interrupted" },
	{ GOT_ERR_PRIVSEP_READ,	"no data received in imsg" },
	{ GOT_ERR_PRIVSEP_LEN,	"unexpected amount of data received in imsg" },
	{ GOT_ERR_PRIVSEP_PIPE,	"privsep peer process closed pipe" },
	{ GOT_ERR_PRIVSEP_NO_FD,"privsep file descriptor unavailable" },
	{ GOT_ERR_PRIVSEP_MSG,	"received unexpected privsep message" },
	{ GOT_ERR_PRIVSEP_DIED,	"unprivileged process died unexpectedly" },
	{ GOT_ERR_PRIVSEP_EXIT,	"bad exit code from unprivileged process" },
	{ GOT_ERR_PACK_OFFSET,	"bad offset in pack file" },
	{ GOT_ERR_OBJ_EXISTS,	"object already exists" },
	{ GOT_ERR_BAD_OBJ_ID,	"bad object id" },
	{ GOT_ERR_ITER_BUSY,	"iteration already in progress" },
	{ GOT_ERR_ITER_COMPLETED,"iteration completed" },
	{ GOT_ERR_RANGE,	"value out of range" },
	{ GOT_ERR_EXPECTED,	"expected an error but have no error" },
	{ GOT_ERR_CANCELLED,	"operation in progress has been cancelled" },
	{ GOT_ERR_NO_TREE_ENTRY,"no such entry found in tree" },
	{ GOT_ERR_FILEIDX_SIG,	"bad file index signature" },
	{ GOT_ERR_FILEIDX_VER,	"unknown file index format version" },
	{ GOT_ERR_FILEIDX_CSUM,	"bad file index checksum" },
	{ GOT_ERR_PATH_PREFIX,	"worktree already contains items from a "
				"different path prefix" },
	{ GOT_ERR_ANCESTRY,	"target commit is on a different branch" },
	{ GOT_ERR_FILEIDX_BAD,	"file index is corrupt" },
	{ GOT_ERR_BAD_REF_DATA,	"could not parse reference data" },
	{ GOT_ERR_TREE_DUP_ENTRY,"duplicate entry in tree object" },
	{ GOT_ERR_DIR_DUP_ENTRY,"duplicate entry in directory" },
	{ GOT_ERR_NOT_WORKTREE, "no got work tree found" },
	{ GOT_ERR_UUID_VERSION, "bad uuid version" },
	{ GOT_ERR_UUID_INVALID, "uuid invalid" },
	{ GOT_ERR_UUID,		"uuid error" },
	{ GOT_ERR_LOCKFILE_TIMEOUT,"lockfile timeout" },
	{ GOT_ERR_BAD_REF_NAME,	"bad reference name" },
	{ GOT_ERR_WORKTREE_REPO,"cannot create worktree inside a git repository" },
	{ GOT_ERR_FILE_MODIFIED,"file contains modifications" },
	{ GOT_ERR_FILE_STATUS,	"file has unexpected status" },
	{ GOT_ERR_COMMIT_CONFLICT,"cannot commit file in conflicted status" },
	{ GOT_ERR_BAD_REF_TYPE,	"bad reference type" },
	{ GOT_ERR_COMMIT_NO_AUTHOR,"GOT_AUTHOR environment variable is not set" },
	{ GOT_ERR_COMMIT_HEAD_CHANGED, "branch head in repository has changed "
	    "while commit was in progress" },
	{ GOT_ERR_COMMIT_OUT_OF_DATE, "work tree must be updated before these "
	    "changes can be committed" },
	{ GOT_ERR_COMMIT_MSG_EMPTY, "commit message cannot be empty" },
	{ GOT_ERR_DIR_NOT_EMPTY, "directory exists and is not empty" },
	{ GOT_ERR_COMMIT_NO_CHANGES, "no changes to commit" },
	{ GOT_ERR_BRANCH_MOVED,	"work tree's head reference now points to a "
	    "different branch; new head reference and/or update -b required" },
	{ GOT_ERR_OBJ_TOO_LARGE,	"object too large" },
	{ GOT_ERR_SAME_BRANCH,	"commit is already contained in this branch" },
	{ GOT_ERR_ROOT_COMMIT,	"specified commit has no parent commit" },
	{ GOT_ERR_MIXED_COMMITS,"work tree contains files from multiple "
	    "base commits; the entire work tree must be updated first" },
	{ GOT_ERR_CONFLICTS,	"work tree contains conflicted files; these "
	    "conflicts must be resolved first" },
	{ GOT_ERR_BRANCH_EXISTS,"specified branch already exists" },
	{ GOT_ERR_MODIFIED,	"work tree contains local changes; these "
	    "changes must be committed or reverted first" },
	{ GOT_ERR_NOT_REBASING,	"rebase operation not in progress" },
	{ GOT_ERR_EMPTY_REBASE,	"no commits to rebase" },
	{ GOT_ERR_REBASE_COMMITID,"rebase commit ID mismatch" },
	{ GOT_ERR_REBASING,	"a rebase operation is in progress in this "
	    "work tree and must be continued or aborted first" },
	{ GOT_ERR_REBASE_PATH,	"cannot rebase branch which contains "
	    "changes outside of this work tree's path prefix" },
	{ GOT_ERR_NOT_HISTEDIT,	"histedit operation not in progress" },
	{ GOT_ERR_EMPTY_HISTEDIT,"no commits to edit; perhaps the work tree "
	    "must be updated to an older commit first" },
	{ GOT_ERR_NO_HISTEDIT_CMD,"no histedit commands provided" },
	{ GOT_ERR_HISTEDIT_SYNTAX,"syntax error in histedit command list" },
	{ GOT_ERR_HISTEDIT_CANCEL,"histedit operation cancelled" },
	{ GOT_ERR_HISTEDIT_COMMITID,"histedit commit ID mismatch" },
	{ GOT_ERR_HISTEDIT_BUSY,"histedit operation is in progress in this "
	    "work tree and must be continued or aborted first" },
	{ GOT_ERR_HISTEDIT_CMD, "bad histedit command" },
	{ GOT_ERR_HISTEDIT_PATH, "cannot edit branch history which contains "
	    "changes outside of this work tree's path prefix" },
	{ GOT_ERR_NO_MERGED_PATHS, "empty list of merged paths" },
	{ GOT_ERR_COMMIT_BRANCH, "will not commit to a branch outside the "
	    "\"refs/heads/\" reference namespace" },
	{ GOT_ERR_FILE_STAGED, "file is staged" },
	{ GOT_ERR_STAGE_NO_CHANGE, "no changes to stage" },
	{ GOT_ERR_STAGE_CONFLICT, "cannot stage file in conflicted status" },
	{ GOT_ERR_STAGE_OUT_OF_DATE, "work tree must be updated before "
	    "changes can be staged" },
	{ GOT_ERR_FILE_NOT_STAGED, "file is not staged" },
	{ GOT_ERR_STAGED_PATHS, "work tree contains files with staged "
	    "changes; these changes must be committed or unstaged first" },
	{ GOT_ERR_PATCH_CHOICE, "invalid patch choice" },
	{ GOT_ERR_COMMIT_NO_EMAIL,"GOT_AUTHOR environment variable contains "
	    "no email address; an email address is required for compatibility "
	    "with Git" },
	{ GOT_ERR_TAG_EXISTS,"specified tag already exists" },
	{ GOT_ERR_GIT_REPO_FORMAT,"unknown git repository format version" },
	{ GOT_ERR_REBASE_REQUIRED,"specified branch must be rebased first" },
	{ GOT_ERR_REGEX, "regular expression error" },
	{ GOT_ERR_REF_NAME_MINUS, "reference name may not start with '-'" },
	{ GOT_ERR_GITCONFIG_SYNTAX, "gitconfig syntax error" },
	{ GOT_ERR_REBASE_OUT_OF_DATE, "work tree must be updated before it "
	    "can be used to rebase a branch" },
	{ GOT_ERR_CACHE_DUP_ENTRY, "duplicate cache entry" },
	{ GOT_ERR_QUERYSTRING, "bad querystring" },
};

/*
 * Get an error object from the above list, for a given error code.
 * The error message is fixed.
 */
const struct got_error *got_error(int);

/*
 * Get an error object from the above list, for a given error code.
 * Use the specified error message instead of the default one.
 * Caution: If the message buffer lives in dynamically allocated memory,
 * then this memory likely won't be freed.
 */
const struct got_error *got_error_msg(int, const char *);

/*
 * Get a statically allocated error object with code GOT_ERR_ERRNO
 * and an error message obtained from strerror(3), prefixed with a
 * string.
 */
const struct got_error *got_error_from_errno(const char *);

/*
 * Get a statically allocated error object with code GOT_ERR_ERRNO
 * and an error message obtained from strerror(3), prefixed with two
 * strings.
 */
const struct got_error *got_error_from_errno2(const char *, const char *);

/*
 * Get a statically allocated error object with code GOT_ERR_ERRNO
 * and an error message obtained from strerror(3), prefixed with three
 * strings.
 */
const struct got_error *got_error_from_errno3(const char *, const char *,
    const char *);

/*
 * Set errno to the specified error code and return a statically
 * allocated error object with code GOT_ERR_ERRNO and an error
 * message obtained from strerror(3), optionally prefixed with a
 * string.
 */
const struct got_error *got_error_set_errno(int, const char *);

/*
 * If ferror(3) indicates an error status for the FILE, obtain an error
 * from got_error_from_errno(). Else, obtain the error via got_error()
 * with the error code provided in the second argument.
 */
const struct got_error *got_ferror(FILE *, int);

/*
 * Obtain an error with code GOT_ERR_NO_OBJ and an error message which
 * contains the specified object ID. The message buffer is statically
 * allocated; future invocations of this function will overwrite the
 * message set during earlier invocations.
 */
struct got_object_id; /* forward declaration */
const struct got_error *got_error_no_obj(struct got_object_id *);

/*
 * Obtain an error with code GOT_ERR_NOT_REF and an error message which
 * contains the specified reference name. The message buffer is statically
 * allocated; future invocations of this function will overwrite the
 * message set during earlier invocations.
 */
const struct got_error *got_error_not_ref(const char *);

/* Return an error based on a uuid(3) status code. */
const struct got_error *got_error_uuid(uint32_t, const char *);

/* Return an error with a path prefixed to the error message. */
const struct got_error *got_error_path(const char *, int);
