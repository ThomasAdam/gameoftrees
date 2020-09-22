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

test_ref_create() {
	local testroot=`test_init ref_create`
	local commit_id=`git_show_head $testroot/repo`

	# Create a ref pointing at a commit ID
	got ref -r $testroot/repo -c $commit_id refs/heads/commitref
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got ref command failed unexpectedly"
		test_done "$testroot" "$ret"
		return 1
	fi

	# Create a ref based on repository's HEAD reference
	got ref -r $testroot/repo -c HEAD refs/heads/newref
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got ref command failed unexpectedly"
		test_done "$testroot" "$ret"
		return 1
	fi

	# Ensure that Git recognizes the ref Got has created
	(cd $testroot/repo && git checkout -q newref)
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "git checkout command failed unexpectedly"
		test_done "$testroot" "$ret"
		return 1
	fi

	# Ensure Got recognizes the new ref
	got checkout -b newref $testroot/repo $testroot/wt >/dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got checkout command failed unexpectedly"
		test_done "$testroot" "$ret"
		return 1
	fi

	# Create a head ref based on another specific ref
	(cd $testroot/wt && got ref -c refs/heads/master refs/heads/anotherref)
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/repo && git checkout -q anotherref)
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "git checkout command failed unexpectedly"
		test_done "$testroot" "$ret"
	fi

	# Create a symbolic ref
	(cd $testroot/wt && got ref -s refs/heads/master refs/heads/symbolicref)
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/repo && git checkout -q symbolicref)
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "git checkout command failed unexpectedly"
		test_done "$testroot" "$ret"
		return 1
	fi

	# Attempt to create a symbolic ref pointing at a non-reference
	(cd $testroot/wt && got ref -s $commit_id refs/heads/symbolicref \
		2> $testroot/stderr)
	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "git ref command succeeded unexpectedly"
		test_done "$testroot" "1"
		return 1
	fi

	echo "got: reference $commit_id not found" > $testroot/stderr.expected
	cmp -s $testroot/stderr $testroot/stderr.expected
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
		test_done "$testroot" "$ret"
		return 1
	fi

	# Attempt to create a reference without specifying a name
	(cd $testroot/wt && got ref -c $commit_id 2> $testroot/stderr)
	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "git ref command succeeded unexpectedly"
		test_done "$testroot" "1"
		return 1
	fi

	grep -q '^usage: got ref' $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "unexpected usage error message: " >&2
		cat $testroot/stderr >&2
		test_done "$testroot" "$ret"
		return 1
	fi

	# Attempt to create a symbolic reference without specifying a name
	(cd $testroot/wt && got ref -s refs/heads/symbolicref \
		2> $testroot/stderr)
	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "git ref command succeeded unexpectedly"
		test_done "$testroot" "1"
		return 1
	fi

	grep -q '^usage: got ref' $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "unexpected usage error message: " >&2
		cat $testroot/stderr >&2
		test_done "$testroot" "$ret"
		return 1
	fi

	# Change HEAD
	got ref -r $testroot/repo -s refs/heads/newref HEAD
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got ref command failed unexpectedly"
		test_done "$testroot" "$ret"
		return 1
	fi

	# Ensure that Git recognizes the ref Got has created
	(cd $testroot/repo && git checkout -q HEAD)
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "git checkout command failed unexpectedly"
		test_done "$testroot" "$ret"
		return 1
	fi

	# Ensure Got recognizes the new ref
	(cd $testroot/wt && got update -b HEAD >/dev/null)
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got checkout command failed unexpectedly"
		test_done "$testroot" "$ret"
		return 1
	fi
	got ref -r $testroot/repo -l > $testroot/stdout
	echo "HEAD: refs/heads/newref" > $testroot/stdout.expected
	echo -n "refs/got/worktree/base-" >> $testroot/stdout.expected
	cat $testroot/wt/.got/uuid | tr -d '\n' >> $testroot/stdout.expected
	echo ": $commit_id" >> $testroot/stdout.expected
	echo "refs/heads/anotherref: $commit_id" >> $testroot/stdout.expected
	echo "refs/heads/commitref: $commit_id" >> $testroot/stdout.expected
	echo "refs/heads/master: $commit_id" >> $testroot/stdout.expected
	echo "refs/heads/newref: $commit_id" >> $testroot/stdout.expected
	echo "refs/heads/symbolicref: refs/heads/master" \
		>> $testroot/stdout.expected
	cmp -s $testroot/stdout $testroot/stdout.expected
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

