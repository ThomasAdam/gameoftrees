/*
 * Copyright (c) 2019, 2020 Tracey Emery <tracey@traceyemery.net>
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
#include <sys/stat.h>
#include <sys/types.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <regex.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <got_object.h>
#include <got_reference.h>
#include <got_repository.h>
#include <got_path.h>
#include <got_cancel.h>
#include <got_worktree.h>
#include <got_diff.h>
#include <got_commit_graph.h>
#include <got_blame.h>
#include <got_privsep.h>
#include <got_opentemp.h>

#include <kcgi.h>
#include <kcgihtml.h>

#include "gotweb.h"

#ifndef nitems
#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

struct gw_trans {
	TAILQ_HEAD(headers, gw_header)	 gw_headers;
	TAILQ_HEAD(dirs, gw_dir)	 gw_dirs;
	struct gw_dir		*gw_dir;
	struct gotweb_conf	*gw_conf;
	struct ktemplate	*gw_tmpl;
	struct khtmlreq		*gw_html_req;
	struct kreq		*gw_req;
	const struct got_error	*error;
	const char		*repo_name;
	char			*repo_path;
	char			*commit;
	const char		*repo_file;
	char			*repo_folder;
	char			*headref;
	unsigned int		 action;
	unsigned int		 page;
	unsigned int		 repos_total;
	enum kmime		 mime;
};

struct gw_header {
	TAILQ_ENTRY(gw_header)		 entry;
	struct got_repository		*repo;
	struct got_reflist_head		 refs;
	struct got_commit_object	*commit;
	struct got_object_id		*id;
	char				*path;

	char			*refs_str;
	char			*commit_id; /* id_str1 */
	char			*parent_id; /* id_str2 */
	char			*tree_id;
	const char		*author;
	const char		*committer;
	char			*commit_msg;
	time_t			 committer_time;
};

struct gw_dir {
	TAILQ_ENTRY(gw_dir)	 entry;
	char			*name;
	char			*owner;
	char			*description;
	char			*url;
	char			*age;
	char			*path;
};

enum gw_key {
	KEY_ACTION,
	KEY_COMMIT_ID,
	KEY_FILE,
	KEY_FOLDER,
	KEY_HEADREF,
	KEY_PAGE,
	KEY_PATH,
	KEY__ZMAX
};

enum gw_tmpl {
	TEMPL_CONTENT,
	TEMPL_HEAD,
	TEMPL_HEADER,
	TEMPL_SEARCH,
	TEMPL_SITEPATH,
	TEMPL_SITEOWNER,
	TEMPL_TITLE,
	TEMPL__MAX
};

enum gw_ref_tm {
	TM_DIFF,
	TM_LONG,
};

enum gw_tags {
	TAGBRIEF,
	TAGFULL,
};

static const char *const gw_templs[TEMPL__MAX] = {
	"content",
	"head",
	"header",
	"search",
	"sitepath",
	"siteowner",
	"title",
};

static const struct kvalid gw_keys[KEY__ZMAX] = {
	{ kvalid_stringne,	"action" },
	{ kvalid_stringne,	"commit" },
	{ kvalid_stringne,	"file" },
	{ kvalid_stringne,	"folder" },
	{ kvalid_stringne,	"headref" },
	{ kvalid_int,		"page" },
	{ kvalid_stringne,	"path" },
};

static struct gw_header		*gw_init_header(void);

static void			 gw_free_headers(struct gw_header *);

static int			 gw_template(size_t, void *);

static const struct got_error	*gw_error(struct gw_trans *);
static const struct got_error	*gw_init_gw_dir(struct gw_dir **, const char *);
static const struct got_error	*gw_get_repo_description(char **,
				    struct gw_trans *, char *);
static const struct got_error	*gw_get_repo_owner(char **, struct gw_trans *,
				    char *);
static const struct got_error	*gw_get_time_str(char **, time_t, int);
static const struct got_error	*gw_get_repo_age(char **, struct gw_trans *,
				    char *, char *, int);
static const struct got_error	*gw_output_file_blame(struct gw_trans *);
static const struct got_error	*gw_output_blob_buf(struct gw_trans *);
static const struct got_error	*gw_output_repo_tree(struct gw_trans *);
static const struct got_error	*gw_output_diff(struct gw_trans *,
				    struct gw_header *);
static const struct got_error	*gw_output_repo_tags(struct gw_trans *,
				    struct gw_header *, int, int);
static const struct got_error	*gw_output_repo_heads(struct gw_trans *);
static const struct got_error	*gw_output_site_link(struct gw_trans *);
static const struct got_error	*gw_get_clone_url(char **, struct gw_trans *,
				    char *);
static const struct got_error	*gw_colordiff_line(struct gw_trans *, char *);

static const struct got_error	*gw_gen_commit_header(struct gw_trans *, char *,
				    char*);
static const struct got_error	*gw_gen_diff_header(struct gw_trans *, char *,
				    char*);
static const struct got_error	*gw_gen_author_header(struct gw_trans *,
				    const char *);
static const struct got_error	*gw_gen_age_header(struct gw_trans *,
				    const char *);
static const struct got_error	*gw_gen_committer_header(struct gw_trans *,
				    const char *);
static const struct got_error	*gw_gen_commit_msg_header(struct gw_trans*,
				    char *);
static const struct got_error	*gw_gen_tree_header(struct gw_trans *, char *);
static const struct got_error	*gw_display_open(struct gw_trans *, enum khttp,
				    enum kmime);
static const struct got_error	*gw_display_index(struct gw_trans *);
static const struct got_error	*gw_get_header(struct gw_trans *,
				    struct gw_header *, int);
static const struct got_error	*gw_get_commits(struct gw_trans *,
				    struct gw_header *, int);
static const struct got_error	*gw_get_commit(struct gw_trans *,
				    struct gw_header *);
static const struct got_error	*gw_apply_unveil(const char *);
static const struct got_error	*gw_blame_cb(void *, int, int,
				    struct got_object_id *);
static const struct got_error	*gw_load_got_paths(struct gw_trans *);
static const struct got_error	*gw_load_got_path(struct gw_trans *,
				    struct gw_dir *);
static const struct got_error	*gw_parse_querystring(struct gw_trans *);
static const struct got_error	*gw_blame(struct gw_trans *);
static const struct got_error	*gw_blob(struct gw_trans *);
static const struct got_error	*gw_diff(struct gw_trans *);
static const struct got_error	*gw_index(struct gw_trans *);
static const struct got_error	*gw_commits(struct gw_trans *);
static const struct got_error	*gw_briefs(struct gw_trans *);
static const struct got_error	*gw_summary(struct gw_trans *);
static const struct got_error	*gw_tree(struct gw_trans *);
static const struct got_error	*gw_tag(struct gw_trans *);

struct gw_query_action {
	unsigned int		 func_id;
	const char		*func_name;
	const struct got_error	*(*func_main)(struct gw_trans *);
	char			*template;
};

enum gw_query_actions {
	GW_BLAME,
	GW_BLOB,
	GW_BRIEFS,
	GW_COMMITS,
	GW_DIFF,
	GW_ERR,
	GW_INDEX,
	GW_SUMMARY,
	GW_TAG,
	GW_TREE,
};

static struct gw_query_action gw_query_funcs[] = {
	{ GW_BLAME,	"blame",	gw_blame,	"gw_tmpl/blame.tmpl" },
	{ GW_BLOB,	"blob",		NULL,		NULL },
	{ GW_BRIEFS,	"briefs",	gw_briefs,	"gw_tmpl/briefs.tmpl" },
	{ GW_COMMITS,	"commits",	gw_commits,	"gw_tmpl/commit.tmpl" },
	{ GW_DIFF,	"diff",		gw_diff,	"gw_tmpl/diff.tmpl" },
	{ GW_ERR,	"error",	gw_error,	"gw_tmpl/err.tmpl" },
	{ GW_INDEX,	"index",	gw_index,	"gw_tmpl/index.tmpl" },
	{ GW_SUMMARY,	"summary",	gw_summary,	"gw_tmpl/summry.tmpl" },
	{ GW_TAG,	"tag",		gw_tag,		"gw_tmpl/tag.tmpl" },
	{ GW_TREE,	"tree",		gw_tree,	"gw_tmpl/tree.tmpl" },
};

static const char * 
gw_get_action_name(struct gw_trans *gw_trans)
{
	return gw_query_funcs[gw_trans->action].func_name;
}

static const struct got_error *
gw_kcgi_error(enum kcgi_err kerr)
{
	if (kerr == KCGI_OK)
		return NULL;

	if (kerr == KCGI_EXIT || kerr == KCGI_HUP)
		return got_error(GOT_ERR_CANCELLED);

	if (kerr == KCGI_ENOMEM)
		return got_error_set_errno(ENOMEM,
		    kcgi_strerror(kerr));

	if (kerr == KCGI_ENFILE)
		return got_error_set_errno(ENFILE,
		    kcgi_strerror(kerr));

	if (kerr == KCGI_EAGAIN)
		return got_error_set_errno(EAGAIN,
		    kcgi_strerror(kerr));

	if (kerr == KCGI_FORM)
		return got_error_msg(GOT_ERR_IO,
		    kcgi_strerror(kerr));

	return got_error_from_errno(kcgi_strerror(kerr));
}

static const struct got_error *
gw_apply_unveil(const char *repo_path)
{
	const struct got_error *err;

	if (repo_path && unveil(repo_path, "r") != 0)
		return got_error_from_errno2("unveil", repo_path);

	if (unveil("/tmp", "rwc") != 0)
		return got_error_from_errno2("unveil", "/tmp");

	err = got_privsep_unveil_exec_helpers();
	if (err != NULL)
		return err;

	if (unveil(NULL, NULL) != 0)
		return got_error_from_errno("unveil");

	return NULL;
}

static int
isbinary(const uint8_t *buf, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (buf[i] == 0)
			return 1;
	return 0;
}

