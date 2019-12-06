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

struct got_worktree;
struct got_commitable;
struct got_commit_object;
struct got_fileindex;

/* status codes */
#define GOT_STATUS_NO_CHANGE	' '
#define GOT_STATUS_ADD		'A'
#define GOT_STATUS_EXISTS	'E'
#define GOT_STATUS_UPDATE	'U'
#define GOT_STATUS_DELETE	'D'
#define GOT_STATUS_MODIFY	'M'
#define GOT_STATUS_MODE_CHANGE	'm'
#define GOT_STATUS_CONFLICT	'C'
#define GOT_STATUS_MERGE	'G'
#define GOT_STATUS_MISSING	'!'
#define GOT_STATUS_UNVERSIONED	'?'
#define GOT_STATUS_OBSTRUCTED	'~'
#define GOT_STATUS_NONEXISTENT	'N'
#define GOT_STATUS_REVERT	'R'
#define GOT_STATUS_CANNOT_DELETE 'd'
#define GOT_STATUS_BUMP_BASE	'b'

/*
 * Attempt to initialize a new work tree on disk.
 * The first argument is the path to a directory where the work tree
 * will be created. The path itself must not yet exist, but the dirname(3)
 * of the path must already exist.
 * The reference provided will be used to determine the new worktree's
 * base commit. The third argument speficies the work tree's path prefix.
 */
const struct got_error *got_worktree_init(const char *, struct got_reference *,
    const char *, struct got_repository *);

/*
 * Attempt to open a worktree at or above the specified path.
 * The caller must dispose of it with got_worktree_close().
 */
const struct got_error *got_worktree_open(struct got_worktree **, const char *);

/* Dispose of an open work tree. */
const struct got_error *got_worktree_close(struct got_worktree *);

/*
 * Get the path to the root directory of a worktree.
 */
const char *got_worktree_get_root_path(struct got_worktree *);

/*
 * Get the path to the repository associated with a worktree.
 */
const char *got_worktree_get_repo_path(struct got_worktree *);

/*
 * Get the path prefix associated with a worktree.
 */
const char *got_worktree_get_path_prefix(struct got_worktree *);

/*
 * Check if a user-provided path prefix matches that of the worktree.
 */
const struct got_error *got_worktree_match_path_prefix(int *,
    struct got_worktree *, const char *);

/*
 * Get the name of a work tree's HEAD reference.
 */
const char *got_worktree_get_head_ref_name(struct got_worktree *);

/*
 * Set the branch head reference of the work tree.
 */
const struct got_error *got_worktree_set_head_ref(struct got_worktree *,
    struct got_reference *);

/*
 * Get the current base commit ID of a worktree.
 */
struct got_object_id *got_worktree_get_base_commit_id(struct got_worktree *);

/*
 * Set the base commit Id of a worktree.
 */
const struct got_error *got_worktree_set_base_commit_id(struct got_worktree *,
    struct got_repository *, struct got_object_id *);

/* A callback function which is invoked when a path is checked out. */
typedef const struct got_error *(*got_worktree_checkout_cb)(void *,
    unsigned char, const char *);

/*
 * Attempt to check out files into a work tree from its associated repository
 * and path prefix, and update the work tree's file index accordingly.
 * File content is obtained from blobs within the work tree's path prefix
 * inside the tree corresponding to the work tree's base commit.
 * The checkout progress callback will be invoked with the provided
 * void * argument, and the path of each checked out file.
 *
 * It is possible to restrict the checkout operation to specific paths in
 * the work tree, in which case all files outside those paths will remain at
 * their currently recorded base commit. Inconsistent base commits can be
 * repaired later by running another update operation across the entire work
 * tree. Inconsistent base-commits may also occur if this function runs into
 * an error or if the checkout operation is cancelled by the cancel callback.
 * Allspecified paths are relative to the work tree's root. Pass a pathlist
 * with a single empty path "" to check out files across the entire work tree.
 *
 * Some operations may refuse to run while the work tree contains files from
 * multiple base commits.
 */
const struct got_error *got_worktree_checkout_files(struct got_worktree *,
    struct got_pathlist_head *, struct got_repository *,
    got_worktree_checkout_cb, void *, got_cancel_cb, void *);