test_ref_delete() {
	local testroot=`test_init ref_delete`
	local commit_id=`git_show_head $testroot/repo`

	for b in ref1 ref2 ref3; do
		got ref -r $testroot/repo -c refs/heads/master refs/heads/$b
		ret="$?"
		if [ "$ret" != "0" ]; then
			echo "got ref command failed unexpectedly"
			test_done "$testroot" "$ret"
			return 1
		fi
	done

	got ref -d -r $testroot/repo refs/heads/ref2 > $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got ref command failed unexpectedly"
		test_done "$testroot" "$ret"
		return 1
	fi

	got ref -l -r $testroot/repo > $testroot/stdout
	echo "HEAD: refs/heads/master" > $testroot/stdout.expected
	echo "refs/heads/master: $commit_id" >> $testroot/stdout.expected
	echo "refs/heads/ref1: $commit_id" >> $testroot/stdout.expected
	echo "refs/heads/ref3: $commit_id" >> $testroot/stdout.expected
	cmp -s $testroot/stdout $testroot/stdout.expected
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	got ref -r $testroot/repo -d refs/heads/bogus_ref_name \
		> $testroot/stdout 2> $testroot/stderr
	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "got ref succeeded unexpectedly"
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "got: reference refs/heads/bogus_ref_name not found" \
		> $testroot/stderr.expected
	cmp -s $testroot/stderr $testroot/stderr.expected
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/repo && git pack-refs --all)

	echo "modified alpha" > $testroot/repo/alpha
	git_commit $testroot/repo -m "modified alpha"
	local commit_id2=`git_show_head $testroot/repo`

	# ref 'master' now exists in both packed and loose forms

	got ref -l -r $testroot/repo > $testroot/stdout
	echo "HEAD: refs/heads/master" > $testroot/stdout.expected
	echo "refs/heads/master: $commit_id2" >> $testroot/stdout.expected
	echo "refs/heads/ref1: $commit_id" >> $testroot/stdout.expected
	echo "refs/heads/ref3: $commit_id" >> $testroot/stdout.expected
	cmp -s $testroot/stdout $testroot/stdout.expected
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	got ref -r $testroot/repo -d master

	got ref -l -r $testroot/repo > $testroot/stdout
	echo "HEAD: refs/heads/master" > $testroot/stdout.expected
	echo "refs/heads/ref1: $commit_id" >> $testroot/stdout.expected
	echo "refs/heads/ref3: $commit_id" >> $testroot/stdout.expected
	cmp -s $testroot/stdout $testroot/stdout.expected
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