static const struct got_error *
gw_blame(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	struct gw_header *header = NULL;
	char *age = NULL;
	enum kcgi_err kerr;

	if (pledge("stdio rpath wpath cpath proc exec sendfd unveil",
	    NULL) == -1)
		return got_error_from_errno("pledge");

	if ((header = gw_init_header()) == NULL)
		return got_error_from_errno("malloc");

	error = gw_apply_unveil(gw_trans->gw_dir->path);
	if (error)
		goto done;

	error = gw_get_header(gw_trans, header, 1);
	if (error)
		goto done;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "blame_header_wrapper", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "blame_header", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	error = gw_get_time_str(&age, header->committer_time,
	    TM_LONG);
	if (error)
		goto done;
	error = gw_gen_age_header(gw_trans, age ?age : "");
	if (error)
		goto done;
	error = gw_gen_commit_msg_header(gw_trans, header->commit_msg);
	if (error)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "dotted_line", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		goto done;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "blame", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	error = gw_output_file_blame(gw_trans);
	if (error)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
done:
	got_ref_list_free(&header->refs);
	gw_free_headers(header);
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_blob(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	struct gw_header *header = NULL;

	if (pledge("stdio rpath wpath cpath proc exec sendfd unveil",
	    NULL) == -1)
		return got_error_from_errno("pledge");

	if ((header = gw_init_header()) == NULL)
		return got_error_from_errno("malloc");

	error = gw_apply_unveil(gw_trans->gw_dir->path);
	if (error)
		goto done;

	error = gw_get_header(gw_trans, header, 1);
	if (error)
		goto done;

	error = gw_output_blob_buf(gw_trans);
done:
	got_ref_list_free(&header->refs);
	gw_free_headers(header);
	return error;
}

static const struct got_error *
gw_diff(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	struct gw_header *header = NULL;
	char *age = NULL;
	enum kcgi_err kerr = KCGI_OK;

	if (pledge("stdio rpath wpath cpath proc exec sendfd unveil",
	    NULL) == -1)
		return got_error_from_errno("pledge");

	if ((header = gw_init_header()) == NULL)
		return got_error_from_errno("malloc");

	error = gw_apply_unveil(gw_trans->gw_dir->path);
	if (error)
		goto done;

	error = gw_get_header(gw_trans, header, 1);
	if (error)
		goto done;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "diff_header_wrapper", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "diff_header", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	error = gw_gen_diff_header(gw_trans, header->parent_id,
	    header->commit_id);
	if (error)
		goto done;
	error = gw_gen_commit_header(gw_trans, header->commit_id,
	    header->refs_str);
	if (error)
		goto done;
	error = gw_gen_tree_header(gw_trans, header->tree_id);
	if (error)
		goto done;
	error = gw_gen_author_header(gw_trans, header->author);
	if (error)
		goto done;
	error = gw_gen_committer_header(gw_trans, header->author);
	if (error)
		goto done;
	error = gw_get_time_str(&age, header->committer_time,
	    TM_LONG);
	if (error)
		goto done;
	error = gw_gen_age_header(gw_trans, age ?age : "");
	if (error)
		goto done;
	error = gw_gen_commit_msg_header(gw_trans, header->commit_msg);
	if (error)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "dotted_line", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		goto done;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "diff", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	error = gw_output_diff(gw_trans, header);
	if (error)
		goto done;

	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
done:
	got_ref_list_free(&header->refs);
	gw_free_headers(header);
	free(age);
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_index(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	struct gw_dir *gw_dir = NULL;
	char *href_next = NULL, *href_prev = NULL, *href_summary = NULL;
	char *href_briefs = NULL, *href_commits = NULL, *href_tree = NULL;
	unsigned int prev_disp = 0, next_disp = 1, dir_c = 0;
	enum kcgi_err kerr;

	if (pledge("stdio rpath proc exec sendfd unveil",
	    NULL) == -1) {
		error = got_error_from_errno("pledge");
		return error;
	}

	error = gw_apply_unveil(gw_trans->gw_conf->got_repos_path);
	if (error)
		return error;

	error = gw_load_got_paths(gw_trans);
	if (error)
		return error;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "index_header", KATTR__MAX);
	if (kerr != KCGI_OK)
		return gw_kcgi_error(kerr);
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "index_header_project", KATTR__MAX);
	if (kerr != KCGI_OK)
		return gw_kcgi_error(kerr);
	kerr = khtml_puts(gw_trans->gw_html_req, "Project");
	if (kerr != KCGI_OK)
		return gw_kcgi_error(kerr);
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		return gw_kcgi_error(kerr);

	if (gw_trans->gw_conf->got_show_repo_description) {
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "index_header_description", KATTR__MAX);
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
		kerr = khtml_puts(gw_trans->gw_html_req, "Description");
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
	}

	if (gw_trans->gw_conf->got_show_repo_owner) {
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "index_header_owner", KATTR__MAX);
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
		kerr = khtml_puts(gw_trans->gw_html_req, "Owner");
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
	}

	if (gw_trans->gw_conf->got_show_repo_age) {
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "index_header_age", KATTR__MAX);
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
		kerr = khtml_puts(gw_trans->gw_html_req, "Last Change");
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
	}

	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		return gw_kcgi_error(kerr);

	if (TAILQ_EMPTY(&gw_trans->gw_dirs)) {
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "index_wrapper", KATTR__MAX);
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
		kerr = khtml_puts(gw_trans->gw_html_req,
		    "No repositories found in ");
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
		kerr = khtml_puts(gw_trans->gw_html_req,
		    gw_trans->gw_conf->got_repos_path);
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "dotted_line", KATTR__MAX);
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
		return error;
	}

	TAILQ_FOREACH(gw_dir, &gw_trans->gw_dirs, entry)
		dir_c++;

	TAILQ_FOREACH(gw_dir, &gw_trans->gw_dirs, entry) {
		if (gw_trans->page > 0 && (gw_trans->page *
		    gw_trans->gw_conf->got_max_repos_display) > prev_disp) {
			prev_disp++;
			continue;
		}

		prev_disp++;

		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "index_wrapper", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;

		if (asprintf(&href_summary, "?path=%s&action=summary",
		    gw_dir->name) == -1) {
			error = got_error_from_errno("asprintf");
			goto done;
		}
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "index_project", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A, KATTR_HREF,
		    href_summary, KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, gw_dir->name);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
		if (kerr != KCGI_OK)
			goto done;
		if (gw_trans->gw_conf->got_show_repo_description) {
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "index_project_description", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req,
			    gw_dir->description ? gw_dir->description : "");
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				goto done;
		}
		if (gw_trans->gw_conf->got_show_repo_owner) {
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "index_project_owner", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req,
			    gw_dir->owner ? gw_dir->owner : "");
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				goto done;
		}
		if (gw_trans->gw_conf->got_show_repo_age) {
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "index_project_age", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req,
			    gw_dir->age ? gw_dir->age : "");
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				goto done;
		}

		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "navs_wrapper", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "navs", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A, KATTR_HREF,
		    href_summary, KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, "summary");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_puts(gw_trans->gw_html_req, " | ");
		if (kerr != KCGI_OK)
			goto done;

		if (asprintf(&href_briefs, "?path=%s&action=briefs",
		    gw_dir->name) == -1) {
			error = got_error_from_errno("asprintf");
			goto done;
		}
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A, KATTR_HREF,
		    href_briefs, KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, "commit briefs");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			error = gw_kcgi_error(kerr);

		kerr = khtml_puts(gw_trans->gw_html_req, " | ");
		if (kerr != KCGI_OK)
			goto done;

		if (asprintf(&href_commits, "?path=%s&action=commits",
		    gw_dir->name) == -1) {
			error = got_error_from_errno("asprintf");
			goto done;
		}
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A, KATTR_HREF,
		    href_commits, KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, "commits");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_puts(gw_trans->gw_html_req, " | ");
		if (kerr != KCGI_OK)
			goto done;

		if (asprintf(&href_tree, "?path=%s&action=tree",
		    gw_dir->name) == -1) {
			error = got_error_from_errno("asprintf");
			goto done;
		}
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A, KATTR_HREF,
		    href_tree, KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, "tree");
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_closeelem(gw_trans->gw_html_req, 4);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "dotted_line", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;

		free(href_summary);
		href_summary = NULL;
		free(href_briefs);
		href_briefs = NULL;
		free(href_commits);
		href_commits = NULL;
		free(href_tree);
		href_tree = NULL;

		if (gw_trans->gw_conf->got_max_repos_display == 0)
			continue;

		if (next_disp == gw_trans->gw_conf->got_max_repos_display) {
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "np_wrapper", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "nav_prev", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
		} else if ((gw_trans->gw_conf->got_max_repos_display > 0) &&
		    (gw_trans->page > 0) &&
		    (next_disp == gw_trans->gw_conf->got_max_repos_display ||
		    prev_disp == gw_trans->repos_total)) {
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "np_wrapper", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "nav_prev", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
		}

		if ((gw_trans->gw_conf->got_max_repos_display > 0) &&
		    (gw_trans->page > 0) &&
		    (next_disp == gw_trans->gw_conf->got_max_repos_display ||
		    prev_disp == gw_trans->repos_total)) {
			if (asprintf(&href_prev, "?page=%d",
			    gw_trans->page - 1) == -1) {
				error = got_error_from_errno("asprintf");
				goto done;
			}
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
			    KATTR_HREF, href_prev, KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req, "Previous");
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				goto done;
		}

		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);

		if (gw_trans->gw_conf->got_max_repos_display > 0 &&
		    next_disp == gw_trans->gw_conf->got_max_repos_display &&
		    dir_c != (gw_trans->page + 1) *
		    gw_trans->gw_conf->got_max_repos_display) {
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "nav_next", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			if (asprintf(&href_next, "?page=%d",
			    gw_trans->page + 1) == -1) {
				error = got_error_from_errno("calloc");
				goto done;
			}
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
			    KATTR_HREF, href_next, KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req, "Next");
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 3);
			if (kerr != KCGI_OK)
				goto done;
			next_disp = 0;
			break;
		}

		if ((gw_trans->gw_conf->got_max_repos_display > 0) &&
		    (gw_trans->page > 0) &&
		    (next_disp == gw_trans->gw_conf->got_max_repos_display ||
		    prev_disp == gw_trans->repos_total)) {
			kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
			if (kerr != KCGI_OK)
				goto done;
		}
		next_disp++;
	}
done:
	free(href_prev);
	free(href_next);
	free(href_summary);
	free(href_briefs);
	free(href_commits);
	free(href_tree);
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_commits(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	struct gw_header *header = NULL, *n_header = NULL;
	char *age = NULL;
	char *href_diff = NULL, *href_blob = NULL;
	enum kcgi_err kerr = KCGI_OK;

	if ((header = gw_init_header()) == NULL)
		return got_error_from_errno("malloc");

	if (pledge("stdio rpath proc exec sendfd unveil",
	    NULL) == -1) {
		error = got_error_from_errno("pledge");
		goto done;
	}

	error = gw_apply_unveil(gw_trans->gw_dir->path);
	if (error)
		goto done;

	error = gw_get_header(gw_trans, header,
	    gw_trans->gw_conf->got_max_commits_display);
	if (error)
		goto done;

	TAILQ_FOREACH(n_header, &gw_trans->gw_headers, entry) {
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "commits_line_wrapper", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		error = gw_gen_commit_header(gw_trans, n_header->commit_id,
		    n_header->refs_str);
		if (error)
			goto done;
		error = gw_gen_author_header(gw_trans, n_header->author);
		if (error)
			goto done;
		error = gw_gen_committer_header(gw_trans, n_header->author);
		if (error)
			goto done;
		error = gw_get_time_str(&age, n_header->committer_time,
		    TM_LONG);
		if (error)
			goto done;
		error = gw_gen_age_header(gw_trans, age ?age : "");
		if (error)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "dotted_line", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "commit", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khttp_puts(gw_trans->gw_req, n_header->commit_msg);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;

		if (asprintf(&href_diff, "?path=%s&action=diff&commit=%s",
		    gw_trans->repo_name, n_header->commit_id) == -1) {
			error = got_error_from_errno("asprintf");
			goto done;
		}
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "navs_wrapper", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "navs", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
		    KATTR_HREF, href_diff, KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, "diff");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_puts(gw_trans->gw_html_req, " | ");
		if (kerr != KCGI_OK)
			goto done;

		if (asprintf(&href_blob, "?path=%s&action=tree&commit=%s",
		    gw_trans->repo_name, n_header->commit_id) == -1) {
			error = got_error_from_errno("asprintf");
			goto done;
		}
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
		    KATTR_HREF, href_blob, KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		khtml_puts(gw_trans->gw_html_req, "tree");
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "solid_line", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
		if (kerr != KCGI_OK)
			goto done;

		free(age);
		age = NULL;
	}
done:
	got_ref_list_free(&header->refs);
	gw_free_headers(header);
	TAILQ_FOREACH(n_header, &gw_trans->gw_headers, entry)
		gw_free_headers(n_header);
	free(age);
	free(href_diff);
	free(href_blob);
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_briefs(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	struct gw_header *header = NULL, *n_header = NULL;
	char *age = NULL;
	char *href_diff = NULL, *href_blob = NULL;
	char *newline, *smallerthan;
	enum kcgi_err kerr = KCGI_OK;

	if ((header = gw_init_header()) == NULL)
		return got_error_from_errno("malloc");

	if (pledge("stdio rpath proc exec sendfd unveil",
	    NULL) == -1) {
		error = got_error_from_errno("pledge");
		goto done;
	}

	error = gw_apply_unveil(gw_trans->gw_dir->path);
	if (error)
		goto done;

	if (gw_trans->action == GW_SUMMARY)
		error = gw_get_header(gw_trans, header, D_MAXSLCOMMDISP);
	else
		error = gw_get_header(gw_trans, header,
		    gw_trans->gw_conf->got_max_commits_display);
	if (error)
		goto done;

	TAILQ_FOREACH(n_header, &gw_trans->gw_headers, entry) {
		error = gw_get_time_str(&age, n_header->committer_time,
		    TM_DIFF);
		if (error)
			goto done;

		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "briefs_wrapper", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "briefs_age", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, age ? age : "");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "briefs_author", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		smallerthan = strchr(n_header->author, '<');
		if (smallerthan)
			*smallerthan = '\0';
		kerr = khtml_puts(gw_trans->gw_html_req, n_header->author);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;

		if (asprintf(&href_diff, "?path=%s&action=diff&commit=%s",
		    gw_trans->repo_name, n_header->commit_id) == -1) {
			error = got_error_from_errno("asprintf");
			goto done;
		}
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "briefs_log", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		newline = strchr(n_header->commit_msg, '\n');
		if (newline)
			*newline = '\0';
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
		    KATTR_HREF, href_diff, KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, n_header->commit_msg);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "navs_wrapper", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "navs", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
		    KATTR_HREF, href_diff, KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, "diff");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_puts(gw_trans->gw_html_req, " | ");
		if (kerr != KCGI_OK)
			goto done;

		if (asprintf(&href_blob, "?path=%s&action=tree&commit=%s",
		    gw_trans->repo_name, n_header->commit_id) == -1) {
			error = got_error_from_errno("asprintf");
			goto done;
		}
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
		    KATTR_HREF, href_blob, KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		khtml_puts(gw_trans->gw_html_req, "tree");
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "dotted_line", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 3);
		if (kerr != KCGI_OK)
			goto done;

		free(age);
		age = NULL;
		free(href_diff);
		href_diff = NULL;
		free(href_blob);
		href_blob = NULL;
	}
