#!/bin/sh
#
# Copyright (c) 2019 Stefan Sperling <stsp@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

. ./common.sh

test_checkout_basic() {
	local testroot=`test_init checkout_basic`

	echo "A  $testroot/wt/alpha" > $testroot/stdout.expected
	echo "A  $testroot/wt/beta" >> $testroot/stdout.expected
	echo "A  $testroot/wt/epsilon/zeta" >> $testroot/stdout.expected
	echo "A  $testroot/wt/gamma/delta" >> $testroot/stdout.expected
	echo "Now shut up and hack" >> $testroot/stdout.expected

	got checkout $testroot/repo $testroot/wt > $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "alpha" > $testroot/content.expected
	echo "beta" >> $testroot/content.expected
	echo "zeta" >> $testroot/content.expected
	echo "delta" >> $testroot/content.expected
	cat $testroot/wt/alpha $testroot/wt/beta $testroot/wt/epsilon/zeta \
	    $testroot/wt/gamma/delta > $testroot/content

	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
	fi
	test_done "$testroot" "$ret"
}

test_checkout_dir_exists() {
	local testroot=`test_init checkout_dir_exists`

	echo "A  $testroot/wt/alpha" > $testroot/stdout.expected
	echo "A  $testroot/wt/beta" >> $testroot/stdout.expected
	echo "A  $testroot/wt/epsilon/zeta" >> $testroot/stdout.expected
	echo "A  $testroot/wt/gamma/delta" >> $testroot/stdout.expected
	echo "Now shut up and hack" >> $testroot/stdout.expected

	mkdir $testroot/wt

	got checkout $testroot/repo $testroot/wt > $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "alpha" > $testroot/content.expected
	echo "beta" >> $testroot/content.expected
	echo "zeta" >> $testroot/content.expected
	echo "delta" >> $testroot/content.expected
	cat $testroot/wt/alpha $testroot/wt/beta $testroot/wt/epsilon/zeta \
	    $testroot/wt/gamma/delta > $testroot/content

	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
	fi
	test_done "$testroot" "$ret"
}

test_checkout_dir_not_empty() {
	local testroot=`test_init checkout_dir_not_empty`

	echo "A  $testroot/wt/alpha" > $testroot/stdout.expected
	echo "A  $testroot/wt/beta" >> $testroot/stdout.expected
	echo "A  $testroot/wt/epsilon/zeta" >> $testroot/stdout.expected
	echo "A  $testroot/wt/gamma/delta" >> $testroot/stdout.expected
	echo "Now shut up and hack" >> $testroot/stdout.expected

	mkdir $testroot/wt
	touch $testroot/wt/foo

	got checkout $testroot/repo $testroot/wt > $testroot/stdout \
		2> $testroot/stderr
	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "checkout succeeded unexpectedly" >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo "got: $testroot/wt: directory exists and is not empty" \
		> $testroot/stderr.expected
	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
		test_done "$testroot" "$ret"
		return 1
	fi

	echo -n > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"

}

test_checkout_sets_xbit() {
	local testroot=`test_init checkout_sets_xbit 1`

	touch $testroot/repo/xfile
	chmod +x $testroot/repo/xfile
	(cd $testroot/repo && git add .)
	git_commit $testroot/repo -m "adding executable file"

	echo "A  $testroot/wt/xfile" > $testroot/stdout.expected
	echo "Now shut up and hack" >> $testroot/stdout.expected

	got checkout $testroot/repo $testroot/wt > $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	ls -l $testroot/wt/xfile | grep -q '^-rwx'
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "file is not executable" >&2
		ls -l $testroot/wt/xfile >&2
	fi
	test_done "$testroot" "$ret"
}

test_checkout_commit_from_wrong_branch() {
	local testroot=`test_init checkout_commit_from_wrong_branch`

	(cd $testroot/repo && git checkout -q -b newbranch)
	echo "modified alpha on new branch" > $testroot/repo/alpha
	git_commit $testroot/repo -m "modified alpha on new branch"

	local head_rev=`git_show_head $testroot/repo`
	got checkout -b master -c $head_rev $testroot/repo $testroot/wt \
		> $testroot/stdout 2> $testroot/stderr
	ret="$?"
	if [ "$ret" == "0" ]; then
		test_done "$testroot" "1"
		return 1
	fi

	echo -n "" > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo -n "got: target commit is not contained in branch 'master'; " \
		> $testroot/stderr.expected
	echo -n "the branch to use must be specified with -b; if necessary " \
		>> $testroot/stderr.expected
	echo -n "a new branch can be created for this commit with "\
		>> $testroot/stderr.expected
	echo "'got branch -c $head_rev BRANCH_NAME'" \
		>> $testroot/stderr.expected
	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
		test_done "$testroot" "$ret"
		return 1
	fi

	test_done "$testroot" "$ret"
}