/* Merge the differences between two commits into a work tree. */
const struct got_error *
got_worktree_merge_files(struct got_worktree *,
    struct got_object_id *, struct got_object_id *,
    struct got_repository *, got_worktree_checkout_cb, void *,
    got_cancel_cb, void *);

/* A callback function which is invoked to report a path's status. */
typedef const struct got_error *(*got_worktree_status_cb)(void *,
    unsigned char, unsigned char, const char *, struct got_object_id *,
    struct got_object_id *, struct got_object_id *);

/*
 * Report the status of paths in the work tree.
 * The status callback will be invoked with the provided void * argument,
 * a path, and a corresponding status code.
 */
const struct got_error *got_worktree_status(struct got_worktree *,
    struct got_pathlist_head *, struct got_repository *,
    got_worktree_status_cb, void *, got_cancel_cb cancel_cb, void *);

/*
 * Try to resolve a user-provided path to an on-disk path in the work tree.
 * The caller must dispose of the resolved path with free(3).
 */
const struct got_error *got_worktree_resolve_path(char **,
    struct got_worktree *, const char *);

/* Schedule files at on-disk paths for addition in the next commit. */
const struct got_error *got_worktree_schedule_add(struct got_worktree *,
    struct got_pathlist_head *, got_worktree_checkout_cb, void *,
    struct got_repository *, int);

/*
 * Remove files from disk and schedule them to be deleted in the next commit.
 * Don't allow deleting files with uncommitted modifications, unless the
 * parameter 'delete_local_mods' is set.
 */
const struct got_error *
got_worktree_schedule_delete(struct got_worktree *,
    struct got_pathlist_head *, int, got_worktree_status_cb, void *,
    struct got_repository *);

/* A callback function which is used to select or reject a patch. */
typedef const struct got_error *(*got_worktree_patch_cb)(int *, void *,
    unsigned char, const char *, FILE *, int, int);

/* Values for result output parameter of got_wortree_patch_cb. */
#define GOT_PATCH_CHOICE_NONE	0
#define GOT_PATCH_CHOICE_YES	1
#define GOT_PATCH_CHOICE_NO	2
#define GOT_PATCH_CHOICE_QUIT	3

/*
 * Revert a file at the specified path such that it matches its
 * original state in the worktree's base commit.
 * If the patch callback is not NULL, call it to select patch hunks to
 * revert. Otherwise, revert the whole file found at each path.
 */
const struct got_error *got_worktree_revert(struct got_worktree *,
    struct got_pathlist_head *, got_worktree_checkout_cb, void *,
    got_worktree_patch_cb patch_cb, void *patch_arg,
    struct got_repository *);

/*
 * A callback function which is invoked when a commit message is requested.
 * Passes a pathlist with a struct got_commitable * in the data pointer of
 * each element, a pointer to the log message that must be set by the
 * callback and will be freed after committing, and an argument passed
 * through to the callback.
 */
typedef const struct got_error *(*got_worktree_commit_msg_cb)(
    struct got_pathlist_head *, char **, void *);

/*
 * Create a new commit from changes in the work tree.
 * Return the ID of the newly created commit.
 * The worktree's base commit will be set to this new commit.
 * Files unaffected by this commit operation will retain their
 * current base commit.
 * An author and a non-empty log message must be specified.
 * The name of the committer is optional (may be NULL).
 */
const struct got_error *got_worktree_commit(struct got_object_id **,
    struct got_worktree *, struct got_pathlist_head *, const char *,
    const char *, got_worktree_commit_msg_cb, void *,
    got_worktree_status_cb, void *, struct got_repository *);

/* Get the path of a commitable worktree item. */
const char *got_commitable_get_path(struct got_commitable *);

/* Get the status of a commitable worktree item. */
unsigned int got_commitable_get_status(struct got_commitable *);

/*
 * Prepare for rebasing a branch onto the work tree's current branch.
 * This function creates references to a temporary branch, the branch
 * being rebased, and the work tree's current branch, under the
 * "got/worktree/rebase/" namespace. These references are used to
 * keep track of rebase operation state and are used as input and/or
 * output arguments with other rebase-related functions.
 * The function also returns a pointer to a fileindex which must be
 * passed back to other rebase-related functions.
 */