done:
	got_ref_list_free(&header->refs);
	gw_free_headers(header);
	TAILQ_FOREACH(n_header, &gw_trans->gw_headers, entry)
		gw_free_headers(n_header);
	free(age);
	free(href_diff);
	free(href_blob);
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_summary(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	char *age = NULL;
	enum kcgi_err kerr = KCGI_OK;

	if (pledge("stdio rpath proc exec sendfd unveil", NULL) == -1)
		return got_error_from_errno("pledge");

	/* unveil is applied with gw_briefs below */

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "summary_wrapper", KATTR__MAX);
	if (kerr != KCGI_OK)
		return gw_kcgi_error(kerr);

	if (gw_trans->gw_conf->got_show_repo_description &&
	    gw_trans->gw_dir->description != NULL &&
	    (strcmp(gw_trans->gw_dir->description, "") != 0)) {
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "description_title", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, "Description: ");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "description", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req,
		    gw_trans->gw_dir->description);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;
	}

	if (gw_trans->gw_conf->got_show_repo_owner &&
	    gw_trans->gw_dir->owner != NULL &&
	    (strcmp(gw_trans->gw_dir->owner, "") != 0)) {
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "repo_owner_title", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, "Owner: ");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "repo_owner", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req,
		     gw_trans->gw_dir->owner);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;
	}

	if (gw_trans->gw_conf->got_show_repo_age) {
		error = gw_get_repo_age(&age, gw_trans, gw_trans->gw_dir->path,
		    "refs/heads", TM_LONG);
		if (error)
			goto done;
		if (age != NULL) {
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "last_change_title", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req,
			    "Last Change: ");
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "last_change", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req, age);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				goto done;
		}
	}

	if (gw_trans->gw_conf->got_show_repo_cloneurl &&
	    gw_trans->gw_dir->url != NULL &&
	    (strcmp(gw_trans->gw_dir->url, "") != 0)) {
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "cloneurl_title", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, "Clone URL: ");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "cloneurl", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, gw_trans->gw_dir->url);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;
	}

	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		goto done;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "briefs_title_wrapper", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "briefs_title", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, "Commit Briefs");
	if (kerr != KCGI_OK)
		goto done;
	if (gw_trans->headref) {
		kerr = khtml_puts(gw_trans->gw_html_req, " (");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, gw_trans->headref);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, ")");
		if (kerr != KCGI_OK)
			goto done;
	}
	kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
	if (kerr != KCGI_OK)
		goto done;
	error = gw_briefs(gw_trans);
	if (error)
		goto done;

	error = gw_output_repo_tags(gw_trans, NULL, D_MAXSLCOMMDISP,
	    TAGBRIEF);
	if (error)
		goto done;

	error = gw_output_repo_heads(gw_trans);
done:
	free(age);
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_tree(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	struct gw_header *header = NULL;
	char *tree = NULL, *tree_html = NULL, *tree_html_disp = NULL;
	char *age = NULL;
	enum kcgi_err kerr;

	if (pledge("stdio rpath proc exec sendfd unveil", NULL) == -1)
		return got_error_from_errno("pledge");

	if ((header = gw_init_header()) == NULL)
		return got_error_from_errno("malloc");

	error = gw_apply_unveil(gw_trans->gw_dir->path);
	if (error)
		goto done;

	error = gw_get_header(gw_trans, header, 1);
	if (error)
		goto done;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "tree_header_wrapper", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "tree_header", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	error = gw_gen_tree_header(gw_trans, header->tree_id);
	if (error)
		goto done;
	error = gw_get_time_str(&age, header->committer_time,
	    TM_LONG);
	if (error)
		goto done;
	error = gw_gen_age_header(gw_trans, age ?age : "");
	if (error)
		goto done;
	error = gw_gen_commit_msg_header(gw_trans, header->commit_msg);
	if (error)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "dotted_line", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		goto done;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "tree", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	error = gw_output_repo_tree(gw_trans);
	if (error)
		goto done;

	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
done:
	got_ref_list_free(&header->refs);
	gw_free_headers(header);
	free(tree_html_disp);
	free(tree_html);
	free(tree);
	free(age);
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_tag(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	struct gw_header *header = NULL;
	enum kcgi_err kerr;

	if (pledge("stdio rpath proc exec sendfd unveil", NULL) == -1)
		return got_error_from_errno("pledge");

	if ((header = gw_init_header()) == NULL)
		return got_error_from_errno("malloc");

	error = gw_apply_unveil(gw_trans->gw_dir->path);
	if (error)
		goto done;

	error = gw_get_header(gw_trans, header, 1);
	if (error)
		goto done;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "tag_header_wrapper", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "tag_header", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	error = gw_gen_commit_header(gw_trans, header->commit_id,
	    header->refs_str);
	if (error)
		goto done;
	error = gw_gen_commit_msg_header(gw_trans, header->commit_msg);
	if (error)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "dotted_line", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		goto done;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "tree", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;

	error = gw_output_repo_tags(gw_trans, header, 1, TAGFULL);
	if (error)
		goto done;

	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
done:
	got_ref_list_free(&header->refs);
	gw_free_headers(header);
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_load_got_path(struct gw_trans *gw_trans, struct gw_dir *gw_dir)
{
	const struct got_error *error = NULL;
	DIR *dt;
	char *dir_test;
	int opened = 0;

	if (asprintf(&dir_test, "%s/%s/%s",
	    gw_trans->gw_conf->got_repos_path, gw_dir->name,
	    GOTWEB_GIT_DIR) == -1)
		return got_error_from_errno("asprintf");

	dt = opendir(dir_test);
	if (dt == NULL) {
		free(dir_test);
	} else {
		gw_dir->path = strdup(dir_test);
		if (gw_dir->path == NULL) {
			opened = 1;
			error = got_error_from_errno("strdup");
			goto errored;
		}
		opened = 1;
		goto done;
	}

	if (asprintf(&dir_test, "%s/%s/%s",
	    gw_trans->gw_conf->got_repos_path, gw_dir->name,
	    GOTWEB_GOT_DIR) == -1) {
		dir_test = NULL;
		error = got_error_from_errno("asprintf");
		goto errored;
	}

	dt = opendir(dir_test);
	if (dt == NULL)
		free(dir_test);
	else {
		opened = 1;
		error = got_error(GOT_ERR_NOT_GIT_REPO);
		goto errored;
	}

	if (asprintf(&dir_test, "%s/%s",
	    gw_trans->gw_conf->got_repos_path, gw_dir->name) == -1) {
		error = got_error_from_errno("asprintf");
		dir_test = NULL;
		goto errored;
	}

	gw_dir->path = strdup(dir_test);
	if (gw_dir->path == NULL) {
		opened = 1;
		error = got_error_from_errno("strdup");
		goto errored;
	}

	dt = opendir(dir_test);
	if (dt == NULL) {
		error = got_error_from_errno2("bad path", dir_test);
		goto errored;
	} else
		opened = 1;
done:
	error = gw_get_repo_description(&gw_dir->description, gw_trans,
	    gw_dir->path);
	if (error)
		goto errored;
	error = gw_get_repo_owner(&gw_dir->owner, gw_trans, gw_dir->path);
	if (error)
		goto errored;
	error = gw_get_repo_age(&gw_dir->age, gw_trans, gw_dir->path,
	    "refs/heads", TM_DIFF);
	if (error)
		goto errored;
	error = gw_get_clone_url(&gw_dir->url, gw_trans, gw_dir->path);
errored:
	free(dir_test);
	if (opened)
		closedir(dt);
	return error;
}

static const struct got_error *
gw_load_got_paths(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	DIR *d;
	struct dirent **sd_dent;
	struct gw_dir *gw_dir;
	struct stat st;
	unsigned int d_cnt, d_i;

	d = opendir(gw_trans->gw_conf->got_repos_path);
	if (d == NULL) {
		error = got_error_from_errno2("opendir",
		    gw_trans->gw_conf->got_repos_path);
		return error;
	}

	d_cnt = scandir(gw_trans->gw_conf->got_repos_path, &sd_dent, NULL,
	    alphasort);
	if (d_cnt == -1) {
		error = got_error_from_errno2("scandir",
		    gw_trans->gw_conf->got_repos_path);
		return error;
	}

	for (d_i = 0; d_i < d_cnt; d_i++) {
		if (gw_trans->gw_conf->got_max_repos > 0 &&
		    (d_i - 2) == gw_trans->gw_conf->got_max_repos)
			break; /* account for parent and self */

		if (strcmp(sd_dent[d_i]->d_name, ".") == 0 ||
		    strcmp(sd_dent[d_i]->d_name, "..") == 0)
			continue;

		error = gw_init_gw_dir(&gw_dir, sd_dent[d_i]->d_name);
		if (error)
			return error;

		error = gw_load_got_path(gw_trans, gw_dir);
		if (error && error->code == GOT_ERR_NOT_GIT_REPO) {
			error = NULL;
			continue;
		}
		else if (error)
			return error;

		if (lstat(gw_dir->path, &st) == 0 && S_ISDIR(st.st_mode) &&
		    !got_path_dir_is_empty(gw_dir->path)) {
			TAILQ_INSERT_TAIL(&gw_trans->gw_dirs, gw_dir,
			    entry);
			gw_trans->repos_total++;
		}
	}

	closedir(d);
	return error;
}

static const struct got_error *
gw_parse_querystring(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	struct kpair *p;
	struct gw_query_action *action = NULL;
	unsigned int i;

	if (gw_trans->gw_req->fieldnmap[0]) {
		error = got_error_from_errno("bad parse");
		return error;
	} else if ((p = gw_trans->gw_req->fieldmap[KEY_PATH])) {
		/* define gw_trans->repo_path */
		gw_trans->repo_name = p->parsed.s;

		if (asprintf(&gw_trans->repo_path, "%s/%s",
		    gw_trans->gw_conf->got_repos_path, p->parsed.s) == -1)
			return got_error_from_errno("asprintf");

		/* get action and set function */
		if ((p = gw_trans->gw_req->fieldmap[KEY_ACTION])) {
			for (i = 0; i < nitems(gw_query_funcs); i++) {
				action = &gw_query_funcs[i];
				if (action->func_name == NULL)
					continue;
				if (strcmp(action->func_name,
				    p->parsed.s) == 0) {
					gw_trans->action = i;
					break;
				}
			}
		}
		if (gw_trans->action == -1) {
			gw_trans->action = GW_ERR;
			gw_trans->error = got_error_from_errno("bad action");
			return error;
		}

 		if ((p = gw_trans->gw_req->fieldmap[KEY_COMMIT_ID])) {
			if (asprintf(&gw_trans->commit, "%s",
			    p->parsed.s) == -1)
				return got_error_from_errno("asprintf");
		}

		if ((p = gw_trans->gw_req->fieldmap[KEY_FILE]))
			gw_trans->repo_file = p->parsed.s;

		if ((p = gw_trans->gw_req->fieldmap[KEY_FOLDER])) {
			if (asprintf(&gw_trans->repo_folder, "%s",
			    p->parsed.s) == -1)
				return got_error_from_errno("asprintf");
		}

		if ((p = gw_trans->gw_req->fieldmap[KEY_HEADREF])) {
			if (asprintf(&gw_trans->headref, "%s",
			    p->parsed.s) == -1)
				return got_error_from_errno("asprintf");
		}

		error = gw_init_gw_dir(&gw_trans->gw_dir, gw_trans->repo_name);
		if (error)
			return error;

		gw_trans->error = gw_load_got_path(gw_trans, gw_trans->gw_dir);
	} else
		gw_trans->action = GW_INDEX;

	if ((p = gw_trans->gw_req->fieldmap[KEY_PAGE]))
		gw_trans->page = p->parsed.i;

	return error;
}

static const struct got_error *
gw_init_gw_dir(struct gw_dir **gw_dir, const char *dir)
{
	const struct got_error *error;

	*gw_dir = malloc(sizeof(**gw_dir));
	if (*gw_dir == NULL)
		return got_error_from_errno("malloc");

	if (asprintf(&(*gw_dir)->name, "%s", dir) == -1) {
		error = got_error_from_errno("asprintf");
		free(*gw_dir);
		*gw_dir = NULL;
		return NULL;
	}

	return NULL;
}

static const struct got_error *
gw_display_open(struct gw_trans *gw_trans, enum khttp code, enum kmime mime)
{
	enum kcgi_err kerr;

	kerr = khttp_head(gw_trans->gw_req, kresps[KRESP_ALLOW], "GET");
	if (kerr != KCGI_OK)
		return gw_kcgi_error(kerr);
	kerr = khttp_head(gw_trans->gw_req, kresps[KRESP_STATUS], "%s",
	    khttps[code]);
	if (kerr != KCGI_OK)
		return gw_kcgi_error(kerr);
	kerr = khttp_head(gw_trans->gw_req, kresps[KRESP_CONTENT_TYPE], "%s",
	    kmimetypes[mime]);
	if (kerr != KCGI_OK)
		return gw_kcgi_error(kerr);
	kerr = khttp_head(gw_trans->gw_req, "X-Content-Type-Options",
	    "nosniff");
	if (kerr != KCGI_OK)
		return gw_kcgi_error(kerr);
	kerr = khttp_head(gw_trans->gw_req, "X-Frame-Options", "DENY");
	if (kerr != KCGI_OK)
		return gw_kcgi_error(kerr);
	kerr = khttp_head(gw_trans->gw_req, "X-XSS-Protection",
	    "1; mode=block");
	if (kerr != KCGI_OK)
		return gw_kcgi_error(kerr);

	/* XXX repo_file could be NULL if not present in querystring */
	if (gw_trans->mime == KMIME_APP_OCTET_STREAM) {
		kerr = khttp_head(gw_trans->gw_req,
		    kresps[KRESP_CONTENT_DISPOSITION],
		    "attachment; filename=%s", gw_trans->repo_file);
		if (kerr != KCGI_OK)
			return gw_kcgi_error(kerr);
	}

	kerr = khttp_body(gw_trans->gw_req);
	return gw_kcgi_error(kerr);
}

static const struct got_error *
gw_display_index(struct gw_trans *gw_trans)
{
	const struct got_error *error;
	enum kcgi_err kerr;

	/* catch early querystring errors */
	if (gw_trans->error)
		gw_trans->action = GW_ERR;

	error = gw_display_open(gw_trans, KHTTP_200, gw_trans->mime);
	if (error)
		return error;

	kerr = khtml_open(gw_trans->gw_html_req, gw_trans->gw_req, 0);
	if (kerr != KCGI_OK)
		return gw_kcgi_error(kerr);

	if (gw_trans->action != GW_BLOB) {
		kerr = khttp_template(gw_trans->gw_req, gw_trans->gw_tmpl,
		    gw_query_funcs[gw_trans->action].template);
		if (kerr != KCGI_OK) {
			khtml_close(gw_trans->gw_html_req);
			return gw_kcgi_error(kerr);
		}
	}

	return gw_kcgi_error(khtml_close(gw_trans->gw_html_req));
}

static const struct got_error *
gw_error(struct gw_trans *gw_trans)
{
	enum kcgi_err kerr;

	kerr = khtml_puts(gw_trans->gw_html_req, gw_trans->error->msg);

	return gw_kcgi_error(kerr);
}

static int
gw_template(size_t key, void *arg)
{
	const struct got_error *error = NULL;
	enum kcgi_err kerr;
	struct gw_trans *gw_trans = arg;
	char *img_src = NULL;

	switch (key) {
	case (TEMPL_HEAD):
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_META,
		    KATTR_NAME, "viewport",
		    KATTR_CONTENT, "initial-scale=.75, user-scalable=yes",
		    KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_META,
		    KATTR_CHARSET, "utf-8",
		    KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_META,
		    KATTR_NAME, "msapplication-TileColor",
		    KATTR_CONTENT, "#da532c", KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_META,
		    KATTR_NAME, "theme-color",
		    KATTR_CONTENT, "#ffffff", KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_LINK,
		    KATTR_REL, "apple-touch-icon", KATTR_SIZES, "180x180",
		    KATTR_HREF, "/apple-touch-icon.png", KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_LINK,
		    KATTR_REL, "icon", KATTR_TYPE, "image/png", KATTR_SIZES,
		    "32x32", KATTR_HREF, "/favicon-32x32.png", KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_LINK,
		    KATTR_REL, "icon", KATTR_TYPE, "image/png", KATTR_SIZES,
		    "16x16", KATTR_HREF, "/favicon-16x16.png", KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_LINK,
		    KATTR_REL, "manifest", KATTR_HREF, "/site.webmanifest",
		    KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_LINK,
		    KATTR_REL, "mask-icon", KATTR_HREF,
		    "/safari-pinned-tab.svg", KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_LINK,
		    KATTR_REL, "stylesheet", KATTR_TYPE, "text/css",
		    KATTR_HREF, "/gotweb.css", KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			return 0;
		break;
	case(TEMPL_HEADER):
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "got_link", KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
		    KATTR_HREF,  gw_trans->gw_conf->got_logo_url,
		    KATTR_TARGET, "_sotd", KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		if (asprintf(&img_src, "/%s",
		    gw_trans->gw_conf->got_logo) == -1)
			return 0;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_IMG,
		    KATTR_SRC, img_src, KATTR__MAX);
		if (kerr != KCGI_OK) {
			free(img_src);
			return 0;
		}
		kerr = khtml_closeelem(gw_trans->gw_html_req, 3);
		if (kerr != KCGI_OK) {
			free(img_src);
			return 0;
		}
		break;
	case (TEMPL_SITEPATH):
		error = gw_output_site_link(gw_trans);
		if (error)
			return 0;
		break;
	case(TEMPL_TITLE):
		if (gw_trans->gw_conf->got_site_name != NULL) {
			kerr = khtml_puts(gw_trans->gw_html_req,
			    gw_trans->gw_conf->got_site_name);
			if (kerr != KCGI_OK)
				return 0;
		}
		break;
	case (TEMPL_SEARCH):
		break;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "search", KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_FORM,
			    KATTR_METHOD, "POST", KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_INPUT, KATTR_ID,
		    "got-search", KATTR_NAME, "got-search", KATTR_SIZE, "15",
		    KATTR_MAXLENGTH, "50", KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_BUTTON,
		    KATTR__MAX);
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_puts(gw_trans->gw_html_req, "Search");
		if (kerr != KCGI_OK)
			return 0;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 4);
		if (kerr != KCGI_OK)
			return 0;
		break;
	case(TEMPL_SITEOWNER):
		if (gw_trans->gw_conf->got_site_owner != NULL &&
		    gw_trans->gw_conf->got_show_site_owner) {
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "site_owner_wrapper", KATTR__MAX);
			if (kerr != KCGI_OK)
				return 0;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "site_owner", KATTR__MAX);
			if (kerr != KCGI_OK)
				return 0;
			kerr = khtml_puts(gw_trans->gw_html_req,
			    gw_trans->gw_conf->got_site_owner);
			kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
			if (kerr != KCGI_OK)
				return 0;
		}
		break;
	case(TEMPL_CONTENT):
		error = gw_query_funcs[gw_trans->action].func_main(gw_trans);
		if (error) {
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tmpl_err", KATTR__MAX);
			if (kerr != KCGI_OK)
				return 0;
			kerr = khttp_puts(gw_trans->gw_req, "Error: ");
			if (kerr != KCGI_OK)
				return 0;
			kerr = khttp_puts(gw_trans->gw_req, error->msg);
			if (kerr != KCGI_OK)
				return 0;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				return 0;
		}
		break;
	default:
		return 0;
	}
	return 1;
}

