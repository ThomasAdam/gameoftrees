bin_PROGRAMS = tog

tog_SOURCES = tog.c \
	$(top_srcdir)/lib/blame.c \
	$(top_srcdir)/lib/commit_graph.c \
	$(top_srcdir)/lib/delta.c \
	$(top_srcdir)/lib/diff.c \
	$(top_srcdir)/lib/diffreg.c \
	$(top_srcdir)/lib/error.c \
	$(top_srcdir)/lib/fileindex.c \
	$(top_srcdir)/lib/object.c \
	$(top_srcdir)/lib/object_cache.c \
	$(top_srcdir)/lib/object_idset.c \
	$(top_srcdir)/lib/object_parse.c \
	$(top_srcdir)/lib/opentemp.c \
	$(top_srcdir)/lib/path.c \
	$(top_srcdir)/lib/pack.c \
	$(top_srcdir)/lib/privsep.c \
	$(top_srcdir)/lib/reference.c \
	$(top_srcdir)/lib/repository.c \
	$(top_srcdir)/lib/sha1.c \
	$(top_srcdir)/lib/worktree.c \
	$(top_srcdir)/lib/inflate.c \
	$(top_srcdir)/lib/buf.c \
	$(top_srcdir)/lib/rcsutil.c \
	$(top_srcdir)/lib/diff3.c \
	$(top_srcdir)/lib/lockfile.c \
	$(top_srcdir)/lib/deflate.c \
	$(top_srcdir)/lib/object_create.c \
	$(top_srcdir)/lib/delta_cache.c \
	$(top_srcdir)/lib/fetch.c \
	$(top_srcdir)/lib/gotconfig.c \
	$(top_srcdir)/lib/diff_main.c \
	$(top_srcdir)/lib/diff_atomize_text.c \
	$(top_srcdir)/lib/diff_myers.c \
	$(top_srcdir)/lib/diff_output.c \
	$(top_srcdir)/lib/diff_output_plain.c \
	$(top_srcdir)/lib/diff_output_unidiff.c \
	$(top_srcdir)/lib/diff_output_edscript.c \
	$(top_srcdir)/lib/diff_patience.c \
	$(top_srcdir)/lib/utf8.c

tog_DEPENDENCIES = $(top_builddir)/compat/libopenbsd-compat.a

man1_MANS = tog.1

AM_CPPFLAGS += -DGOT_VERSION='"@VERSION@"' \
	-DGOT_VERSION_NUMBER='"@VERSION@"' \
	-DGOT_LIBEXEC_DIR="${bindir}" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/compat \
	-I$(top_srcdir)/lib \
	-I$(top_srcdir)/include \
	-I.

LDADD = -L$(top_builddir)/compat -lopenbsd-compat -lpthread
