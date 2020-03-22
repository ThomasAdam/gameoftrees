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

function test_rm_basic {
	local testroot=`test_init rm_basic`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo 'D  alpha' > $testroot/stdout.expected
	echo 'D  beta' >> $testroot/stdout.expected
	(cd $testroot/wt && got rm alpha beta > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	for f in alpha beta; do
		if [ -e $testroot/wt/$f ]; then
			echo "removed file $f still exists on disk" >&2
			test_done "$testroot" "1"
			return 1
		fi
	done

	test_done "$testroot" "0"
}

function test_rm_with_local_mods {
	local testroot=`test_init rm_with_local_mods`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "modified beta" > $testroot/wt/beta
	echo 'got: beta: file contains modifications' \
		> $testroot/stderr.expected
	(cd $testroot/wt && got rm beta 2>$testroot/stderr)

	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
		test_done "$testroot" "$ret"
		return 1
	fi

	echo 'D  beta' > $testroot/stdout.expected
	(cd $testroot/wt && got rm -f beta > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi

	if [ -e $testroot/wt/beta ]; then
		echo "removed file beta still exists on disk" >&2
		test_done "$testroot" "1"
		return 1
	fi

	test_done "$testroot" "$ret"
}

function test_double_rm {
	local testroot=`test_init double_rm`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got rm beta > /dev/null)

	for fflag in "" "-f"; do
		echo -n > $testroot/stderr.expected
		(cd $testroot/wt && got rm $fflag beta > $testroot/stdout \
			2> $testroot/stderr)
		ret="$?"
		if [ "$ret" != "0" ]; then
			echo "got rm command failed unexpectedly" >&2
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
	done
	test_done "$testroot" "0"
}

function test_rm_and_add_elsewhere {
	local testroot=`test_init rm_and_add_elsewhere`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && mv alpha epsilon/)

	(cd $testroot/wt && got status > $testroot/stdout)

	echo '!  alpha' > $testroot/stdout.expected
	echo '?  epsilon/alpha' >> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo 'D  alpha' > $testroot/stdout.expected
	(cd $testroot/wt && got rm alpha > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo 'A  epsilon/alpha' > $testroot/stdout.expected
	(cd $testroot/wt && got add epsilon/alpha > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got status > $testroot/stdout)

	echo 'D  alpha' > $testroot/stdout.expected
	echo 'A  epsilon/alpha' >> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_rm_directory {
	local testroot=`test_init rm_directory`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got rm . > $testroot/stdout 2> $testroot/stderr)
	ret="$?"
	echo "got: removing directories requires -R option" \
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

	(cd $testroot/wt && got rm -R . > $testroot/stdout)

	echo 'D  alpha' > $testroot/stdout.expected
	echo 'D  beta' >> $testroot/stdout.expected
	echo 'D  epsilon/zeta' >> $testroot/stdout.expected
	echo 'D  gamma/delta' >> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && ls -l > $testroot/stdout)

	echo -n '' > $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && ls -l > $testroot/stdout)

	echo -n '' > $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	test_done "$testroot" "$ret"
}

function test_rm_directory_keep_files {
	local testroot=`test_init rm_directory_keep_files`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got rm . > $testroot/stdout 2> $testroot/stderr)
	ret="$?"
	echo "got: removing directories requires -R option" \
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

	(cd $testroot/wt && got rm -k -R . > $testroot/stdout)

	echo 'D  alpha' > $testroot/stdout.expected
	echo 'D  beta' >> $testroot/stdout.expected
	echo 'D  epsilon/zeta' >> $testroot/stdout.expected
	echo 'D  gamma/delta' >> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got st . > $testroot/stdout)

	echo 'D  alpha' > $testroot/stdout.expected
	echo 'D  beta' >> $testroot/stdout.expected
	echo 'D  epsilon/zeta' >> $testroot/stdout.expected
	echo 'D  gamma/delta' >> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got commit -m "keep" > /dev/null)
	(cd $testroot/wt && got st . > $testroot/stdout)

	echo '?  alpha' > $testroot/stdout.expected
	echo '?  beta' >> $testroot/stdout.expected
	echo '?  epsilon/zeta' >> $testroot/stdout.expected
	echo '?  gamma/delta' >> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	test_done "$testroot" "$ret"
}

function test_rm_subtree {
	local testroot=`test_init rm_subtree`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	mkdir -p $testroot/wt/epsilon/foo/bar/baz
	mkdir -p $testroot/wt/epsilon/foo/bar/bax
	echo "new file" > $testroot/wt/epsilon/foo/a.o
	echo "new file" > $testroot/wt/epsilon/foo/a.o
	echo "new file" > $testroot/wt/epsilon/foo/bar/b.o
	echo "new file" > $testroot/wt/epsilon/foo/bar/b.d
	echo "new file" > $testroot/wt/epsilon/foo/bar/baz/f.o
	echo "new file" > $testroot/wt/epsilon/foo/bar/baz/f.d
	echo "new file" > $testroot/wt/epsilon/foo/bar/baz/c.o
	echo "new file" > $testroot/wt/epsilon/foo/bar/baz/c.d
	echo "new file" > $testroot/wt/epsilon/foo/bar/bax/e.o
	echo "new file" > $testroot/wt/epsilon/foo/bar/bax/e.d
	echo "new file" > $testroot/wt/epsilon/foo/bar/bax/x.o
	echo "new file" > $testroot/wt/epsilon/foo/bar/bax/x.d
	(cd $testroot/wt && got add -R epsilon >/dev/null)
	(cd $testroot/wt && got commit -m "add subtree" >/dev/null)

	# now delete and revert the entire subtree
	(cd $testroot/wt && got rm -R epsilon/foo >/dev/null)

	if [ -d $testroot/wt/epsilon/foo ]; then
		echo "removed dir epsilon/foo still exists on disk" >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo "D  epsilon/foo/a.o" > $testroot/stdout.expected
	echo "D  epsilon/foo/bar/b.d" >> $testroot/stdout.expected
	echo "D  epsilon/foo/bar/b.o" >> $testroot/stdout.expected
	echo "D  epsilon/foo/bar/bax/e.d" >> $testroot/stdout.expected
	echo "D  epsilon/foo/bar/bax/e.o" >> $testroot/stdout.expected
	echo "D  epsilon/foo/bar/bax/x.d" >> $testroot/stdout.expected
	echo "D  epsilon/foo/bar/bax/x.o" >> $testroot/stdout.expected
	echo "D  epsilon/foo/bar/baz/c.d" >> $testroot/stdout.expected
	echo "D  epsilon/foo/bar/baz/c.o" >> $testroot/stdout.expected
	echo "D  epsilon/foo/bar/baz/f.d" >> $testroot/stdout.expected
	echo "D  epsilon/foo/bar/baz/f.o" >> $testroot/stdout.expected

	(cd $testroot/wt && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

run_test test_rm_basic
run_test test_rm_with_local_mods
run_test test_double_rm
run_test test_rm_and_add_elsewhere
run_test test_rm_directory
run_test test_rm_directory_keep_files
run_test test_rm_subtree