static const struct got_error *
gw_gen_commit_header(struct gw_trans *gw_trans, char *str1, char *str2)
{
	const struct got_error *error = NULL;
	enum kcgi_err kerr = KCGI_OK;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "header_commit_title", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, "Commit: ");
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "header_commit", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, str1);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, " ");
	if (kerr != KCGI_OK)
		goto done;
	if (str2 != NULL) {
		kerr = khtml_puts(gw_trans->gw_html_req, "(");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, str2);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, ")");
		if (kerr != KCGI_OK)
			goto done;
	}
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
done:
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_gen_diff_header(struct gw_trans *gw_trans, char *str1, char *str2)
{
	const struct got_error *error = NULL;
	enum kcgi_err kerr = KCGI_OK;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "header_diff_title", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, "Diff: ");
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "header_diff", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	if (str1 != NULL) {
		kerr = khtml_puts(gw_trans->gw_html_req, str1);
		if (kerr != KCGI_OK)
			goto done;
	}
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_BR, KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, str2);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
done:
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_gen_age_header(struct gw_trans *gw_trans, const char *str)
{
	const struct got_error *error = NULL;
	enum kcgi_err kerr = KCGI_OK;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "header_age_title", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, "Date: ");
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "header_age", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, str);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
done:
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_gen_author_header(struct gw_trans *gw_trans, const char *str)
{
	const struct got_error *error = NULL;
	enum kcgi_err kerr;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "header_author_title", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, "Author: ");
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "header_author", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, str);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
done:
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_gen_committer_header(struct gw_trans *gw_trans, const char *str)
{
	const struct got_error *error = NULL;
	enum kcgi_err kerr;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "header_committer_title", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, "Committer: ");
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "header_committer", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, str);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
done:
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_gen_commit_msg_header(struct gw_trans *gw_trans, char *str)
{
	const struct got_error *error = NULL;
	enum kcgi_err kerr;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "header_commit_msg_title", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, "Message: ");
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "header_commit_msg", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khttp_puts(gw_trans->gw_req, str);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
done:
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_gen_tree_header(struct gw_trans *gw_trans, char *str)
{
	const struct got_error *error = NULL;
	enum kcgi_err kerr;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "header_tree_title", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, "Tree: ");
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "header_tree", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, str);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
done:
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_get_repo_description(char **description, struct gw_trans *gw_trans,
    char *dir)
{
	const struct got_error *error = NULL;
	FILE *f = NULL;
	char *d_file = NULL;
	unsigned int len;
	size_t n;

	*description = NULL;
	if (gw_trans->gw_conf->got_show_repo_description == 0)
		return NULL;

	if (asprintf(&d_file, "%s/description", dir) == -1)
		return got_error_from_errno("asprintf");

	f = fopen(d_file, "r");
	if (f == NULL) {
		if (errno == ENOENT || errno == EACCES)
			return NULL;
		error = got_error_from_errno2("fopen", d_file);
		goto done;
	}

	if (fseek(f, 0, SEEK_END) == -1) {
		error = got_ferror(f, GOT_ERR_IO);
		goto done;
	}
	len = ftell(f);
	if (len == -1) {
		error = got_ferror(f, GOT_ERR_IO);
		goto done;
	}
	if (fseek(f, 0, SEEK_SET) == -1) {
		error = got_ferror(f, GOT_ERR_IO);
		goto done;
	}
	*description = calloc(len + 1, sizeof(**description));
	if (*description == NULL) {
		error = got_error_from_errno("calloc");
		goto done;
	}

	n = fread(*description, 1, len, f);
	if (n == 0 && ferror(f))
		error = got_ferror(f, GOT_ERR_IO);
done:
	if (f != NULL && fclose(f) == -1 && error == NULL)
		error = got_error_from_errno("fclose");
	free(d_file);
	return error;
}

static const struct got_error *
gw_get_time_str(char **repo_age, time_t committer_time, int ref_tm)
{
	struct tm tm;
	time_t diff_time;
	char *years = "years ago", *months = "months ago";
	char *weeks = "weeks ago", *days = "days ago", *hours = "hours ago";
	char *minutes = "minutes ago", *seconds = "seconds ago";
	char *now = "right now";
	char *s;
	char datebuf[29];

	*repo_age = NULL;

	switch (ref_tm) {
	case TM_DIFF:
		diff_time = time(NULL) - committer_time;
		if (diff_time > 60 * 60 * 24 * 365 * 2) {
			if (asprintf(repo_age, "%lld %s",
			    (diff_time / 60 / 60 / 24 / 365), years) == -1)
				return got_error_from_errno("asprintf");
		} else if (diff_time > 60 * 60 * 24 * (365 / 12) * 2) {
			if (asprintf(repo_age, "%lld %s",
			    (diff_time / 60 / 60 / 24 / (365 / 12)),
			    months) == -1)
				return got_error_from_errno("asprintf");
		} else if (diff_time > 60 * 60 * 24 * 7 * 2) {
			if (asprintf(repo_age, "%lld %s",
			    (diff_time / 60 / 60 / 24 / 7), weeks) == -1)
				return got_error_from_errno("asprintf");
		} else if (diff_time > 60 * 60 * 24 * 2) {
			if (asprintf(repo_age, "%lld %s",
			    (diff_time / 60 / 60 / 24), days) == -1)
				return got_error_from_errno("asprintf");
		} else if (diff_time > 60 * 60 * 2) {
			if (asprintf(repo_age, "%lld %s",
			    (diff_time / 60 / 60), hours) == -1)
				return got_error_from_errno("asprintf");
		} else if (diff_time > 60 * 2) {
			if (asprintf(repo_age, "%lld %s", (diff_time / 60),
			    minutes) == -1)
				return got_error_from_errno("asprintf");
		} else if (diff_time > 2) {
			if (asprintf(repo_age, "%lld %s", diff_time,
			    seconds) == -1)
				return got_error_from_errno("asprintf");
		} else {
			if (asprintf(repo_age, "%s", now) == -1)
				return got_error_from_errno("asprintf");
		}
		break;
	case TM_LONG:
		if (gmtime_r(&committer_time, &tm) == NULL)
			return got_error_from_errno("gmtime_r");

		s = asctime_r(&tm, datebuf);
		if (s == NULL)
			return got_error_from_errno("asctime_r");

		if (asprintf(repo_age, "%s UTC", datebuf) == -1)
			return got_error_from_errno("asprintf");
		break;
	}
	return NULL;
}