test_checkout_tag() {
	local testroot=`test_init checkout_tag`
	local tag="1.0.0"

	(cd $testroot/repo && git tag -a -m "test" $tag)

	echo "A  $testroot/wt/alpha" > $testroot/stdout.expected
	echo "A  $testroot/wt/beta" >> $testroot/stdout.expected
	echo "A  $testroot/wt/epsilon/zeta" >> $testroot/stdout.expected
	echo "A  $testroot/wt/gamma/delta" >> $testroot/stdout.expected
	echo "Now shut up and hack" >> $testroot/stdout.expected

	got checkout -c $tag $testroot/repo $testroot/wt > $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "alpha" > $testroot/content.expected
	echo "beta" >> $testroot/content.expected
	echo "zeta" >> $testroot/content.expected
	echo "delta" >> $testroot/content.expected
	cat $testroot/wt/alpha $testroot/wt/beta $testroot/wt/epsilon/zeta \
	    $testroot/wt/gamma/delta > $testroot/content

	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
	fi
	test_done "$testroot" "$ret"
}

test_checkout_ignores_submodules() {
	local testroot=`test_init checkout_ignores_submodules`

	make_single_file_repo $testroot/repo2 foo

	(cd $testroot/repo && git submodule -q add ../repo2)
	(cd $testroot/repo && git commit -q -m 'adding submodule')

	echo "A  $testroot/wt/.gitmodules" > $testroot/stdout.expected
	echo "A  $testroot/wt/alpha" >> $testroot/stdout.expected
	echo "A  $testroot/wt/beta" >> $testroot/stdout.expected
	echo "A  $testroot/wt/epsilon/zeta" >> $testroot/stdout.expected
	echo "A  $testroot/wt/gamma/delta" >> $testroot/stdout.expected
	echo "Now shut up and hack" >> $testroot/stdout.expected

	got checkout $testroot/repo $testroot/wt > $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "alpha" > $testroot/content.expected
	echo "beta" >> $testroot/content.expected
	echo "zeta" >> $testroot/content.expected
	echo "delta" >> $testroot/content.expected
	cat $testroot/wt/alpha $testroot/wt/beta $testroot/wt/epsilon/zeta \
	    $testroot/wt/gamma/delta > $testroot/content

	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
	fi
	test_done "$testroot" "$ret"
}

test_checkout_read_only() {
	local testroot=`test_init checkout_read_only`

	# Make the repostiory read-only
	chmod -R a-w $testroot/repo

	echo "A  $testroot/wt/alpha" > $testroot/stdout.expected
	echo "A  $testroot/wt/beta" >> $testroot/stdout.expected
	echo "A  $testroot/wt/epsilon/zeta" >> $testroot/stdout.expected
	echo "A  $testroot/wt/gamma/delta" >> $testroot/stdout.expected
	echo "Now shut up and hack" >> $testroot/stdout.expected

	got checkout $testroot/repo $testroot/wt \
		> $testroot/stdout 2> $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo -n "got: warning: could not create a reference " \
		> $testroot/stderr.expected
	echo -n "to the work tree's base commit; the commit could " \
		>> $testroot/stderr.expected
	echo -n "be garbage-collected by Git; making the repository " \
		>> $testroot/stderr.expected
	echo "writable and running 'got update' will prevent this" \
		>> $testroot/stderr.expected
	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "alpha" > $testroot/content.expected
	echo "beta" >> $testroot/content.expected
	echo "zeta" >> $testroot/content.expected
	echo "delta" >> $testroot/content.expected
	cat $testroot/wt/alpha $testroot/wt/beta $testroot/wt/epsilon/zeta \
	    $testroot/wt/gamma/delta > $testroot/content

	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
	fi
	chmod -R u+w $testroot/repo # make repo cleanup work
	test_done "$testroot" "$ret"
}

