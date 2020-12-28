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

test_add_basic() {
	local testroot=`test_init add_basic`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "new file" > $testroot/wt/foo

	echo 'A  foo' > $testroot/stdout.expected
	(cd $testroot/wt && got add foo > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

test_double_add() {
	local testroot=`test_init double_add`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "new file" > $testroot/wt/foo
	(cd $testroot/wt && got add foo > /dev/null)

	(cd $testroot/wt && got add foo > $testroot/stdout)
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got add failed unexpectedly" >&2
		test_done "$testroot" 1
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

test_add_multiple() {
	local testroot=`test_init multiple_add`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "new file" > $testroot/wt/foo
	echo "new file" > $testroot/wt/bar
	echo "new file" > $testroot/wt/baz
	(cd $testroot/wt && got add foo bar baz > $testroot/stdout)
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got add failed unexpectedly" >&2
		test_done "$testroot" 1
		return 1
	fi

	echo "A  foo" > $testroot/stdout.expected
	echo "A  bar" >> $testroot/stdout.expected
	echo "A  baz" >> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

test_add_file_in_new_subdir() {
	local testroot=`test_init add_file_in_new_subdir`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	mkdir -p $testroot/wt/new
	echo "new file" > $testroot/wt/new/foo

	echo 'A  new/foo' > $testroot/stdout.expected
	(cd $testroot/wt && got add new/foo > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

test_add_deleted() {
	local testroot=`test_init add_deleted`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got rm beta > /dev/null)

	echo -n > $testroot/stdout.expected
	(cd $testroot/wt && got add beta > $testroot/stdout 2> $testroot/stderr)
	ret="$?"
	if [ "$ret" = "0" ]; then
		echo "got add command succeeded unexpectedly" >&2
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "1"
		return 1
	fi

	echo "got: beta: file has unexpected status" > $testroot/stderr.expected
	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
	fi
	test_done "$testroot" "$ret"
}

test_add_directory() {
	local testroot=`test_init add_directory`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got add . > $testroot/stdout 2> $testroot/stderr)
	ret="$?"
	echo "got: adding directories requires -R option" \
		> $testroot/stderr.expected
	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got add -I . > $testroot/stdout 2> $testroot/stderr)
	ret="$?"
	echo "got: disregarding ignores requires -R option" \
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
		test_done "$testroot" "$ret"
		return 1
	fi

	mkdir -p $testroot/wt/tree1
	mkdir -p $testroot/wt/tree2
	echo "tree1/**" > $testroot/wt/.gitignore
	echo "tree2/**" >> $testroot/wt/.gitignore
	echo -n > $testroot/wt/tree1/foo
	echo -n > $testroot/wt/tree2/foo
	echo -n > $testroot/wt/epsilon/zeta1
	echo -n > $testroot/wt/epsilon/zeta2

	(cd $testroot/wt && got add -R . > $testroot/stdout)

	echo 'A  .gitignore' > $testroot/stdout.expected
	echo 'A  epsilon/zeta1' >> $testroot/stdout.expected
	echo 'A  epsilon/zeta2' >> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got add -RI tree1 > $testroot/stdout)

	echo 'A  tree1/foo' > $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got add tree2/foo > $testroot/stdout)

	echo 'A  tree2/foo' > $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi
	test_done "$testroot" "$ret"
}

test_add_clashes_with_submodule() {
	local testroot=`test_init add_clashes_with_submodule`

	make_single_file_repo $testroot/repo2 foo

	(cd $testroot/repo && git submodule -q add ../repo2)
	(cd $testroot/repo && git commit -q -m 'adding submodule')

	got checkout $testroot/repo $testroot/wt > /dev/null

	# Atttempt to add a file clashes with a submodule
	echo "This is a file called repo2" > $testroot/wt/repo2
	(cd $testroot/wt && got add repo2 > /dev/null)

	(cd $testroot/wt && got status > $testroot/stdout)
	echo "A  repo2" > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	# Update for good measure; see the error below.
	(cd $testroot/wt && got update > /dev/null)

	# This currently fails with "work tree must be updated"...
	(cd $testroot/wt && got commit -m 'add file repo2' \
		> $testroot/stdout 2> $testroot/stderr)
	ret="$?"
	if [ "$ret" = "0" ]; then
		echo "commit succeeded unexpectedly" >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo -n "got: work tree must be updated " > $testroot/stderr.expected
	echo "before these changes can be committed" >> $testroot/stderr.expected
	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
	fi
	test_done "$testroot" "$ret"
}

test_add_symlink() {
	local testroot=`test_init add_symlink`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && ln -s alpha alpha.link)
	(cd $testroot/wt && ln -s epsilon epsilon.link)
	(cd $testroot/wt && ln -s /etc/passwd passwd.link)
	(cd $testroot/wt && ln -s ../beta epsilon/beta.link)
	(cd $testroot/wt && ln -s nonexistent nonexistent.link)

	echo "A  alpha.link" > $testroot/stdout.expected
	(cd $testroot/wt && got add alpha.link > $testroot/stdout)
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "A  epsilon.link" > $testroot/stdout.expected
	(cd $testroot/wt && got add epsilon.link > $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "A  passwd.link" > $testroot/stdout.expected
	(cd $testroot/wt && got add passwd.link > $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "A  epsilon/beta.link" > $testroot/stdout.expected
	(cd $testroot/wt && got add epsilon/beta.link > $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "A  nonexistent.link" > $testroot/stdout.expected
	(cd $testroot/wt && got add nonexistent.link > $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

test_parseargs "$@"
run_test test_add_basic
run_test test_double_add
run_test test_add_multiple
run_test test_add_file_in_new_subdir
run_test test_add_deleted
run_test test_add_directory
run_test test_add_clashes_with_submodule
run_test test_add_symlink