const struct got_error *got_worktree_rebase_prepare(struct got_reference **,
    struct got_reference **, struct got_fileindex **, struct got_worktree *,
    struct got_reference *, struct got_repository *);

/*
 * Continue an interrupted rebase operation.
 * This function returns existing references created when rebase was prepared,
 * and the ID of the commit currently being rebased. This should be called
 * before either resuming or aborting a rebase operation.
 * The function also returns a pointer to a fileindex which must be
 * passed back to other rebase-related functions.
 */
const struct got_error *got_worktree_rebase_continue(struct got_object_id **,
    struct got_reference **, struct got_reference **, struct got_reference **,
    struct got_fileindex **, struct got_worktree *, struct got_repository *);

/* Check whether a, potentially interrupted, rebase operation is in progress. */
const struct got_error *got_worktree_rebase_in_progress(int *,
    struct got_worktree *);

/*
 * Merge changes from the commit currently being rebased into the work tree.
 * Report affected files, including merge conflicts, via the specified
 * progress callback. Also populate a list of affected paths which should
 * be passed to got_worktree_rebase_commit() after a conflict-free merge.
 * This list must be initialized with TAILQ_INIT() and disposed of with
 * got_worktree_rebase_pathlist_free().
 */
const struct got_error *got_worktree_rebase_merge_files(
    struct got_pathlist_head *, struct got_worktree *, struct got_fileindex *,
    struct got_object_id *, struct got_object_id *, struct got_repository *,
    got_worktree_checkout_cb, void *, got_cancel_cb, void *);

/*
 * Commit changes merged by got_worktree_rebase_merge_files() to a temporary
 * branch and return the ID of the newly created commit. An optional list of
 * merged paths can be provided; otherwise this function will perform a status
 * crawl across the entire work tree to find paths to commit.
 */
const struct got_error *got_worktree_rebase_commit(struct got_object_id **,
    struct got_pathlist_head *, struct got_worktree *, struct got_fileindex *,
    struct got_reference *, struct got_commit_object *,
    struct got_object_id *, struct got_repository *);

/* Free a list of merged paths from got_worktree_merge_files. */
void got_worktree_rebase_pathlist_free(struct got_pathlist_head *);

/* Postpone the rebase operation. Should be called after a merge conflict. */
const struct got_error *got_worktree_rebase_postpone(struct got_worktree *,
    struct got_fileindex *);

/*
 * Complete the current rebase operation. This should be called once all
 * commits have been rebased successfully.
 */
const struct got_error *got_worktree_rebase_complete(struct got_worktree *,
    struct got_fileindex *, struct got_reference *, struct got_reference *,
    struct got_reference *, struct got_repository *);

/*
 * Abort the current rebase operation.
 * Report reverted files via the specified progress callback.
 */
const struct got_error *got_worktree_rebase_abort(struct got_worktree *,
    struct got_fileindex *, struct got_repository *, struct got_reference *,
    got_worktree_checkout_cb, void *);

/*
 * Prepare for editing the history of the work tree's current branch.
 * This function creates references to a temporary branch, and the
 * work tree's current branch, under the "got/worktree/histedit/" namespace.
 * These references are used to keep track of histedit operation state and
 * are used as input and/or output arguments with other histedit-related
 * functions.
 */
const struct got_error *got_worktree_histedit_prepare(struct got_reference **,
    struct got_reference **, struct got_object_id **, struct got_fileindex **,
    struct got_worktree *, struct got_repository *);

/*
 * Continue an interrupted histedit operation.
 * This function returns existing references created when histedit was
 * prepared and the ID of the commit currently being edited.
 * It should be called before resuming or aborting a histedit operation.
 */
const struct got_error *got_worktree_histedit_continue(struct got_object_id **,
    struct got_reference **, struct got_reference **, struct got_object_id **,
    struct got_fileindex **, struct got_worktree *, struct got_repository *);