static const struct got_error *
gw_get_repo_age(char **repo_age, struct gw_trans *gw_trans, char *dir,
    char *repo_ref, int ref_tm)
{
	const struct got_error *error = NULL;
	struct got_object_id *id = NULL;
	struct got_repository *repo = NULL;
	struct got_commit_object *commit = NULL;
	struct got_reflist_head refs;
	struct got_reflist_entry *re;
	struct got_reference *head_ref;
	int is_head = 0;
	time_t committer_time = 0, cmp_time = 0;
	const char *refname;

	*repo_age = NULL;
	SIMPLEQ_INIT(&refs);

	if (repo_ref == NULL)
		return NULL;

	if (strncmp(repo_ref, "refs/heads/", 11) == 0)
		is_head = 1;

	if (gw_trans->gw_conf->got_show_repo_age == 0)
		return NULL;

	error = got_repo_open(&repo, dir, NULL);
	if (error)
		goto done;

	if (is_head)
		error = got_ref_list(&refs, repo, "refs/heads",
		    got_ref_cmp_by_name, NULL);
	else
		error = got_ref_list(&refs, repo, repo_ref,
		    got_ref_cmp_by_name, NULL);
	if (error)
		goto done;

	SIMPLEQ_FOREACH(re, &refs, entry) {
		if (is_head) {
			refname = strdup(repo_ref);
			if (refname == NULL) {
				error = got_error_from_errno("strdup");
				goto done;
			}
		} else {
			refname = got_ref_get_name(re->ref);
			if (refname == NULL) {
				error = got_error_from_errno("strdup");
				goto done;
			}
		}
		error = got_ref_open(&head_ref, repo, refname, 0);
		if (error)
			goto done;

		error = got_ref_resolve(&id, repo, head_ref);
		got_ref_close(head_ref);
		if (error)
			goto done;

		error = got_object_open_as_commit(&commit, repo, id);
		if (error)
			goto done;

		committer_time =
		    got_object_commit_get_committer_time(commit);

		if (cmp_time < committer_time)
			cmp_time = committer_time;
	}

	if (cmp_time != 0) {
		committer_time = cmp_time;
		error = gw_get_time_str(repo_age, committer_time, ref_tm);
	}
done:
	got_ref_list_free(&refs);
	free(id);
	return error;
}

static const struct got_error *
gw_output_diff(struct gw_trans *gw_trans, struct gw_header *header)
{
	const struct got_error *error;
	FILE *f = NULL;
	struct got_object_id *id1 = NULL, *id2 = NULL;
	char *label1 = NULL, *label2 = NULL, *line = NULL;
	int obj_type;
	size_t linesize = 0;
	ssize_t linelen;
	enum kcgi_err kerr = KCGI_OK;

	f = got_opentemp();
	if (f == NULL)
		return NULL;

	error = got_repo_open(&header->repo, gw_trans->repo_path, NULL);
	if (error)
		goto done;

	if (header->parent_id != NULL &&
	    strncmp(header->parent_id, "/dev/null", 9) != 0) {
		error = got_repo_match_object_id(&id1, &label1,
			header->parent_id, GOT_OBJ_TYPE_ANY, 1, header->repo);
		if (error)
			goto done;
	}

	error = got_repo_match_object_id(&id2, &label2,
	    header->commit_id, GOT_OBJ_TYPE_ANY, 1, header->repo);
	if (error)
		goto done;

	error = got_object_get_type(&obj_type, header->repo, id2);
	if (error)
		goto done;
	switch (obj_type) {
	case GOT_OBJ_TYPE_BLOB:
		error = got_diff_objects_as_blobs(id1, id2, NULL, NULL, 3, 0,
		    header->repo, f);
		break;
	case GOT_OBJ_TYPE_TREE:
		error = got_diff_objects_as_trees(id1, id2, "", "", 3, 0,
		    header->repo, f);
		break;
	case GOT_OBJ_TYPE_COMMIT:
		error = got_diff_objects_as_commits(id1, id2, 3, 0,
		    header->repo, f);
		break;
	default:
		error = got_error(GOT_ERR_OBJ_TYPE);
	}
	if (error)
		goto done;

	if (fseek(f, 0, SEEK_SET) == -1) {
		error = got_ferror(f, GOT_ERR_IO);
		goto done;
	}

	while ((linelen = getline(&line, &linesize, f)) != -1) {
		error = gw_colordiff_line(gw_trans, line);
		if (error)
			goto done;
		/* XXX: KHTML_PRETTY breaks this */
		kerr = khtml_puts(gw_trans->gw_html_req, line);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;
	}
	if (linelen == -1 && ferror(f))
		error = got_error_from_errno("getline");
done:
	if (f && fclose(f) == -1 && error == NULL)
		error = got_error_from_errno("fclose");
	free(line);
	free(label1);
	free(label2);
	free(id1);
	free(id2);

	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_get_repo_owner(char **owner, struct gw_trans *gw_trans, char *dir)
{
	const struct got_error *error = NULL;
	struct got_repository *repo;
	const char *gitconfig_owner;

	*owner = NULL;

	if (gw_trans->gw_conf->got_show_repo_owner == 0)
		return NULL;

	error = got_repo_open(&repo, dir, NULL);
	if (error)
		return error;
	gitconfig_owner = got_repo_get_gitconfig_owner(repo);
	if (gitconfig_owner) {
		*owner = strdup(gitconfig_owner);
		if (*owner == NULL)
			error = got_error_from_errno("strdup");
	}
	got_repo_close(repo);
	return error;
}

static const struct got_error *
gw_get_clone_url(char **url, struct gw_trans *gw_trans, char *dir)
{
	const struct got_error *error = NULL;
	FILE *f;
	char *d_file = NULL;
	unsigned int len;
	size_t n;

	*url = NULL;

	if (asprintf(&d_file, "%s/cloneurl", dir) == -1)
		return got_error_from_errno("asprintf");

	f = fopen(d_file, "r");
	if (f == NULL) {
		if (errno != ENOENT && errno != EACCES)
			error = got_error_from_errno2("fopen", d_file);
		goto done;
	}

	if (fseek(f, 0, SEEK_END) == -1) {
		error = got_ferror(f, GOT_ERR_IO);
		goto done;
	}
	len = ftell(f);
	if (len == -1) {
		error = got_ferror(f, GOT_ERR_IO);
		goto done;
	}
	if (fseek(f, 0, SEEK_SET) == -1) {
		error = got_ferror(f, GOT_ERR_IO);
		goto done;
	}

	*url = calloc(len + 1, sizeof(**url));
	if (*url == NULL) {
		error = got_error_from_errno("calloc");
		goto done;
	}

	n = fread(*url, 1, len, f);
	if (n == 0 && ferror(f))
		error = got_ferror(f, GOT_ERR_IO);
done:
	if (f && fclose(f) == -1 && error == NULL)
		error = got_error_from_errno("fclose");
	free(d_file);
	return NULL;
}

static const struct got_error *
gw_output_repo_tags(struct gw_trans *gw_trans, struct gw_header *header,
    int limit, int tag_type)
{
	const struct got_error *error = NULL;
	struct got_repository *repo = NULL;
	struct got_reflist_head refs;
	struct got_reflist_entry *re;
	char *age = NULL;
	char *id_str = NULL, *newline, *href_commits = NULL;
	char *tag_commit0 = NULL, *href_tag = NULL, *href_briefs = NULL;
	struct got_tag_object *tag = NULL;
	enum kcgi_err kerr = KCGI_OK;
	int summary_header_displayed = 0;

	SIMPLEQ_INIT(&refs);

	error = got_repo_open(&repo, gw_trans->repo_path, NULL);
	if (error)
		return error;

	error = got_ref_list(&refs, repo, "refs/tags", got_ref_cmp_tags, repo);
	if (error)
		goto done;

	SIMPLEQ_FOREACH(re, &refs, entry) {
		const char *refname;
		const char *tagger;
		const char *tag_commit;
		time_t tagger_time;
		struct got_object_id *id;
		struct got_commit_object *commit = NULL;

		refname = got_ref_get_name(re->ref);
		if (strncmp(refname, "refs/tags/", 10) != 0)
			continue;
		refname += 10;

		error = got_ref_resolve(&id, repo, re->ref);
		if (error)
			goto done;

		error = got_object_open_as_tag(&tag, repo, id);
		if (error) {
			if (error->code != GOT_ERR_OBJ_TYPE) {
				free(id);
				goto done;
			}
			/* "lightweight" tag */
			error = got_object_open_as_commit(&commit, repo, id);
			if (error) {
				free(id);
				goto done;
			}
			tagger = got_object_commit_get_committer(commit);
			tagger_time =
			    got_object_commit_get_committer_time(commit);
			error = got_object_id_str(&id_str, id);
			free(id);
		} else {
			free(id);
			tagger = got_object_tag_get_tagger(tag);
			tagger_time = got_object_tag_get_tagger_time(tag);
			error = got_object_id_str(&id_str,
			    got_object_tag_get_object_id(tag));
		}
		if (error)
			goto done;

		if (tag_type == TAGFULL && strncmp(id_str, header->commit_id,
		    strlen(id_str)) != 0)
			continue;

		if (commit) {
			error = got_object_commit_get_logmsg(&tag_commit0,
			    commit);
			if (error)
				goto done;
			got_object_commit_close(commit);
		} else {
			tag_commit0 = strdup(got_object_tag_get_message(tag));
			if (tag_commit0 == NULL) {
				error = got_error_from_errno("strdup");
				goto done;
			}
		}

		tag_commit = tag_commit0;
		while (*tag_commit == '\n')
			tag_commit++;

		switch (tag_type) {
		case TAGBRIEF:
			newline = strchr(tag_commit, '\n');
			if (newline)
				*newline = '\0';

			if (summary_header_displayed == 0) {
				kerr = khtml_attr(gw_trans->gw_html_req,
				    KELEM_DIV, KATTR_ID,
				    "summary_tags_title_wrapper", KATTR__MAX);
				if (kerr != KCGI_OK)
					goto done;
				kerr = khtml_attr(gw_trans->gw_html_req,
				    KELEM_DIV, KATTR_ID,
				    "summary_tags_title", KATTR__MAX);
				if (kerr != KCGI_OK)
					goto done;
				kerr = khtml_puts(gw_trans->gw_html_req,
				    "Tags");
				if (kerr != KCGI_OK)
					goto done;
				kerr = khtml_closeelem(gw_trans->gw_html_req,
				    2);
				if (kerr != KCGI_OK)
					goto done;
				kerr = khtml_attr(gw_trans->gw_html_req,
				    KELEM_DIV, KATTR_ID,
				    "summary_tags_content", KATTR__MAX);
				if (kerr != KCGI_OK)
					goto done;
				summary_header_displayed = 1;
			}

			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tags_wrapper", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tags_age", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			error = gw_get_time_str(&age, tagger_time, TM_DIFF);
			if (error)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req,
			    age ? age : "");
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tags", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req, refname);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tags_name", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			if (asprintf(&href_tag, "?path=%s&action=tag&commit=%s",
			    gw_trans->repo_name, id_str) == -1) {
				error = got_error_from_errno("asprintf");
				goto done;
			}
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
			    KATTR_HREF, href_tag, KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req, tag_commit);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 3);
			if (kerr != KCGI_OK)
				goto done;

			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "navs_wrapper", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "navs", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;

			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
			    KATTR_HREF, href_tag, KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req, "tag");
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				goto done;

			kerr = khtml_puts(gw_trans->gw_html_req, " | ");
			if (kerr != KCGI_OK)
				goto done;
			if (asprintf(&href_briefs,
			    "?path=%s&action=briefs&commit=%s",
			    gw_trans->repo_name, id_str) == -1) {
				error = got_error_from_errno("asprintf");
				goto done;
			}
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
			    KATTR_HREF, href_briefs, KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req,
			    "commit briefs");
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				goto done;

			kerr = khtml_puts(gw_trans->gw_html_req, " | ");
			if (kerr != KCGI_OK)
				goto done;

			if (asprintf(&href_commits,
			    "?path=%s&action=commits&commit=%s",
			    gw_trans->repo_name, id_str) == -1) {
				error = got_error_from_errno("asprintf");
				goto done;
			}
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
			    KATTR_HREF, href_commits, KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req,
			    "commits");
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 3);
			if (kerr != KCGI_OK)
				goto done;

			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "dotted_line", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
			if (kerr != KCGI_OK)
				goto done;
			break;
		case TAGFULL:
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tag_info_date_title", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req, "Tag Date:");
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tag_info_date", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			error = gw_get_time_str(&age, tagger_time, TM_LONG);
			if (error)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req,
			    age ? age : "");
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				goto done;

			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tag_info_tagger_title", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req, "Tagger:");
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tag_info_date", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req, tagger);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				goto done;

			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tag_info", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khttp_puts(gw_trans->gw_req, tag_commit);
			if (kerr != KCGI_OK)
				goto done;
			break;
		default:
			break;
		}
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;

		if (limit && --limit == 0)
			break;

		if (tag)
			got_object_tag_close(tag);
		tag = NULL;
		free(id_str);
		id_str = NULL;
		free(age);
		age = NULL;
		free(tag_commit0);
		tag_commit0 = NULL;
		free(href_tag);
		href_tag = NULL;
		free(href_briefs);
		href_briefs = NULL;
		free(href_commits);
		href_commits = NULL;
	}