test_ref_list() {
	local testroot=`test_init ref_list`
	local commit_id=`git_show_head $testroot/repo`

	# Create a tag pointing at a commit ID
	got tag -r $testroot/repo -c $commit_id -m "1.0" "1.0" >/dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got tag command failed unexpectedly"
		test_done "$testroot" "$ret"
		return 1
	fi
	local tag_id=`got ref -r $testroot/repo -l \
		| grep "^refs/tags/$tag" | tr -d ' ' | cut -d: -f2`

	# Create a ref based on repository's HEAD reference
	got ref -r $testroot/repo -c HEAD refs/foo/zoo
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got ref command failed unexpectedly"
		test_done "$testroot" "$ret"
		return 1
	fi

	# Create a head ref based on another specific ref
	(cd $testroot/repo && got ref -c refs/heads/master refs/foo/bar/baz)
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	# Create a HEAD ref in the namespace of a remote repository
	(cd $testroot/repo && got ref -s refs/heads/master \
		refs/remotes/origin/HEAD)
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	got ref -r $testroot/repo -l > $testroot/stdout

	echo "HEAD: refs/heads/master" > $testroot/stdout.expected
	echo "refs/foo/bar/baz: $commit_id" >> $testroot/stdout.expected
	echo "refs/foo/zoo: $commit_id" >> $testroot/stdout.expected
	echo "refs/heads/master: $commit_id" >> $testroot/stdout.expected
	echo "refs/remotes/origin/HEAD: refs/heads/master" \
		>> $testroot/stdout.expected
	echo "refs/tags/1.0: $tag_id" >> $testroot/stdout.expected
	cmp -s $testroot/stdout $testroot/stdout.expected
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	got ref -r $testroot/repo -l refs > $testroot/stdout

	echo "refs/foo/bar/baz: $commit_id" > $testroot/stdout.expected
	echo "refs/foo/zoo: $commit_id" >> $testroot/stdout.expected
	echo "refs/heads/master: $commit_id" >> $testroot/stdout.expected
	echo "refs/remotes/origin/HEAD: refs/heads/master" \
		>> $testroot/stdout.expected
	echo "refs/tags/1.0: $tag_id" >> $testroot/stdout.expected
	cmp -s $testroot/stdout $testroot/stdout.expected
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	got ref -r $testroot/repo -l refs/tags > $testroot/stdout

	echo "refs/tags/1.0: $tag_id" > $testroot/stdout.expected
	cmp -s $testroot/stdout $testroot/stdout.expected
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	for r in refs/foo/bar/baz refs/foo/bar/baz foo/bar/baz foo/bar; do
		got ref -r $testroot/repo -l $r > $testroot/stdout

		echo "refs/foo/bar/baz: $commit_id" > $testroot/stdout.expected
		cmp -s $testroot/stdout $testroot/stdout.expected
		ret="$?"
		if [ "$ret" != "0" ]; then
			diff -u $testroot/stdout.expected $testroot/stdout
			test_done "$testroot" "$ret"
			return 1
		fi
	done

	for r in refs/foo foo; do
		got ref -r $testroot/repo -l $r > $testroot/stdout

		echo "refs/foo/bar/baz: $commit_id" > $testroot/stdout.expected
		echo "refs/foo/zoo: $commit_id" >> $testroot/stdout.expected
		cmp -s $testroot/stdout $testroot/stdout.expected
		ret="$?"
		if [ "$ret" != "0" ]; then
			diff -u $testroot/stdout.expected $testroot/stdout
			test_done "$testroot" "$ret"
			return 1
		fi
	done

	for r in refs//foo/bar refs//foo//bar refs////////foo//bar; do
		got ref -r $testroot/repo -l $r > $testroot/stdout

		echo "refs/foo/bar/baz: $commit_id" > $testroot/stdout.expected
		cmp -s $testroot/stdout $testroot/stdout.expected
		ret="$?"
		if [ "$ret" != "0" ]; then
			diff -u $testroot/stdout.expected $testroot/stdout
			test_done "$testroot" "$ret"
			return 1
		fi
	done

	# attempt to list non-existing references
	for r in refs/fo bar baz moo riffs /refs/abc refs/foo/bar/baz/moo; do
		got ref -r $testroot/repo -l $r > $testroot/stdout

		echo -n > $testroot/stdout.expected
		cmp -s $testroot/stdout $testroot/stdout.expected
		ret="$?"
		if [ "$ret" != "0" ]; then
			diff -u $testroot/stdout.expected $testroot/stdout
			test_done "$testroot" "$ret"
			return 1
		fi
	done	

	test_done "$testroot" "$ret"
}

test_parseargs "$@"
run_test test_ref_create
run_test test_ref_delete
run_test test_ref_list
