#!/bin/sh
#
# Copyright (c) 2019, 2020 Stefan Sperling <stsp@openbsd.org>
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

function test_log_in_repo {
	local testroot=`test_init log_in_repo`
	local head_rev=`git_show_head $testroot/repo`

	echo "commit $head_rev (master)" > $testroot/stdout.expected

	for p in "" "." alpha epsilon epsilon/zeta; do
		(cd $testroot/repo && got log $p | \
			grep ^commit > $testroot/stdout)
		cmp -s $testroot/stdout.expected $testroot/stdout
		ret="$?"
		if [ "$ret" != "0" ]; then
			diff -u $testroot/stdout.expected $testroot/stdout
			test_done "$testroot" "$ret"
			return 1
		fi
	done

	for p in "" "." zeta; do
		(cd $testroot/repo/epsilon && got log $p | \
			grep ^commit > $testroot/stdout)
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

function test_log_in_bare_repo {
	local testroot=`test_init log_in_bare_repo`
	local head_rev=`git_show_head $testroot/repo`

	echo "commit $head_rev (master)" > $testroot/stdout.expected

	for p in "" "." alpha epsilon epsilon/zeta; do
		(cd $testroot/repo/.git && got log $p | \
			grep ^commit > $testroot/stdout)
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

function test_log_in_worktree {
	local testroot=`test_init log_in_worktree 1`

	make_test_tree $testroot/repo
	mkdir -p $testroot/repo/epsilon/d
	echo foo > $testroot/repo/epsilon/d/foo
	(cd $testroot/repo && git add .)
	git_commit $testroot/repo -m "adding the test tree"
	local head_commit=`git_show_head $testroot/repo`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "commit $head_commit (master)" > $testroot/stdout.expected

	for p in "" "." alpha epsilon; do
		(cd $testroot/wt && got log $p | \
			grep ^commit > $testroot/stdout)
		cmp -s $testroot/stdout.expected $testroot/stdout
		ret="$?"
		if [ "$ret" != "0" ]; then
			diff -u $testroot/stdout.expected $testroot/stdout
			test_done "$testroot" "$ret"
			return 1
		fi
	done

	for p in "" "." zeta; do
		(cd $testroot/wt/epsilon && got log $p | \
			grep ^commit > $testroot/stdout)
		cmp -s $testroot/stdout.expected $testroot/stdout
		ret="$?"
		if [ "$ret" != "0" ]; then
			diff -u $testroot/stdout.expected $testroot/stdout
			test_done "$testroot" "$ret"
			return 1
		fi
	done

	for p in "" "." foo; do
		(cd $testroot/wt/epsilon && got log d/$p | \
			grep ^commit > $testroot/stdout)
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

function test_log_in_worktree_with_path_prefix {
	local testroot=`test_init log_in_prefixed_worktree`
	local head_rev=`git_show_head $testroot/repo`

	echo "modified zeta" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "modified zeta"
	local zeta_rev=`git_show_head $testroot/repo`

	echo "modified delta" > $testroot/repo/gamma/delta
	git_commit $testroot/repo -m "modified delta"

	got checkout -p epsilon $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "commit $zeta_rev" > $testroot/stdout.expected
	echo "commit $head_rev" >> $testroot/stdout.expected

	for p in "" "." zeta; do
		(cd $testroot/wt && got log $p | \
			grep ^commit > $testroot/stdout)
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

function test_log_tag {
	local testroot=`test_init log_tag`
	local commit_id=`git_show_head $testroot/repo`
	local tag="1.0.0"
	local tag2="2.0.0"

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/repo && git tag -a -m "test" $tag)

	echo "commit $commit_id (master, tags/$tag)" > $testroot/stdout.expected
	(cd $testroot/wt && got log -l1 -c $tag | grep ^commit \
		> $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	# test a "lightweight" tag
	(cd $testroot/repo && git tag $tag2)

	echo "commit $commit_id (master, tags/$tag, tags/$tag2)" \
		> $testroot/stdout.expected
	(cd $testroot/wt && got log -l1 -c $tag2 | grep ^commit \
		> $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_log_limit {
	local testroot=`test_init log_limit`
	local commit_id0=`git_show_head $testroot/repo`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "modified alpha" > $testroot/wt/alpha
	(cd $testroot/wt && got commit -m 'test log_limit' > /dev/null)
	local commit_id1=`git_show_head $testroot/repo`

	(cd $testroot/wt && got rm beta >/dev/null)
	(cd $testroot/wt && got commit -m 'test log_limit' > /dev/null)
	local commit_id2=`git_show_head $testroot/repo`

	echo "new file" > $testroot/wt/new
	(cd $testroot/wt && got add new >/dev/null)
	(cd $testroot/wt && got commit -m 'test log_limit' > /dev/null)
	local commit_id3=`git_show_head $testroot/repo`

	# -l1 should print the first commit only
	echo "commit $commit_id3 (master)" > $testroot/stdout.expected
	(cd $testroot/wt && got log -l1 | grep ^commit > $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	# env var can be used to set a log limit without -l option
	echo "commit $commit_id3 (master)" > $testroot/stdout.expected
	echo "commit $commit_id2" >> $testroot/stdout.expected
	(cd $testroot/wt && env GOT_LOG_DEFAULT_LIMIT=2 got log | \
		grep ^commit > $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	# non-numeric env var is ignored
	(cd $testroot/wt && env GOT_LOG_DEFAULT_LIMIT=foobar got log | \
		grep ^commit > $testroot/stdout)
	echo "commit $commit_id3 (master)" > $testroot/stdout.expected
	echo "commit $commit_id2" >> $testroot/stdout.expected
	echo "commit $commit_id1" >> $testroot/stdout.expected
	echo "commit $commit_id0" >> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	# -l option takes precedence over env var
	echo "commit $commit_id3 (master)" > $testroot/stdout.expected
	echo "commit $commit_id2" >> $testroot/stdout.expected
	echo "commit $commit_id1" >> $testroot/stdout.expected
	echo "commit $commit_id0" >> $testroot/stdout.expected
	(cd $testroot/wt && env GOT_LOG_DEFAULT_LIMIT=1 got log -l0 | \
		grep ^commit > $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "0"
}

function test_log_patch_added_file {
	local testroot=`test_init log_patch_added_file`
	local commit_id0=`git_show_head $testroot/repo`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "new file" > $testroot/wt/new
	(cd $testroot/wt && got add new >/dev/null)
	(cd $testroot/wt && got commit -m 'test log_limit' > /dev/null)
	local commit_id1=`git_show_head $testroot/repo`

	echo "commit $commit_id1 (master)" > $testroot/stdout.expected
	# This used to fail with 'got: no such entry found in tree'
	(cd $testroot/wt && got log -l1 -p new > $testroot/stdout.patch)
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got log command failed unexpectedly" >&2
		test_done "$testroot" "$ret"
		return 1
	fi
	grep ^commit $testroot/stdout.patch > $testroot/stdout
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_log_nonexistent_path {
	local testroot=`test_init log_nonexistent_path`
	local head_rev=`git_show_head $testroot/repo`

	echo "commit $head_rev (master)" > $testroot/stdout.expected

	(cd $testroot/repo && got log this/does/not/exist \
		> $testroot/stdout 2> $testroot/stderr)
	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "log command succeeded unexpectedly" >&2
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

	echo "got: this/does/not/exist: no such entry found in tree" \
		> $testroot/stderr.expected
	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
	fi
	test_done "$testroot" "$ret"
}

function test_log_end_at_commit {
	local testroot=`test_init log_end_at_commit`
	local commit_id0=`git_show_head $testroot/repo`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "modified alpha" > $testroot/wt/alpha
	(cd $testroot/wt && got commit -m 'test log_limit' > /dev/null)
	local commit_id1=`git_show_head $testroot/repo`

	(cd $testroot/wt && got rm beta >/dev/null)
	(cd $testroot/wt && got commit -m 'test log_limit' > /dev/null)
	local commit_id2=`git_show_head $testroot/repo`

	echo "new file" > $testroot/wt/new
	(cd $testroot/wt && got add new >/dev/null)
	(cd $testroot/wt && got commit -m 'test log_limit' > /dev/null)
	local commit_id3=`git_show_head $testroot/repo`

	# Print commit 3 only
	echo "commit $commit_id3 (master)" > $testroot/stdout.expected
	(cd $testroot/wt && got log -x $commit_id3 | grep ^commit \
		> $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	# Print commit 3 up to commit 1 inclusive
	echo "commit $commit_id3 (master)" > $testroot/stdout.expected
	echo "commit $commit_id2" >> $testroot/stdout.expected
	echo "commit $commit_id1" >> $testroot/stdout.expected
	(cd $testroot/wt && got log -c $commit_id3 -x $commit_id1 | \
		grep ^commit > $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	# Create commits on an unrelated branch
	(cd $testroot/wt && got br foo > /dev/null)
	echo bar >> $testroot/wt/alpha
	(cd $testroot/wt && got commit -m "change on branch foo" >/dev/null)
	local commit_id4=`git_show_branch_head $testroot/repo foo`

	# Print commit 4 only (in work tree)
	echo "commit $commit_id4 (foo)" > $testroot/stdout.expected
	(cd $testroot/wt && got log -x foo | grep ^commit \
		> $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	# Print commit 4 only (in repository)
	echo "commit $commit_id4 (foo)" > $testroot/stdout.expected
	(cd $testroot/repo && got log -c foo -x foo | grep ^commit \
		> $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	# Repository's HEAD is on master branch so -x foo without an explicit
	# '-c foo' start commit has no effect there
	echo "commit $commit_id3 (master)" > $testroot/stdout.expected
	echo "commit $commit_id2" >> $testroot/stdout.expected
	echo "commit $commit_id1" >> $testroot/stdout.expected
	echo "commit $commit_id0" >> $testroot/stdout.expected
	(cd $testroot/repo && got log -x foo | grep ^commit \
		> $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	# got will refuse -x with a non-existent commit
	(cd $testroot/wt && got log -x nonexistent \
		> $testroot/stdout 2> $testroot/stderr) 
	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "log command succeeded unexpectedly" >&2
		test_done "$testroot" "1"
		return 1
	fi
	echo -n > $testroot/stdout.expected
	echo "got: nonexistent: bad object id string" \
		> $testroot/stderr.expected
	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
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

	# try the same with the hash of an empty string which is very
	# unlikely to match any object
	local empty_sha1=da39a3ee5e6b4b0d3255bfef95601890afd80709 
	(cd $testroot/wt && got log -x $empty_sha1 \
		> $testroot/stdout 2> $testroot/stderr) 
	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "log command succeeded unexpectedly" >&2
		test_done "$testroot" "1"
		return 1
	fi
	echo -n > $testroot/stdout.expected
	echo "got: $empty_sha1: object not found" > $testroot/stderr.expected
	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
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

	test_done "$testroot" "0"
}

function test_log_reverse_display {
	local testroot=`test_init log_reverse_display`
	local commit_id0=`git_show_head $testroot/repo`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "modified alpha" > $testroot/wt/alpha
	(cd $testroot/wt && got commit -m 'commit1' > /dev/null)
	local commit_id1=`git_show_head $testroot/repo`

	(cd $testroot/wt && got rm beta >/dev/null)
	(cd $testroot/wt && got commit -m 'commit2' > /dev/null)
	local commit_id2=`git_show_head $testroot/repo`

	echo "new file" > $testroot/wt/new
	(cd $testroot/wt && got add new >/dev/null)
	(cd $testroot/wt && got commit -m 'commit3' > /dev/null)
	local commit_id3=`git_show_head $testroot/repo`

	# -R alone should display all commits in reverse
	echo "commit $commit_id0" > $testroot/stdout.expected
	echo "commit $commit_id1" >> $testroot/stdout.expected
	echo "commit $commit_id2" >> $testroot/stdout.expected
	echo "commit $commit_id3 (master)" >> $testroot/stdout.expected
	(cd $testroot/wt && got log -R | grep ^commit > $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	# -R takes effect after the -l commit traversal limit
	echo "commit $commit_id2" > $testroot/stdout.expected
	echo "commit $commit_id3 (master)" >> $testroot/stdout.expected
	(cd $testroot/wt && got log -R -l2 | grep ^commit > $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	# -R works with commit ranges specified via -c and -x
	echo "commit $commit_id1" > $testroot/stdout.expected
	echo "commit $commit_id2" >> $testroot/stdout.expected
	echo "commit $commit_id3 (master)" >> $testroot/stdout.expected
	(cd $testroot/wt && got log -R -c $commit_id3 -x $commit_id1 | \
		grep ^commit > $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
	fi

	# commit matching with -s applies before -R
	echo "commit $commit_id1" > $testroot/stdout.expected
	echo "commit $commit_id2" >> $testroot/stdout.expected
	(cd $testroot/wt && got log -R -s 'commit[12]' | \
		grep ^commit > $testroot/stdout)
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_log_in_worktree_different_repo {
	local testroot=`test_init log_in_worktree_different_repo 1`

	make_test_tree $testroot/repo
	mkdir -p $testroot/repo/epsilon/d
	echo foo > $testroot/repo/epsilon/d/foo
	(cd $testroot/repo && git add .)
	git_commit $testroot/repo -m "adding the test tree"
	local head_commit=`git_show_head $testroot/repo`

	got init $testroot/other-repo
	mkdir -p $testroot/tree
	make_test_tree $testroot/tree
	got import -mm -b foo -r $testroot/other-repo $testroot/tree >/dev/null
	got checkout -b foo $testroot/other-repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "commit $head_commit (master)" > $testroot/stdout.expected

	# 'got log' used to fail with "reference refs/heads/foo not found"
	# even though that reference belongs to an unrelated repository
	# found via a worktree via the current working directory
	for p in "" alpha epsilon; do
		(cd $testroot/wt && got log -r $testroot/repo $p | \
			grep ^commit > $testroot/stdout)
		cmp -s $testroot/stdout.expected $testroot/stdout
		ret="$?"
		if [ "$ret" != "0" ]; then
			diff -u $testroot/stdout.expected $testroot/stdout
			test_done "$testroot" "$ret"
			return 1
		fi
	done

	for p in "" epsilon/zeta; do
		(cd $testroot/wt/epsilon && got log -r $testroot/repo $p | \
			grep ^commit > $testroot/stdout)
		cmp -s $testroot/stdout.expected $testroot/stdout
		ret="$?"
		if [ "$ret" != "0" ]; then
			diff -u $testroot/stdout.expected $testroot/stdout
			test_done "$testroot" "$ret"
			return 1
		fi
	done

	for p in "" foo; do
		(cd $testroot/wt/epsilon && got log -r $testroot/repo epsilon/d/$p | \
			grep ^commit > $testroot/stdout)
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

run_test test_log_in_repo
run_test test_log_in_bare_repo
run_test test_log_in_worktree
run_test test_log_in_worktree_with_path_prefix
run_test test_log_tag
run_test test_log_limit
run_test test_log_patch_added_file
run_test test_log_nonexistent_path
run_test test_log_end_at_commit
run_test test_log_reverse_display
run_test test_log_in_worktree_different_repo