done:
	if (tag)
		got_object_tag_close(tag);
	free(id_str);
	free(age);
	free(tag_commit0);
	free(href_tag);
	free(href_briefs);
	free(href_commits);
	got_ref_list_free(&refs);
	if (repo)
		got_repo_close(repo);
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static void
gw_free_headers(struct gw_header *header)
{
	free(header->id);
	free(header->path);
	if (header->commit != NULL)
		got_object_commit_close(header->commit);
	if (header->repo)
		got_repo_close(header->repo);
	free(header->refs_str);
	free(header->commit_id);
	free(header->parent_id);
	free(header->tree_id);
	free(header->commit_msg);
}

static struct gw_header *
gw_init_header()
{
	struct gw_header *header;

	header = malloc(sizeof(*header));
	if (header == NULL)
		return NULL;

	header->repo = NULL;
	header->commit = NULL;
	header->id = NULL;
	header->path = NULL;
	SIMPLEQ_INIT(&header->refs);

	header->refs_str = NULL;
	header->commit_id = NULL;
	header->parent_id = NULL;
	header->tree_id = NULL;
	header->commit_msg = NULL;

	return header;
}

static const struct got_error *
gw_get_commits(struct gw_trans * gw_trans, struct gw_header *header,
    int limit)
{
	const struct got_error *error = NULL;
	struct got_commit_graph *graph = NULL;

	error = got_commit_graph_open(&graph, header->path, 0);
	if (error)
		return error;

	error = got_commit_graph_iter_start(graph, header->id, header->repo,
	    NULL, NULL);
	if (error)
		goto done;

	for (;;) {
		error = got_commit_graph_iter_next(&header->id, graph,
		    header->repo, NULL, NULL);
		if (error) {
			if (error->code == GOT_ERR_ITER_COMPLETED)
				error = NULL;
			goto done;
		}
		if (header->id == NULL)
			goto done;

		error = got_object_open_as_commit(&header->commit, header->repo,
		    header->id);
		if (error)
			goto done;

		error = gw_get_commit(gw_trans, header);
		if (limit > 1) {
			struct gw_header *n_header = NULL;
			if ((n_header = gw_init_header()) == NULL) {
				error = got_error_from_errno("malloc");
				goto done;
			}

			if (header->refs_str != NULL) {
				n_header->refs_str = strdup(header->refs_str);
				if (n_header->refs_str == NULL) {
					error = got_error_from_errno("strdup");
					goto done;
				}
			}
			n_header->commit_id = strdup(header->commit_id);
			if (n_header->commit_id == NULL) {
				error = got_error_from_errno("strdup");
				goto done;
			}
			if (header->parent_id != NULL) {
				n_header->parent_id = strdup(header->parent_id);
				if (n_header->parent_id == NULL) {
					error = got_error_from_errno("strdup");
					goto done;
				}
			}
			n_header->tree_id = strdup(header->tree_id);
			if (n_header->tree_id == NULL) {
				error = got_error_from_errno("strdup");
				goto done;
			}
			n_header->author = header->author;
			n_header->committer = header->committer;
			n_header->commit_msg = strdup(header->commit_msg);
			if (n_header->commit_msg == NULL) {
				error = got_error_from_errno("strdup");
				goto done;
			}
			n_header->committer_time = header->committer_time;
			TAILQ_INSERT_TAIL(&gw_trans->gw_headers, n_header,
			    entry);
		}
		if (error || (limit && --limit == 0))
			break;
	}
done:
	if (graph)
		got_commit_graph_close(graph);
	return error;
}

static const struct got_error *
gw_get_commit(struct gw_trans *gw_trans, struct gw_header *header)
{
	const struct got_error *error = NULL;
	struct got_reflist_entry *re;
	struct got_object_id *id2 = NULL;
	struct got_object_qid *parent_id;
	char *commit_msg = NULL, *commit_msg0;

	/*print commit*/
	SIMPLEQ_FOREACH(re, &header->refs, entry) {
		char *s;
		const char *name;
		struct got_tag_object *tag = NULL;
		int cmp;

		name = got_ref_get_name(re->ref);
		if (strcmp(name, GOT_REF_HEAD) == 0)
			continue;
		if (strncmp(name, "refs/", 5) == 0)
			name += 5;
		if (strncmp(name, "got/", 4) == 0)
			continue;
		if (strncmp(name, "heads/", 6) == 0)
			name += 6;
		if (strncmp(name, "remotes/", 8) == 0)
			name += 8;
		if (strncmp(name, "tags/", 5) == 0) {
			error = got_object_open_as_tag(&tag, header->repo,
			    re->id);
			if (error) {
				if (error->code != GOT_ERR_OBJ_TYPE)
					continue;
				/*
				 * Ref points at something other
				 * than a tag.
				 */
				error = NULL;
				tag = NULL;
			}
		}
		cmp = got_object_id_cmp(tag ?
		    got_object_tag_get_object_id(tag) : re->id, header->id);
		if (tag)
			got_object_tag_close(tag);
		if (cmp != 0)
			continue;
		s = header->refs_str;
		if (asprintf(&header->refs_str, "%s%s%s", s ? s : "",
		    s ? ", " : "", name) == -1) {
			error = got_error_from_errno("asprintf");
			free(s);
			header->refs_str = NULL;
			return error;
		}
		free(s);
	}

	error = got_object_id_str(&header->commit_id, header->id);
	if (error)
		return error;

	error = got_object_id_str(&header->tree_id,
	    got_object_commit_get_tree_id(header->commit));
	if (error)
		return error;

	if (gw_trans->action == GW_DIFF) {
		parent_id = SIMPLEQ_FIRST(
		    got_object_commit_get_parent_ids(header->commit));
		if (parent_id != NULL) {
			id2 = got_object_id_dup(parent_id->id);
			free (parent_id);
			error = got_object_id_str(&header->parent_id, id2);
			if (error)
				return error;
			free(id2);
		} else {
			header->parent_id = strdup("/dev/null");
			if (header->parent_id == NULL) {
				error = got_error_from_errno("strdup");
				return error;
			}
		}
	}

	header->committer_time =
	    got_object_commit_get_committer_time(header->commit);

	header->author =
	    got_object_commit_get_author(header->commit);
	header->committer =
	    got_object_commit_get_committer(header->commit);
	if (error)
		return error;
	error = got_object_commit_get_logmsg(&commit_msg0, header->commit);
	if (error)
		return error;

	commit_msg = commit_msg0;
	while (*commit_msg == '\n')
		commit_msg++;

	header->commit_msg = strdup(commit_msg);
	if (header->commit_msg == NULL)
		error = got_error_from_errno("strdup");
	free(commit_msg0);
	return error;
}

static const struct got_error *
gw_get_header(struct gw_trans *gw_trans, struct gw_header *header, int limit)
{
	const struct got_error *error = NULL;
	char *in_repo_path = NULL;

	error = got_repo_open(&header->repo, gw_trans->repo_path, NULL);
	if (error)
		return error;

	if (gw_trans->commit == NULL) {
		struct got_reference *head_ref;
		error = got_ref_open(&head_ref, header->repo,
		    gw_trans->headref, 0);
		if (error)
			return error;

		error = got_ref_resolve(&header->id, header->repo, head_ref);
		got_ref_close(head_ref);
		if (error)
			return error;

		error = got_object_open_as_commit(&header->commit,
		    header->repo, header->id);
	} else {
		struct got_reference *ref;
		error = got_ref_open(&ref, header->repo, gw_trans->commit, 0);
		if (error == NULL) {
			int obj_type;
			error = got_ref_resolve(&header->id, header->repo, ref);
			got_ref_close(ref);
			if (error)
				return error;
			error = got_object_get_type(&obj_type, header->repo,
			    header->id);
			if (error)
				return error;
			if (obj_type == GOT_OBJ_TYPE_TAG) {
				struct got_tag_object *tag;
				error = got_object_open_as_tag(&tag,
				    header->repo, header->id);
				if (error)
					return error;
				if (got_object_tag_get_object_type(tag) !=
				    GOT_OBJ_TYPE_COMMIT) {
					got_object_tag_close(tag);
					error = got_error(GOT_ERR_OBJ_TYPE);
					return error;
				}
				free(header->id);
				header->id = got_object_id_dup(
				    got_object_tag_get_object_id(tag));
				if (header->id == NULL)
					error = got_error_from_errno(
					    "got_object_id_dup");
				got_object_tag_close(tag);
				if (error)
					return error;
			} else if (obj_type != GOT_OBJ_TYPE_COMMIT) {
				error = got_error(GOT_ERR_OBJ_TYPE);
				return error;
			}
			error = got_object_open_as_commit(&header->commit,
			    header->repo, header->id);
			if (error)
				return error;
		}
		if (header->commit == NULL) {
			error = got_repo_match_object_id_prefix(&header->id,
			    gw_trans->commit, GOT_OBJ_TYPE_COMMIT,
			    header->repo);
			if (error)
				return error;
		}
		error = got_repo_match_object_id_prefix(&header->id,
			    gw_trans->commit, GOT_OBJ_TYPE_COMMIT,
			    header->repo);
	}

	error = got_repo_map_path(&in_repo_path, header->repo,
	    gw_trans->repo_path, 1);
	if (error)
		return error;

	if (in_repo_path) {
		header->path = strdup(in_repo_path);
		if (header->path == NULL) {
			error = got_error_from_errno("strdup");
			free(in_repo_path);
			return error;
		}
	}
	free(in_repo_path);

	error = got_ref_list(&header->refs, header->repo, NULL,
	    got_ref_cmp_by_name, NULL);
	if (error)
		return error;

	error = gw_get_commits(gw_trans, header, limit);
	return error;
}

struct blame_line {
	int annotated;
	char *id_str;
	char *committer;
	char datebuf[11]; /* YYYY-MM-DD + NUL */
};

struct gw_blame_cb_args {
	struct blame_line *lines;
	int nlines;
	int nlines_prec;
	int lineno_cur;
	off_t *line_offsets;
	FILE *f;
	struct got_repository *repo;
	struct gw_trans *gw_trans;
};