/* Check whether a histedit operation is in progress. */
const struct got_error *got_worktree_histedit_in_progress(int *,
    struct got_worktree *);

/*
 * Merge changes from the commit currently being edited into the work tree.
 * Report affected files, including merge conflicts, via the specified
 * progress callback. Also populate a list of affected paths which should
 * be passed to got_worktree_histedit_commit() after a conflict-free merge.
 * This list must be initialized with TAILQ_INIT() and disposed of with
 * got_worktree_rebase_pathlist_free().
 */
const struct got_error *got_worktree_histedit_merge_files(
    struct got_pathlist_head *, struct got_worktree *, struct got_fileindex *,
    struct got_object_id *, struct got_object_id *, struct got_repository *,
    got_worktree_checkout_cb, void *, got_cancel_cb, void *);

/*
 * Commit changes merged by got_worktree_histedit_merge_files() to a temporary
 * branch and return the ID of the newly created commit. An optional list of
 * merged paths can be provided; otherwise this function will perform a status
 * crawl across the entire work tree to find paths to commit.
 * An optional log message can be provided which will be used instead of the
 * commit's original message.
 */
const struct got_error *got_worktree_histedit_commit(struct got_object_id **,
    struct got_pathlist_head *, struct got_worktree *, struct got_fileindex *,
    struct got_reference *, struct got_commit_object *,
    struct got_object_id *, const char *, struct got_repository *);

/*
 * Record the specified commit as skipped during histedit.
 * This should be called for commits which get dropped or get folded into
 * a subsequent commit.
 */
const struct got_error *got_worktree_histedit_skip_commit(struct got_worktree *,
    struct got_object_id *, struct got_repository *);

/* Postpone the histedit operation. */
const struct got_error *got_worktree_histedit_postpone(struct got_worktree *,
    struct got_fileindex *);

/*
 * Complete the current histedit operation. This should be called once all
 * commits have been edited successfully.
 */
const struct got_error *got_worktree_histedit_complete(struct got_worktree *,
    struct got_fileindex *, struct got_reference *, struct got_reference *,
    struct got_repository *);

/*
 * Abort the current histedit operation.
 * Report reverted files via the specified progress callback.
 */
const struct got_error *got_worktree_histedit_abort(struct got_worktree *,
    struct got_fileindex *, struct got_repository *, struct got_reference *,
    struct got_object_id *, got_worktree_checkout_cb, void *);

/* Get the path to this work tree's histedit script file. */
const struct got_error *got_worktree_get_histedit_script_path(char **,
    struct got_worktree *);

/*
 * Prepare a work tree for integrating a branch.
 * Return pointers to a fileindex and locked references which must be
 * passed back to other integrate-related functions.
 */
const struct got_error *
got_worktree_integrate_prepare(struct got_fileindex **,
   struct got_reference **, struct got_reference **,
    struct got_worktree *, const char *, struct got_repository *);

/*
 * Carry out a prepared branch integration operation.
 * Report affected files via the specified progress callback.
 */
const struct got_error *got_worktree_integrate_continue(
    struct got_worktree *, struct got_fileindex *, struct got_repository *,
    struct got_reference *, struct got_reference *,
    got_worktree_checkout_cb, void *, got_cancel_cb, void *);

/* Abort a prepared branch integration operation. */
const struct got_error *got_worktree_integrate_abort(struct got_worktree *,
    struct got_fileindex *, struct got_repository *,
    struct got_reference *, struct got_reference *);

/*
 * Stage the specified paths for commit.
 * If the patch callback is not NULL, call it to select patch hunks for
 * staging. Otherwise, stage the full file content found at each path.
*/
const struct got_error *got_worktree_stage(struct got_worktree *,
    struct got_pathlist_head *, got_worktree_status_cb, void *,
    got_worktree_patch_cb, void *, struct got_repository *);

/*
 * Merge staged changes for the specified paths back into the work tree
 * and mark the paths as non-staged again.
 */
const struct got_error *got_worktree_unstage(struct got_worktree *,
    struct got_pathlist_head *, got_worktree_checkout_cb, void *,
    got_worktree_patch_cb, void *, struct got_repository *);