test_checkout_into_nonempty_dir() {
	local testroot=`test_init checkout_into_nonempty_dir`

	mkdir -p $testroot/wt
	make_test_tree $testroot/wt

	got checkout $testroot/repo $testroot/wt > $testroot/stdout \
		2> $testroot/stderr
	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "checkout succeeded unexpectedly" >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo -n > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "got: $testroot/wt: directory exists and is not empty" \
		> $testroot/stderr.expected
	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "?  $testroot/wt/alpha" > $testroot/stdout.expected
	echo "?  $testroot/wt/beta" >> $testroot/stdout.expected
	echo "?  $testroot/wt/epsilon/zeta" >> $testroot/stdout.expected
	echo "?  $testroot/wt/gamma/delta" >> $testroot/stdout.expected
	echo "Now shut up and hack" >> $testroot/stdout.expected

	got checkout -E $testroot/repo $testroot/wt > $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "E  $testroot/wt/alpha" > $testroot/stdout.expected
	echo "E  $testroot/wt/beta" >> $testroot/stdout.expected
	echo "E  $testroot/wt/epsilon/zeta" >> $testroot/stdout.expected
	echo "E  $testroot/wt/gamma/delta" >> $testroot/stdout.expected
	echo "Now shut up and hack" >> $testroot/stdout.expected

	got checkout -E $testroot/repo $testroot/wt > $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "alpha" > $testroot/content.expected
	echo "beta" >> $testroot/content.expected
	echo "zeta" >> $testroot/content.expected
	echo "delta" >> $testroot/content.expected
	cat $testroot/wt/alpha $testroot/wt/beta $testroot/wt/epsilon/zeta \
	    $testroot/wt/gamma/delta > $testroot/content

	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "modified alpha" > $testroot/wt/alpha

	echo "E  $testroot/wt/alpha" > $testroot/stdout.expected
	echo "E  $testroot/wt/beta" >> $testroot/stdout.expected
	echo "E  $testroot/wt/epsilon/zeta" >> $testroot/stdout.expected
	echo "E  $testroot/wt/gamma/delta" >> $testroot/stdout.expected
	echo "Now shut up and hack" >> $testroot/stdout.expected

	got checkout -E $testroot/repo $testroot/wt > $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "modified alpha" > $testroot/content.expected
	echo "beta" >> $testroot/content.expected
	echo "zeta" >> $testroot/content.expected
	echo "delta" >> $testroot/content.expected
	cat $testroot/wt/alpha $testroot/wt/beta $testroot/wt/epsilon/zeta \
	    $testroot/wt/gamma/delta > $testroot/content

	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	echo 'M  alpha' > $testroot/stdout.expected
	(cd $testroot/wt && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

test_checkout_symlink() {
	local testroot=`test_init checkout_symlink`

	(cd $testroot/repo && ln -s alpha alpha.link)
	(cd $testroot/repo && ln -s epsilon epsilon.link)
	(cd $testroot/repo && ln -s /etc/passwd passwd.link)
	(cd $testroot/repo && ln -s passwd.link passwd2.link)
	(cd $testroot/repo && ln -s ../beta epsilon/beta.link)
	(cd $testroot/repo && ln -s nonexistent nonexistent.link)
	(cd $testroot/repo && ln -s .got/foo dotgotfoo.link)
	(cd $testroot/repo && git add .)
	git_commit $testroot/repo -m "add symlinks"

	got checkout $testroot/repo $testroot/wt > $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got checkout failed unexpectedly" >&2
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "A  $testroot/wt/alpha" > $testroot/stdout.expected
	echo "A  $testroot/wt/alpha.link" >> $testroot/stdout.expected
	echo "A  $testroot/wt/beta" >> $testroot/stdout.expected
	echo "A  $testroot/wt/dotgotfoo.link" >> $testroot/stdout.expected
	echo "A  $testroot/wt/epsilon/beta.link" >> $testroot/stdout.expected
	echo "A  $testroot/wt/epsilon/zeta" >> $testroot/stdout.expected
	echo "A  $testroot/wt/epsilon.link" >> $testroot/stdout.expected
	echo "A  $testroot/wt/gamma/delta" >> $testroot/stdout.expected
	echo "A  $testroot/wt/nonexistent.link" >> $testroot/stdout.expected
	echo "A  $testroot/wt/passwd.link" >> $testroot/stdout.expected
	echo "A  $testroot/wt/passwd2.link" >> $testroot/stdout.expected
	echo "Now shut up and hack" >> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	if ! [ -h $testroot/wt/alpha.link ]; then
		echo "alpha.link is not a symlink"
		test_done "$testroot" "1"
		return 1
	fi

	readlink $testroot/wt/alpha.link > $testroot/stdout
	echo "alpha" > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	if ! [ -h $testroot/wt/epsilon.link ]; then
		echo "epsilon.link is not a symlink"
		test_done "$testroot" "1"
		return 1
	fi

	readlink $testroot/wt/epsilon.link > $testroot/stdout
	echo "epsilon" > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	if [ -h $testroot/wt/passwd.link ]; then
		echo -n "passwd.link symlink points outside of work tree: " >&2
		readlink $testroot/wt/passwd.link >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo -n "/etc/passwd" > $testroot/content.expected
	cp $testroot/wt/passwd.link $testroot/content

	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	if ! [ -h $testroot/wt/passwd2.link ]; then
		echo "passwd2.link is not a symlink"
		test_done "$testroot" "1"
		return 1
	fi

	readlink $testroot/wt/passwd2.link > $testroot/stdout
	echo "passwd.link" > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	readlink $testroot/wt/epsilon/beta.link > $testroot/stdout
	echo "../beta" > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	readlink $testroot/wt/nonexistent.link > $testroot/stdout
	echo "nonexistent" > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	if [ -h $testroot/wt/dotgotfoo.link ]; then
		echo -n "dotgotfoo.link symlink points into .got dir: " >&2
		readlink $testroot/wt/dotgotfoo.link >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo -n ".got/foo" > $testroot/content.expected
	cp $testroot/wt/dotgotfoo.link $testroot/content

	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
	fi
	test_done "$testroot" "$ret"
}

test_checkout_symlink_relative_wtpath() {
	local testroot=`test_init checkout_symlink_with_wtpath`

	(cd $testroot/repo && ln -s alpha alpha.link)
	(cd $testroot/repo && ln -s epsilon epsilon.link)
	(cd $testroot/repo && ln -s /etc/passwd passwd.link)
	(cd $testroot/repo && ln -s ../beta epsilon/beta.link)
	(cd $testroot/repo && ln -s nonexistent nonexistent.link)
	(cd $testroot/repo && ln -s .got/foo dotgotfoo.link)
	(cd $testroot/repo && git add .)
	git_commit $testroot/repo -m "add symlinks"

	(cd $testroot && got checkout $testroot/repo wt > /dev/null)
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	if ! [ -h $testroot/wt/alpha.link ]; then
		echo "alpha.link is not a symlink"
		test_done "$testroot" "1"
		return 1
	fi

	readlink $testroot/wt/alpha.link > $testroot/stdout
	echo "alpha" > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	if ! [ -h $testroot/wt/epsilon.link ]; then
		echo "epsilon.link is not a symlink"
		test_done "$testroot" "1"
		return 1
	fi

	readlink $testroot/wt/epsilon.link > $testroot/stdout
	echo "epsilon" > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	if [ -h $testroot/wt/passwd.link ]; then
		echo -n "passwd.link symlink points outside of work tree: " >&2
		readlink $testroot/wt/passwd.link >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo -n "/etc/passwd" > $testroot/content.expected
	cp $testroot/wt/passwd.link $testroot/content

	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	readlink $testroot/wt/epsilon/beta.link > $testroot/stdout
	echo "../beta" > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	readlink $testroot/wt/nonexistent.link > $testroot/stdout
	echo "nonexistent" > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	if [ -h $testroot/wt/dotgotfoo.link ]; then
		echo -n "dotgotfoo.link symlink points into .got dir: " >&2
		readlink $testroot/wt/dotgotfoo.link >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo -n ".got/foo" > $testroot/content.expected
	cp $testroot/wt/dotgotfoo.link $testroot/content

	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
	fi
	test_done "$testroot" "$ret"
}

test_parseargs "$@"
run_test test_checkout_basic
run_test test_checkout_dir_exists
run_test test_checkout_dir_not_empty
run_test test_checkout_sets_xbit
run_test test_checkout_commit_from_wrong_branch
run_test test_checkout_tag
run_test test_checkout_ignores_submodules
run_test test_checkout_read_only
run_test test_checkout_into_nonempty_dir
run_test test_checkout_symlink
run_test test_checkout_symlink_relative_wtpath