static const struct got_error *
gw_blame_cb(void *arg, int nlines, int lineno, struct got_object_id *id)
{
	const struct got_error *err = NULL;
	struct gw_blame_cb_args *a = arg;
	struct blame_line *bline;
	char *line = NULL;
	size_t linesize = 0;
	struct got_commit_object *commit = NULL;
	off_t offset;
	struct tm tm;
	time_t committer_time;
	enum kcgi_err kerr = KCGI_OK;

	if (nlines != a->nlines ||
	    (lineno != -1 && lineno < 1) || lineno > a->nlines)
		return got_error(GOT_ERR_RANGE);

	if (lineno == -1)
		return NULL; /* no change in this commit */

	/* Annotate this line. */
	bline = &a->lines[lineno - 1];
	if (bline->annotated)
		return NULL;
	err = got_object_id_str(&bline->id_str, id);
	if (err)
		return err;

	err = got_object_open_as_commit(&commit, a->repo, id);
	if (err)
		goto done;

	bline->committer = strdup(got_object_commit_get_committer(commit));
	if (bline->committer == NULL) {
		err = got_error_from_errno("strdup");
		goto done;
	}

	committer_time = got_object_commit_get_committer_time(commit);
	if (localtime_r(&committer_time, &tm) == NULL)
		return got_error_from_errno("localtime_r");
	if (strftime(bline->datebuf, sizeof(bline->datebuf), "%G-%m-%d",
	    &tm) >= sizeof(bline->datebuf)) {
		err = got_error(GOT_ERR_NO_SPACE);
		goto done;
	}
	bline->annotated = 1;

	/* Print lines annotated so far. */
	bline = &a->lines[a->lineno_cur - 1];
	if (!bline->annotated)
		goto done;

	offset = a->line_offsets[a->lineno_cur - 1];
	if (fseeko(a->f, offset, SEEK_SET) == -1) {
		err = got_error_from_errno("fseeko");
		goto done;
	}

	while (bline->annotated) {
		char *smallerthan, *at, *nl, *committer;
		char *lineno = NULL, *href_diff = NULL, *href_link = NULL;
		size_t len;

		if (getline(&line, &linesize, a->f) == -1) {
			if (ferror(a->f))
				err = got_error_from_errno("getline");
			break;
		}

		committer = bline->committer;
		smallerthan = strchr(committer, '<');
		if (smallerthan && smallerthan[1] != '\0')
			committer = smallerthan + 1;
		at = strchr(committer, '@');
		if (at)
			*at = '\0';
		len = strlen(committer);
		if (len >= 9)
			committer[8] = '\0';

		nl = strchr(line, '\n');
		if (nl)
			*nl = '\0';

		kerr = khtml_attr(a->gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "blame_wrapper", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto err;
		kerr = khtml_attr(a->gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "blame_number", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto err;
		if (asprintf(&lineno, "%.*d", a->nlines_prec,
		    a->lineno_cur) == -1)
			goto err;
		kerr = khtml_puts(a->gw_trans->gw_html_req, lineno);
		if (kerr != KCGI_OK)
			goto err;
		kerr = khtml_closeelem(a->gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto err;

		kerr = khtml_attr(a->gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "blame_hash", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto err;
		/* XXX repo_file could be NULL if not present in querystring */
		if (asprintf(&href_diff,
		    "?path=%s&action=diff&commit=%s&file=%s&folder=%s",
		    a->gw_trans->repo_name, bline->id_str,
		    a->gw_trans->repo_file, a->gw_trans->repo_folder ?
		    a->gw_trans->repo_folder : "") == -1) {
			err = got_error_from_errno("asprintf");
			goto err;
		}
		if (asprintf(&href_link, "%.8s", bline->id_str) == -1) {
			err = got_error_from_errno("asprintf");
			goto err;
		}
		kerr = khtml_attr(a->gw_trans->gw_html_req, KELEM_A,
		    KATTR_HREF, href_diff, KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(a->gw_trans->gw_html_req, href_link);
		if (kerr != KCGI_OK)
			goto err;
		kerr = khtml_closeelem(a->gw_trans->gw_html_req, 2);
		if (kerr != KCGI_OK)
			goto err;

		kerr = khtml_attr(a->gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "blame_date", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto err;
		kerr = khtml_puts(a->gw_trans->gw_html_req, bline->datebuf);
		if (kerr != KCGI_OK)
			goto err;
		kerr = khtml_closeelem(a->gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto err;

		kerr = khtml_attr(a->gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "blame_author", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto err;
		kerr = khtml_puts(a->gw_trans->gw_html_req, committer);
		if (kerr != KCGI_OK)
			goto err;
		kerr = khtml_closeelem(a->gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto err;

		kerr = khtml_attr(a->gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "blame_code", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto err;
		kerr = khtml_puts(a->gw_trans->gw_html_req, line);
		if (kerr != KCGI_OK)
			goto err;
		kerr = khtml_closeelem(a->gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto err;

		kerr = khtml_closeelem(a->gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto err;

		a->lineno_cur++;
		bline = &a->lines[a->lineno_cur - 1];
err:
		free(lineno);
		free(href_diff);
		free(href_link);
	}
done:
	if (commit)
		got_object_commit_close(commit);
	free(line);
	if (err == NULL && kerr != KCGI_OK)
		err = gw_kcgi_error(kerr);
	return err;
}

static const struct got_error *
gw_output_file_blame(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	struct got_repository *repo = NULL;
	struct got_object_id *obj_id = NULL;
	struct got_object_id *commit_id = NULL;
	struct got_blob_object *blob = NULL;
	char *path = NULL, *in_repo_path = NULL;
	struct gw_blame_cb_args bca;
	int i, obj_type;
	size_t filesize;

	error = got_repo_open(&repo, gw_trans->repo_path, NULL);
	if (error)
		return error;

	/* XXX repo_file could be NULL if not present in querystring */
	if (asprintf(&path, "%s%s%s",
	    gw_trans->repo_folder ? gw_trans->repo_folder : "",
	    gw_trans->repo_folder ? "/" : "",
	    gw_trans->repo_file) == -1) {
		error = got_error_from_errno("asprintf");
		goto done;
	}

	error = got_repo_map_path(&in_repo_path, repo, path, 1);
	if (error)
		goto done;

	error = got_repo_match_object_id(&commit_id, NULL, gw_trans->commit,
	    GOT_OBJ_TYPE_COMMIT, 1, repo);
	if (error)
		goto done;

	error = got_object_id_by_path(&obj_id, repo, commit_id, in_repo_path);
	if (error)
		goto done;

	if (obj_id == NULL) {
		error = got_error(GOT_ERR_NO_OBJ);
		goto done;
	}

	error = got_object_get_type(&obj_type, repo, obj_id);
	if (error)
		goto done;

	if (obj_type != GOT_OBJ_TYPE_BLOB) {
		error = got_error(GOT_ERR_OBJ_TYPE);
		goto done;
	}

	error = got_object_open_as_blob(&blob, repo, obj_id, 8192);
	if (error)
		goto done;

	bca.f = got_opentemp();
	if (bca.f == NULL) {
		error = got_error_from_errno("got_opentemp");
		goto done;
	}
	error = got_object_blob_dump_to_file(&filesize, &bca.nlines,
	    &bca.line_offsets, bca.f, blob);
	if (error || bca.nlines == 0)
		goto done;

	/* Don't include \n at EOF in the blame line count. */
	if (bca.line_offsets[bca.nlines - 1] == filesize)
		bca.nlines--;

	bca.lines = calloc(bca.nlines, sizeof(*bca.lines));
	if (bca.lines == NULL) {
		error = got_error_from_errno("calloc");
		goto done;
	}
	bca.lineno_cur = 1;
	bca.nlines_prec = 0;
	i = bca.nlines;
	while (i > 0) {
		i /= 10;
		bca.nlines_prec++;
	}
	bca.repo = repo;
	bca.gw_trans = gw_trans;

	error = got_blame(in_repo_path, commit_id, repo, gw_blame_cb, &bca,
	    NULL, NULL);
done:
	free(bca.line_offsets);
	free(in_repo_path);
	free(commit_id);
	free(obj_id);
	free(path);

	for (i = 0; i < bca.nlines; i++) {
		struct blame_line *bline = &bca.lines[i];
		free(bline->id_str);
		free(bline->committer);
	}
	free(bca.lines);
	if (bca.f && fclose(bca.f) == EOF && error == NULL)
		error = got_error_from_errno("fclose");
	if (blob)
		got_object_blob_close(blob);
	if (repo)
		got_repo_close(repo);
	return error;
}

static const struct got_error *
gw_output_blob_buf(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	struct got_repository *repo = NULL;
	struct got_object_id *obj_id = NULL;
	struct got_object_id *commit_id = NULL;
	struct got_blob_object *blob = NULL;
	char *path = NULL, *in_repo_path = NULL;
	int obj_type, set_mime = 0;
	size_t len, hdrlen;
	const uint8_t *buf;
	enum kcgi_err kerr = KCGI_OK;

	error = got_repo_open(&repo, gw_trans->repo_path, NULL);
	if (error)
		return error;

	/* XXX repo_file could be NULL if not present in querystring */
	if (asprintf(&path, "%s%s%s",
	    gw_trans->repo_folder ? gw_trans->repo_folder : "",
	    gw_trans->repo_folder ? "/" : "",
	    gw_trans->repo_file) == -1) {
		error = got_error_from_errno("asprintf");
		goto done;
	}

	error = got_repo_map_path(&in_repo_path, repo, path, 1);
	if (error)
		goto done;

	error = got_repo_match_object_id(&commit_id, NULL, gw_trans->commit,
	    GOT_OBJ_TYPE_COMMIT, 1, repo);
	if (error)
		goto done;

	error = got_object_id_by_path(&obj_id, repo, commit_id, in_repo_path);
	if (error)
		goto done;

	if (obj_id == NULL) {
		error = got_error(GOT_ERR_NO_OBJ);
		goto done;
	}

	error = got_object_get_type(&obj_type, repo, obj_id);
	if (error)
		goto done;

	if (obj_type != GOT_OBJ_TYPE_BLOB) {
		error = got_error(GOT_ERR_OBJ_TYPE);
		goto done;
	}

	error = got_object_open_as_blob(&blob, repo, obj_id, 8192);
	if (error)
		goto done;

	hdrlen = got_object_blob_get_hdrlen(blob);
	do {
		error = got_object_blob_read_block(&len, blob);
		if (error)
			goto done;
		buf = got_object_blob_get_read_buf(blob);

		/*
		 * Skip blob object header first time around,
		 * which also contains a zero byte.
		 */
		buf += hdrlen;
		if (set_mime == 0) {
			if (isbinary(buf, len - hdrlen))
				gw_trans->mime = KMIME_APP_OCTET_STREAM;
			else
				gw_trans->mime = KMIME_TEXT_PLAIN;
			set_mime = 1;
			error = gw_display_index(gw_trans);
			if (error)
				goto done;
		}
		khttp_write(gw_trans->gw_req, buf, len - hdrlen);
		hdrlen = 0;
	} while (len != 0);
done:
	free(in_repo_path);
	free(commit_id);
	free(obj_id);
	free(path);
	if (blob)
		got_object_blob_close(blob);
	if (repo)
		got_repo_close(repo);
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_output_repo_tree(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	struct got_repository *repo = NULL;
	struct got_object_id *tree_id = NULL, *commit_id = NULL;
	struct got_tree_object *tree = NULL;
	char *path = NULL, *in_repo_path = NULL;
	char *id_str = NULL;
	char *build_folder = NULL;
	char *href_blob = NULL, *href_blame = NULL;
	const char *class = NULL;
	int nentries, i, class_flip = 0;
	enum kcgi_err kerr = KCGI_OK;

	error = got_repo_open(&repo, gw_trans->repo_path, NULL);
	if (error)
		return error;

	if (gw_trans->repo_folder != NULL) {
		path = strdup(gw_trans->repo_folder);
		if (path == NULL) {
			error = got_error_from_errno("strdup");
			goto done;
		}
	} else {
		error = got_repo_map_path(&in_repo_path, repo,
		    gw_trans->repo_path, 1);
		if (error)
			goto done;
		free(path);
		path = in_repo_path;
	}

	if (gw_trans->commit == NULL) {
		struct got_reference *head_ref;
		error = got_ref_open(&head_ref, repo, gw_trans->headref, 0);
		if (error)
			goto done;
		error = got_ref_resolve(&commit_id, repo, head_ref);
		if (error)
			goto done;
		got_ref_close(head_ref);

	} else {
		error = got_repo_match_object_id(&commit_id, NULL,
		    gw_trans->commit, GOT_OBJ_TYPE_COMMIT, 1, repo);
		if (error)
			goto done;
	}

	/*
	 * XXX gw_trans->commit might have already been allocated in
	 * gw_parse_querystring(); could we more cleanly seperate values
	 * we received as arguments from values we compute ourselves?
	 */
	error = got_object_id_str(&gw_trans->commit, commit_id);
	if (error)
		goto done;

	error = got_object_id_by_path(&tree_id, repo, commit_id, path);
	if (error)
		goto done;

	error = got_object_open_as_tree(&tree, repo, tree_id);
	if (error)
		goto done;

	nentries = got_object_tree_get_nentries(tree);
	for (i = 0; i < nentries; i++) {
		struct got_tree_entry *te;
		const char *modestr = "";
		mode_t mode;

		te = got_object_tree_get_entry(tree, i);

		error = got_object_id_str(&id_str, got_tree_entry_get_id(te));
		if (error)
			goto done;

		mode = got_tree_entry_get_mode(te);
		if (got_object_tree_entry_is_submodule(te))
			modestr = "$";
		else if (S_ISLNK(mode))
			modestr = "@";
		else if (S_ISDIR(mode))
			modestr = "/";
		else if (mode & S_IXUSR)
			modestr = "*";

		if (class_flip == 0) {
			class = "back_lightgray";
			class_flip = 1;
		} else {
			class = "back_white";
			class_flip = 0;
		}

		if (S_ISDIR(mode)) {
			if (asprintf(&build_folder, "%s/%s",
			    gw_trans->repo_folder ? gw_trans->repo_folder : "",
			    got_tree_entry_get_name(te)) == -1) {
				error = got_error_from_errno("asprintf");
				goto done;
			}
			if (asprintf(&href_blob,
			    "?path=%s&action=%s&commit=%s&folder=%s",
			    gw_trans->repo_name, gw_get_action_name(gw_trans),
			    gw_trans->commit, build_folder) == -1) {
				error = got_error_from_errno("asprintf");
				goto done;
			}

			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tree_wrapper", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tree_line", KATTR_CLASS, class,
			    KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
			    KATTR_HREF, href_blob, KATTR_CLASS,
			    "diff_directory", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req,
			    got_tree_entry_get_name(te));
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req, modestr);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tree_line_blank", KATTR_CLASS, class,
			    KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_entity(gw_trans->gw_html_req,
			    KENTITY_nbsp);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
			if (kerr != KCGI_OK)
				goto done;
		} else {
			if (asprintf(&href_blob,
			    "?path=%s&action=%s&commit=%s&file=%s&folder=%s",
			    gw_trans->repo_name, "blob", gw_trans->commit,
			    got_tree_entry_get_name(te),
			    gw_trans->repo_folder ?
			    gw_trans->repo_folder : "") == -1) {
				error = got_error_from_errno("asprintf");
				goto done;
			}
			if (asprintf(&href_blame,
			    "?path=%s&action=%s&commit=%s&file=%s&folder=%s",
			    gw_trans->repo_name, "blame", gw_trans->commit,
			    got_tree_entry_get_name(te),
			    gw_trans->repo_folder ?
			    gw_trans->repo_folder : "") == -1) {
				error = got_error_from_errno("asprintf");
				goto done;
			}

			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tree_wrapper", KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tree_line", KATTR_CLASS, class,
			    KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
			    KATTR_HREF, href_blob, KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req,
			    got_tree_entry_get_name(te));
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req, modestr);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
			    KATTR_ID, "tree_line_navs", KATTR_CLASS, class,
			    KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;

			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
			    KATTR_HREF, href_blob, KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req, "blob");
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
			if (kerr != KCGI_OK)
				goto done;

			kerr = khtml_puts(gw_trans->gw_html_req, " | ");
			if (kerr != KCGI_OK)
				goto done;

			kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A,
			    KATTR_HREF, href_blame, KATTR__MAX);
			if (kerr != KCGI_OK)
				goto done;
			kerr = khtml_puts(gw_trans->gw_html_req, "blame");
			if (kerr != KCGI_OK)
				goto done;

			kerr = khtml_closeelem(gw_trans->gw_html_req, 3);
			if (kerr != KCGI_OK)
				goto done;
		}
		free(id_str);
		id_str = NULL;
		free(href_blob);
		href_blob = NULL;
		free(build_folder);
		build_folder = NULL;
	}
done:
	if (tree)
		got_object_tree_close(tree);
	if (repo)
		got_repo_close(repo);
	free(id_str);
	free(href_blob);
	free(href_blame);
	free(in_repo_path);
	free(tree_id);
	free(build_folder);
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

static const struct got_error *
gw_output_repo_heads(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	struct got_repository *repo = NULL;
	struct got_reflist_head refs;
	struct got_reflist_entry *re;
	char *age = NULL, *href_summary = NULL, *href_briefs = NULL;
	char *href_commits = NULL;
	enum kcgi_err kerr = KCGI_OK;

	SIMPLEQ_INIT(&refs);

	error = got_repo_open(&repo, gw_trans->repo_path, NULL);
	if (error)
		goto done;

	error = got_ref_list(&refs, repo, "refs/heads", got_ref_cmp_by_name,
	    NULL);
	if (error)
		goto done;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "summary_heads_title_wrapper", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "summary_heads_title", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req, "Heads");
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
	    KATTR_ID, "summary_heads_content", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;

	SIMPLEQ_FOREACH(re, &refs, entry) {
		char *refname;

		refname = strdup(got_ref_get_name(re->ref));
		if (refname == NULL) {
			error = got_error_from_errno("got_ref_to_str");
			goto done;
		}

		if (strncmp(refname, "refs/heads/", 11) != 0) {
			free(refname);
			continue;
		}

		error = gw_get_repo_age(&age, gw_trans, gw_trans->gw_dir->path,
		    refname, TM_DIFF);
		if (error)
			goto done;

		if (strncmp(refname, "refs/heads/", 11) == 0)
			refname += 11;

		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "heads_wrapper", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "heads_age", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, age ? age : "");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "heads_space", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_entity(gw_trans->gw_html_req, KENTITY_nbsp);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV,
		    KATTR_ID, "head", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		if (asprintf(&href_summary,
		    "?path=%s&action=summary&headref=%s",
		    gw_trans->repo_name, refname) == -1) {
			error = got_error_from_errno("asprintf");
			goto done;
		}
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A, KATTR_HREF,
		    href_summary, KATTR__MAX);
		kerr = khtml_puts(gw_trans->gw_html_req, refname);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 3);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "navs_wrapper", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "navs", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A, KATTR_HREF,
		    href_summary, KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, "summary");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_puts(gw_trans->gw_html_req, " | ");
		if (kerr != KCGI_OK)
			goto done;
		if (asprintf(&href_briefs, "?path=%s&action=briefs&headref=%s",
		    gw_trans->repo_name, refname) == -1) {
			error = got_error_from_errno("asprintf");
			goto done;
		}
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A, KATTR_HREF,
		    href_briefs, KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, "commit briefs");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_puts(gw_trans->gw_html_req, " | ");
		if (kerr != KCGI_OK)
			goto done;

		if (asprintf(&href_commits,
		    "?path=%s&action=commits&headref=%s",
		    gw_trans->repo_name, refname) == -1) {
			error = got_error_from_errno("asprintf");
			goto done;
		}
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A, KATTR_HREF,
		    href_commits, KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, "commits");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 3);
		if (kerr != KCGI_OK)
			goto done;

		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
		    "dotted_line", KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 2);
		if (kerr != KCGI_OK)
			goto done;
		free(href_summary);
		href_summary = NULL;
		free(href_briefs);
		href_briefs = NULL;
		free(href_commits);
		href_commits = NULL;
	}
