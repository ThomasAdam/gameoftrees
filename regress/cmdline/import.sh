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

function test_import_basic {
	local testname=import_basic
	local testroot=`mktemp -p /tmp -d got-test-$testname-XXXXXXXX`

	got init $testroot/repo

	mkdir $testroot/tree
	make_test_tree $testroot/tree

	got import -m 'init' -r $testroot/repo $testroot/tree \
		> $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	local head_commit=`git_show_head $testroot/repo`
	echo "A  $testroot/tree/gamma/delta" > $testroot/stdout.expected
	echo "A  $testroot/tree/epsilon/zeta" >> $testroot/stdout.expected
	echo "A  $testroot/tree/alpha" >> $testroot/stdout.expected
	echo "A  $testroot/tree/beta" >> $testroot/stdout.expected
	echo "Created branch refs/heads/master with commit $head_commit" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/repo && got log -p | grep -v ^date: > $testroot/stdout)

	id_alpha=`get_blob_id $testroot/repo "" alpha`
	id_beta=`get_blob_id $testroot/repo "" beta`
	id_zeta=`get_blob_id $testroot/repo epsilon zeta`
	id_delta=`get_blob_id $testroot/repo gamma delta`
	tree_id=`(cd $testroot/repo && got cat $head_commit | \
		grep ^tree | cut -d ' ' -f 2)`

	echo "-----------------------------------------------" \
		> $testroot/stdout.expected
	echo "commit $head_commit (master)" >> $testroot/stdout.expected
	echo "from: $GOT_AUTHOR" >> $testroot/stdout.expected
	echo " " >> $testroot/stdout.expected
	echo " init" >> $testroot/stdout.expected
	echo " " >> $testroot/stdout.expected
	echo "diff /dev/null $tree_id" >> $testroot/stdout.expected
	echo "blob - /dev/null" >> $testroot/stdout.expected
	echo "blob + $id_alpha" >> $testroot/stdout.expected
	echo "--- /dev/null" >> $testroot/stdout.expected
	echo "+++ alpha" >> $testroot/stdout.expected
	echo "@@ -0,0 +1 @@" >> $testroot/stdout.expected
	echo "+alpha" >> $testroot/stdout.expected
	echo "blob - /dev/null" >> $testroot/stdout.expected
	echo "blob + $id_beta" >> $testroot/stdout.expected
	echo "--- /dev/null" >> $testroot/stdout.expected
	echo "+++ beta" >> $testroot/stdout.expected
	echo "@@ -0,0 +1 @@" >> $testroot/stdout.expected
	echo "+beta" >> $testroot/stdout.expected
	echo "blob - /dev/null" >> $testroot/stdout.expected
	echo "blob + $id_zeta" >> $testroot/stdout.expected
	echo "--- /dev/null" >> $testroot/stdout.expected
	echo "+++ epsilon/zeta" >> $testroot/stdout.expected
	echo "@@ -0,0 +1 @@" >> $testroot/stdout.expected
	echo "+zeta" >> $testroot/stdout.expected
	echo "blob - /dev/null" >> $testroot/stdout.expected
	echo "blob + $id_delta" >> $testroot/stdout.expected
	echo "--- /dev/null" >> $testroot/stdout.expected
	echo "+++ gamma/delta" >> $testroot/stdout.expected
	echo "@@ -0,0 +1 @@" >> $testroot/stdout.expected
	echo "+delta" >> $testroot/stdout.expected
	echo "" >> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

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

function test_import_requires_new_branch {
	local testroot=`test_init import_requires_new_branch`

	mkdir $testroot/tree
	make_test_tree $testroot/tree

	got import -m 'init' -r $testroot/repo $testroot/tree \
		> $testroot/stdout 2> $testroot/stderr
	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "import command should have failed but did not"
		test_done "$testroot" "1"
		return 1
	fi

	echo "got: import target branch already exists" \
		> $testroot/stderr.expected
	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
		test_done "$testroot" "$ret"
		return 1
	fi

	got import -b newbranch -m 'init' -r $testroot/repo $testroot/tree  \
		> $testroot/stdout
	ret="$?"
	test_done "$testroot" "$ret"

}

function test_import_ignores {
	local testname=import_ignores
	local testroot=`mktemp -p /tmp -d got-test-$testname-XXXXXXXX`

	got init $testroot/repo

	mkdir $testroot/tree
	make_test_tree $testroot/tree

	got import -I alpha -I '*lta*' -I '*silon' \
		-m 'init' -r $testroot/repo $testroot/tree > $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	local head_commit=`git_show_head $testroot/repo`
	echo "A  $testroot/tree/beta" >> $testroot/stdout.expected
	echo "Created branch refs/heads/master with commit $head_commit" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_import_empty_dir {
	local testname=import_empty_dir
	local testroot=`mktemp -p /tmp -d got-test-$testname-XXXXXXXX`

	got init $testroot/repo

	mkdir $testroot/tree
	mkdir -p $testroot/tree/empty $testroot/tree/notempty
	echo "alpha" > $testroot/tree/notempty/alpha

	got import -m 'init' -r $testroot/repo $testroot/tree > $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	local head_commit=`git_show_head $testroot/repo`
	echo "A  $testroot/tree/notempty/alpha" >> $testroot/stdout.expected
	echo "Created branch refs/heads/master with commit $head_commit" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	# Verify that Got did not import the empty directory
	echo "notempty/" > $testroot/stdout.expected
	echo "notempty/alpha" >> $testroot/stdout.expected

	got tree -r $testroot/repo -R > $testroot/stdout
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

run_test test_import_basic
run_test test_import_requires_new_branch
run_test test_import_ignores
run_test test_import_empty_dir