done:
	got_ref_list_free(&refs);
	free(href_summary);
	free(href_briefs);
	free(href_commits);
	if (repo)
		got_repo_close(repo);
	return error;
}

static const struct got_error *
gw_output_site_link(struct gw_trans *gw_trans)
{
	const struct got_error *error = NULL;
	char *href_summary = NULL;
	enum kcgi_err kerr = KCGI_OK;

	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "site_link", KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A, KATTR_HREF, GOTWEB,
	    KATTR__MAX);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_puts(gw_trans->gw_html_req,
	   gw_trans->gw_conf->got_site_link);
	if (kerr != KCGI_OK)
		goto done;
	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		goto done;

	if (gw_trans->repo_name != NULL) {
		kerr = khtml_puts(gw_trans->gw_html_req, " / ");
		if (kerr != KCGI_OK)
			goto done;
		if (asprintf(&href_summary, "?path=%s&action=summary",
		    gw_trans->repo_name) == -1)
			goto done;
		kerr = khtml_attr(gw_trans->gw_html_req, KELEM_A, KATTR_HREF,
		    href_summary, KATTR__MAX);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, gw_trans->repo_name);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req, " / ");
		if (kerr != KCGI_OK)
			goto done;
		kerr = khtml_puts(gw_trans->gw_html_req,
		    gw_get_action_name(gw_trans));
		if (kerr != KCGI_OK)
			goto done;
	}

	kerr = khtml_closeelem(gw_trans->gw_html_req, 1);
	if (kerr != KCGI_OK)
		goto done;
done:
	free(href_summary);
	return error;
}

static const struct got_error *
gw_colordiff_line(struct gw_trans *gw_trans, char *buf)
{
	const struct got_error *error = NULL;
	char *color = NULL;
	enum kcgi_err kerr = KCGI_OK;

	if (strncmp(buf, "-", 1) == 0)
		color = "diff_minus";
	else if (strncmp(buf, "+", 1) == 0)
		color = "diff_plus";
	else if (strncmp(buf, "@@", 2) == 0)
		color = "diff_chunk_header";
	else if (strncmp(buf, "@@", 2) == 0)
		color = "diff_chunk_header";
	else if (strncmp(buf, "commit +", 8) == 0)
		color = "diff_meta";
	else if (strncmp(buf, "commit -", 8) == 0)
		color = "diff_meta";
	else if (strncmp(buf, "blob +", 6) == 0)
		color = "diff_meta";
	else if (strncmp(buf, "blob -", 6) == 0)
		color = "diff_meta";
	else if (strncmp(buf, "file +", 6) == 0)
		color = "diff_meta";
	else if (strncmp(buf, "file -", 6) == 0)
		color = "diff_meta";
	else if (strncmp(buf, "from:", 5) == 0)
		color = "diff_author";
	else if (strncmp(buf, "via:", 4) == 0)
		color = "diff_author";
	else if (strncmp(buf, "date:", 5) == 0)
		color = "diff_date";
	kerr = khtml_attr(gw_trans->gw_html_req, KELEM_DIV, KATTR_ID,
	    "diff_line", KATTR_CLASS, color ? color : "", KATTR__MAX);
	if (error == NULL && kerr != KCGI_OK)
		error = gw_kcgi_error(kerr);
	return error;
}

int
main(int argc, char *argv[])
{
	const struct got_error *error = NULL;
	struct gw_trans *gw_trans;
	struct gw_dir *dir = NULL, *tdir;
	const char *page = "index";
	int gw_malloc = 1;
	enum kcgi_err kerr;

	if ((gw_trans = malloc(sizeof(struct gw_trans))) == NULL)
		errx(1, "malloc");

	if ((gw_trans->gw_req = malloc(sizeof(struct kreq))) == NULL)
		errx(1, "malloc");

	if ((gw_trans->gw_html_req = malloc(sizeof(struct khtmlreq))) == NULL)
		errx(1, "malloc");

	if ((gw_trans->gw_tmpl = malloc(sizeof(struct ktemplate))) == NULL)
		errx(1, "malloc");

	kerr = khttp_parse(gw_trans->gw_req, gw_keys, KEY__ZMAX, &page, 1, 0);
	if (kerr != KCGI_OK) {
		error = gw_kcgi_error(kerr);
		goto done;
	}

	if ((gw_trans->gw_conf =
	    malloc(sizeof(struct gotweb_conf))) == NULL) {
		gw_malloc = 0;
		error = got_error_from_errno("malloc");
		goto done;
	}

	TAILQ_INIT(&gw_trans->gw_dirs);
	TAILQ_INIT(&gw_trans->gw_headers);

	gw_trans->action = -1;
	gw_trans->page = 0;
	gw_trans->repos_total = 0;
	gw_trans->repo_path = NULL;
	gw_trans->commit = NULL;
	gw_trans->headref = strdup(GOT_REF_HEAD);
	if (gw_trans->headref == NULL) {
		error = got_error_from_errno("strdup");
		goto done;
	}
	gw_trans->mime = KMIME_TEXT_HTML;
	gw_trans->gw_tmpl->key = gw_templs;
	gw_trans->gw_tmpl->keysz = TEMPL__MAX;
	gw_trans->gw_tmpl->arg = gw_trans;
	gw_trans->gw_tmpl->cb = gw_template;
	error = parse_conf(GOTWEB_CONF, gw_trans->gw_conf);
	if (error)
		goto done;

	error = gw_parse_querystring(gw_trans);
	if (error)
		goto done;

	if (gw_trans->action == GW_BLOB)
		error = gw_blob(gw_trans);
	else
		error = gw_display_index(gw_trans);
done:
	if (gw_malloc) {
		free(gw_trans->gw_conf->got_repos_path);
		free(gw_trans->gw_conf->got_site_name);
		free(gw_trans->gw_conf->got_site_owner);
		free(gw_trans->gw_conf->got_site_link);
		free(gw_trans->gw_conf->got_logo);
		free(gw_trans->gw_conf->got_logo_url);
		free(gw_trans->gw_conf);
		free(gw_trans->commit);
		free(gw_trans->repo_path);
		free(gw_trans->headref);

		TAILQ_FOREACH_SAFE(dir, &gw_trans->gw_dirs, entry, tdir) {
			free(dir->name);
			free(dir->description);
			free(dir->age);
			free(dir->url);
			free(dir->path);
			free(dir);
		}

	}

	khttp_free(gw_trans->gw_req);
	return 0;
}
