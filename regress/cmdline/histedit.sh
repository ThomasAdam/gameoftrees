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

test_histedit_no_op() {
	local testroot=`test_init histedit_no_op`

	local orig_commit=`git_show_head $testroot/repo`

	echo "modified alpha on master" > $testroot/repo/alpha
	(cd $testroot/repo && git rm -q beta)
	echo "new file on master" > $testroot/repo/epsilon/new
	(cd $testroot/repo && git add epsilon/new)
	git_commit $testroot/repo -m "committing changes"
	local old_commit1=`git_show_head $testroot/repo`

	echo "modified zeta on master" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "committing to zeta on master"
	local old_commit2=`git_show_head $testroot/repo`

	got diff -r $testroot/repo $orig_commit $old_commit2 \
		> $testroot/diff.expected

	got checkout -c $orig_commit $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "pick $old_commit1" > $testroot/histedit-script
	echo "pick $old_commit2" >> $testroot/histedit-script

	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout)

	local new_commit1=`git_show_parent_commit $testroot/repo`
	local new_commit2=`git_show_head $testroot/repo`

	local short_old_commit1=`trim_obj_id 28 $old_commit1`
	local short_old_commit2=`trim_obj_id 28 $old_commit2`
	local short_new_commit1=`trim_obj_id 28 $new_commit1`
	local short_new_commit2=`trim_obj_id 28 $new_commit2`

	echo "G  alpha" > $testroot/stdout.expected
	echo "D  beta" >> $testroot/stdout.expected
	echo "A  epsilon/new" >> $testroot/stdout.expected
	echo "$short_old_commit1 -> $short_new_commit1: committing changes" \
		>> $testroot/stdout.expected
	echo "G  epsilon/zeta" >> $testroot/stdout.expected
	echo -n "$short_old_commit2 -> $short_new_commit2: " \
		>> $testroot/stdout.expected
	echo "committing to zeta on master" >> $testroot/stdout.expected
	echo "Switching work tree to refs/heads/master" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "modified alpha on master" > $testroot/content.expected
	cat $testroot/wt/alpha > $testroot/content
	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	if [ -e $testroot/wt/beta ]; then
		echo "removed file beta still exists on disk" >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo "new file on master" > $testroot/content.expected
	cat $testroot/wt/epsilon/new > $testroot/content
	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got status > $testroot/stdout)

	echo -n > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got log -l3 | grep ^commit > $testroot/stdout)
	echo "commit $new_commit2 (master)" > $testroot/stdout.expected
	echo "commit $new_commit1" >> $testroot/stdout.expected
	echo "commit $orig_commit" >> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	got diff -r $testroot/repo $orig_commit $new_commit2 \
		> $testroot/diff
	sed -i -e "s/$old_commit2/$new_commit2/" $testroot/diff.expected
	cmp -s $testroot/diff.expected $testroot/diff
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/diff.expected $testroot/diff
	fi
	test_done "$testroot" "$ret"
}

test_histedit_swap() {
	local testroot=`test_init histedit_swap`

	local orig_commit=`git_show_head $testroot/repo`

	echo "modified alpha on master" > $testroot/repo/alpha
	(cd $testroot/repo && git rm -q beta)
	echo "new file on master" > $testroot/repo/epsilon/new
	(cd $testroot/repo && git add epsilon/new)
	git_commit $testroot/repo -m "committing changes"
	local old_commit1=`git_show_head $testroot/repo`

	echo "modified zeta on master" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "committing to zeta on master"
	local old_commit2=`git_show_head $testroot/repo`

	got diff -r $testroot/repo $orig_commit $old_commit2 \
		> $testroot/diff.expected

	got checkout -c $orig_commit $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "pick $old_commit2" > $testroot/histedit-script
	echo "pick $old_commit1" >> $testroot/histedit-script

	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout)

	local new_commit2=`git_show_parent_commit $testroot/repo`
	local new_commit1=`git_show_head $testroot/repo`

	local short_old_commit1=`trim_obj_id 28 $old_commit1`
	local short_old_commit2=`trim_obj_id 28 $old_commit2`
	local short_new_commit1=`trim_obj_id 28 $new_commit1`
	local short_new_commit2=`trim_obj_id 28 $new_commit2`

	echo "G  epsilon/zeta" > $testroot/stdout.expected
	echo -n "$short_old_commit2 -> $short_new_commit2: " \
		>> $testroot/stdout.expected
	echo "committing to zeta on master" >> $testroot/stdout.expected
	echo "G  alpha" >> $testroot/stdout.expected
	echo "D  beta" >> $testroot/stdout.expected
	echo "A  epsilon/new" >> $testroot/stdout.expected
	echo "$short_old_commit1 -> $short_new_commit1: committing changes" \
		>> $testroot/stdout.expected
	echo "Switching work tree to refs/heads/master" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "modified alpha on master" > $testroot/content.expected
	cat $testroot/wt/alpha > $testroot/content
	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	if [ -e $testroot/wt/beta ]; then
		echo "removed file beta still exists on disk" >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo "new file on master" > $testroot/content.expected
	cat $testroot/wt/epsilon/new > $testroot/content
	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got status > $testroot/stdout)

	echo -n > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got log -l3 | grep ^commit > $testroot/stdout)
	echo "commit $new_commit1 (master)" > $testroot/stdout.expected
	echo "commit $new_commit2" >> $testroot/stdout.expected
	echo "commit $orig_commit" >> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	got diff -r $testroot/repo $orig_commit $new_commit1 \
		> $testroot/diff
	sed -i -e "s/$old_commit2/$new_commit1/" $testroot/diff.expected
	cmp -s $testroot/diff.expected $testroot/diff
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/diff.expected $testroot/diff
	fi
	test_done "$testroot" "$ret"
}

test_histedit_drop() {
	local testroot=`test_init histedit_drop`
	local orig_commit=`git_show_head $testroot/repo`

	echo "modified alpha on master" > $testroot/repo/alpha
	(cd $testroot/repo && git rm -q beta)
	echo "new file on master" > $testroot/repo/epsilon/new
	(cd $testroot/repo && git add epsilon/new)
	git_commit $testroot/repo -m "committing changes"
	local old_commit1=`git_show_head $testroot/repo`

	echo "modified zeta on master" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "committing to zeta on master"
	local old_commit2=`git_show_head $testroot/repo`

	got diff -r $testroot/repo $old_commit1 $old_commit2 \
		> $testroot/diff.expected

	got checkout -c $orig_commit $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "drop $old_commit1" > $testroot/histedit-script
	echo "pick $old_commit2" >> $testroot/histedit-script

	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout)

	local new_commit2=`git_show_head $testroot/repo`

	local short_old_commit1=`trim_obj_id 28 $old_commit1`
	local short_old_commit2=`trim_obj_id 28 $old_commit2`
	local short_new_commit2=`trim_obj_id 28 $new_commit2`

	echo "$short_old_commit1 ->  drop commit: committing changes" \
		> $testroot/stdout.expected
	echo "G  epsilon/zeta" >> $testroot/stdout.expected
	echo -n "$short_old_commit2 -> $short_new_commit2: " \
		>> $testroot/stdout.expected
	echo "committing to zeta on master" >> $testroot/stdout.expected
	echo "Switching work tree to refs/heads/master" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	for f in alpha beta; do
		echo "$f" > $testroot/content.expected
		cat $testroot/wt/$f > $testroot/content
		cmp -s $testroot/content.expected $testroot/content
		ret="$?"
		if [ "$ret" != "0" ]; then
			diff -u $testroot/content.expected $testroot/content
			test_done "$testroot" "$ret"
			return 1
		fi
	done

	if [ -e $testroot/wt/new ]; then
		echo "file new exists on disk but should not" >&2
		test_done "$testroot" "1"
		return 1
	fi

	(cd $testroot/wt && got status > $testroot/stdout)

	echo -n > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got log -l3 | grep ^commit > $testroot/stdout)
	echo "commit $new_commit2 (master)" > $testroot/stdout.expected
	echo "commit $orig_commit" >> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	got diff -r $testroot/repo $orig_commit $new_commit2 \
		> $testroot/diff
	sed -i -e "s/$old_commit1/$orig_commit/" $testroot/diff.expected
	sed -i -e "s/$old_commit2/$new_commit2/" $testroot/diff.expected
	cmp -s $testroot/diff.expected $testroot/diff
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/diff.expected $testroot/diff
	fi
	test_done "$testroot" "$ret"
}

test_histedit_fold() {
	local testroot=`test_init histedit_fold`

	local orig_commit=`git_show_head $testroot/repo`

	echo "modified alpha on master" > $testroot/repo/alpha
	(cd $testroot/repo && git rm -q beta)
	echo "new file on master" > $testroot/repo/epsilon/new
	(cd $testroot/repo && git add epsilon/new)
	git_commit $testroot/repo -m "committing changes"
	local old_commit1=`git_show_head $testroot/repo`

	echo "modified zeta on master" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "committing to zeta on master"
	local old_commit2=`git_show_head $testroot/repo`

	echo "modified delta on master" > $testroot/repo/gamma/delta
	git_commit $testroot/repo -m "committing to delta on master"
	local old_commit3=`git_show_head $testroot/repo`

	got checkout -c $orig_commit $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "fold $old_commit1" > $testroot/histedit-script
	echo "drop $old_commit2" >> $testroot/histedit-script
	echo "pick $old_commit3" >> $testroot/histedit-script
	echo "mesg committing folded changes" >> $testroot/histedit-script

	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout)

	local new_commit1=`git_show_parent_commit $testroot/repo`
	local new_commit2=`git_show_head $testroot/repo`

	local short_old_commit1=`trim_obj_id 28 $old_commit1`
	local short_old_commit2=`trim_obj_id 28 $old_commit2`
	local short_old_commit3=`trim_obj_id 28 $old_commit3`
	local short_new_commit1=`trim_obj_id 28 $new_commit1`
	local short_new_commit2=`trim_obj_id 28 $new_commit2`

	echo "G  alpha" > $testroot/stdout.expected
	echo "D  beta" >> $testroot/stdout.expected
	echo "A  epsilon/new" >> $testroot/stdout.expected
	echo "$short_old_commit1 ->  fold commit: committing changes" \
		>> $testroot/stdout.expected
	echo -n "$short_old_commit2 ->  " >> $testroot/stdout.expected
	echo "drop commit: committing to zeta on master" \
		>> $testroot/stdout.expected
	echo "G  gamma/delta" >> $testroot/stdout.expected
	echo -n "$short_old_commit3 -> $short_new_commit2: " \
		>> $testroot/stdout.expected
	echo "committing folded changes" >> $testroot/stdout.expected
	echo "Switching work tree to refs/heads/master" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "modified alpha on master" > $testroot/content.expected
	cat $testroot/wt/alpha > $testroot/content
	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	if [ -e $testroot/wt/beta ]; then
		echo "removed file beta still exists on disk" >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo "new file on master" > $testroot/content.expected
	cat $testroot/wt/epsilon/new > $testroot/content
	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got status > $testroot/stdout)

	echo -n > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got log -l3 | grep ^commit > $testroot/stdout)
	echo "commit $new_commit2 (master)" > $testroot/stdout.expected
	echo "commit $orig_commit" >> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

test_histedit_edit() {
	local testroot=`test_init histedit_edit`

	local orig_commit=`git_show_head $testroot/repo`

	echo "modified alpha on master" > $testroot/repo/alpha
	(cd $testroot/repo && git rm -q beta)
	echo "new file on master" > $testroot/repo/epsilon/new
	(cd $testroot/repo && git add epsilon/new)
	git_commit $testroot/repo -m "committing changes"
	local old_commit1=`git_show_head $testroot/repo`

	echo "modified zeta on master" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "committing to zeta on master"
	local old_commit2=`git_show_head $testroot/repo`

	got checkout -c $orig_commit $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "edit $old_commit1" > $testroot/histedit-script
	echo "mesg committing changes" >> $testroot/histedit-script
	echo "pick $old_commit2" >> $testroot/histedit-script

	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout)

	local short_old_commit1=`trim_obj_id 28 $old_commit1`
	local short_old_commit2=`trim_obj_id 28 $old_commit2`

	echo "G  alpha" > $testroot/stdout.expected
	echo "D  beta" >> $testroot/stdout.expected
	echo "A  epsilon/new" >> $testroot/stdout.expected
	echo "Stopping histedit for amending commit $old_commit1" \
		>> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "edited modified alpha on master" > $testroot/wt/alpha

	# test interaction of 'got stage' and histedit -c
	(cd $testroot/wt && got stage alpha > /dev/null)
	(cd $testroot/wt && got histedit -c > $testroot/stdout \
		2> $testroot/stderr)
	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "histedit succeeded unexpectedly" >&2
		test_done "$testroot" "1"
		return 1
	fi
	echo -n "got: work tree contains files with staged changes; " \
		> $testroot/stderr.expected
	echo "these changes must be committed or unstaged first" \
		>> $testroot/stderr.expected
	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got unstage alpha > /dev/null)
	(cd $testroot/wt && got histedit -c > $testroot/stdout)

	local new_commit1=`git_show_parent_commit $testroot/repo`
	local new_commit2=`git_show_head $testroot/repo`

	local short_new_commit1=`trim_obj_id 28 $new_commit1`
	local short_new_commit2=`trim_obj_id 28 $new_commit2`

	echo "$short_old_commit1 -> $short_new_commit1: committing changes" \
		> $testroot/stdout.expected
	echo "G  epsilon/zeta" >> $testroot/stdout.expected
	echo -n "$short_old_commit2 -> $short_new_commit2: " \
		>> $testroot/stdout.expected
	echo "committing to zeta on master" >> $testroot/stdout.expected
	echo "Switching work tree to refs/heads/master" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "edited modified alpha on master" > $testroot/content.expected
	cat $testroot/wt/alpha > $testroot/content
	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	if [ -e $testroot/wt/beta ]; then
		echo "removed file beta still exists on disk" >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo "new file on master" > $testroot/content.expected
	cat $testroot/wt/epsilon/new > $testroot/content
	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got status > $testroot/stdout)

	echo -n > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got log -l3 | grep ^commit > $testroot/stdout)
	echo "commit $new_commit2 (master)" > $testroot/stdout.expected
	echo "commit $new_commit1" >> $testroot/stdout.expected
	echo "commit $orig_commit" >> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

test_histedit_fold_last_commit() {
	local testroot=`test_init histedit_fold_last_commit`

	local orig_commit=`git_show_head $testroot/repo`

	echo "modified alpha on master" > $testroot/repo/alpha
	(cd $testroot/repo && git rm -q beta)
	echo "new file on master" > $testroot/repo/epsilon/new
	(cd $testroot/repo && git add epsilon/new)
	git_commit $testroot/repo -m "committing changes"
	local old_commit1=`git_show_head $testroot/repo`

	echo "modified zeta on master" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "committing to zeta on master"
	local old_commit2=`git_show_head $testroot/repo`

	got checkout -c $orig_commit $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "pick $old_commit1" > $testroot/histedit-script
	echo "fold $old_commit2" >> $testroot/histedit-script
	echo "mesg committing folded changes" >> $testroot/histedit-script

	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout 2> $testroot/stderr)

	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "histedit succeeded unexpectedly" >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo "got: last commit in histedit script cannot be folded" \
		> $testroot/stderr.expected

	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
	fi
	test_done "$testroot" "$ret"
}

test_histedit_missing_commit() {
	local testroot=`test_init histedit_missing_commit`

	local orig_commit=`git_show_head $testroot/repo`

	echo "modified alpha on master" > $testroot/repo/alpha
	(cd $testroot/repo && git rm -q beta)
	echo "new file on master" > $testroot/repo/epsilon/new
	(cd $testroot/repo && git add epsilon/new)
	git_commit $testroot/repo -m "committing changes"
	local old_commit1=`git_show_head $testroot/repo`

	echo "modified zeta on master" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "committing to zeta on master"
	local old_commit2=`git_show_head $testroot/repo`

	got checkout -c $orig_commit $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "pick $old_commit1" > $testroot/histedit-script
	echo "mesg committing changes" >> $testroot/histedit-script

	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout 2> $testroot/stderr)

	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "histedit succeeded unexpectedly" >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo "got: commit $old_commit2 missing from histedit script" \
		> $testroot/stderr.expected

	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
	fi
	test_done "$testroot" "$ret"
}

test_histedit_abort() {
	local testroot=`test_init histedit_abort`

	local orig_commit=`git_show_head $testroot/repo`

	echo "modified alpha on master" > $testroot/repo/alpha
	(cd $testroot/repo && git rm -q beta)
	echo "new file on master" > $testroot/repo/epsilon/new
	(cd $testroot/repo && git add epsilon/new)
	git_commit $testroot/repo -m "committing changes"
	local old_commit1=`git_show_head $testroot/repo`

	echo "modified zeta on master" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "committing to zeta on master"
	local old_commit2=`git_show_head $testroot/repo`

	got checkout -c $orig_commit $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "edit $old_commit1" > $testroot/histedit-script
	echo "mesg committing changes" >> $testroot/histedit-script
	echo "pick $old_commit2" >> $testroot/histedit-script

	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout)

	local short_old_commit1=`trim_obj_id 28 $old_commit1`
	local short_old_commit2=`trim_obj_id 28 $old_commit2`

	echo "G  alpha" > $testroot/stdout.expected
	echo "D  beta" >> $testroot/stdout.expected
	echo "A  epsilon/new" >> $testroot/stdout.expected
	echo "Stopping histedit for amending commit $old_commit1" \
		>> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "edited modified alpha on master" > $testroot/wt/alpha

	(cd $testroot/wt && got histedit -a > $testroot/stdout)

	local new_commit1=`git_show_parent_commit $testroot/repo`
	local new_commit2=`git_show_head $testroot/repo`

	echo "Switching work tree to refs/heads/master" \
		> $testroot/stdout.expected
	echo "R  alpha" >> $testroot/stdout.expected
	echo "R  beta" >> $testroot/stdout.expected
	echo "R  epsilon/new" >> $testroot/stdout.expected
	echo "Histedit of refs/heads/master aborted" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	for f in alpha beta; do
		echo "$f" > $testroot/content.expected
		cat $testroot/wt/$f > $testroot/content
		cmp -s $testroot/content.expected $testroot/content
		ret="$?"
		if [ "$ret" != "0" ]; then
			diff -u $testroot/content.expected $testroot/content
			test_done "$testroot" "$ret"
			return 1
		fi
	done

	echo "new file on master" > $testroot/content.expected
	cat $testroot/wt/epsilon/new > $testroot/content
	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got status > $testroot/stdout)

	echo "?  epsilon/new" > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got log -l3 | grep ^commit > $testroot/stdout)
	echo "commit $new_commit2 (master)" > $testroot/stdout.expected
	echo "commit $new_commit1" >> $testroot/stdout.expected
	echo "commit $orig_commit" >> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

test_histedit_path_prefix_drop() {
	local testroot=`test_init histedit_path_prefix_drop`
	local orig_commit=`git_show_head $testroot/repo`

	echo "modified zeta" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "changing zeta"
	local old_commit1=`git_show_head $testroot/repo`

	got checkout -c $orig_commit -p gamma $testroot/repo \
		$testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "drop $old_commit1" > $testroot/histedit-script

	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout 2> $testroot/stderr)

	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "histedit succeeded unexpectedly" >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo -n "got: cannot edit branch history which contains changes " \
		> $testroot/stderr.expected
	echo "outside of this work tree's path prefix" \
		>> $testroot/stderr.expected

	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
		test_done "$testroot" "$ret"
		return 1
	fi

	rm -rf $testroot/wt
	got checkout -c $orig_commit -p epsilon $testroot/repo \
		$testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi
	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout)

	local short_old_commit1=`trim_obj_id 28 $old_commit1`
	local short_old_commit2=`trim_obj_id 28 $old_commit2`

	echo "$short_old_commit1 ->  drop commit: changing zeta" \
		> $testroot/stdout.expected
	echo "Switching work tree to refs/heads/master" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "zeta" > $testroot/content.expected
	cat $testroot/wt/zeta > $testroot/content
	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi


	(cd $testroot/wt && got status > $testroot/stdout)

	echo -n > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got log -l3 | grep ^commit > $testroot/stdout)
	echo "commit $orig_commit (master)" > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

test_histedit_path_prefix_edit() {
	local testroot=`test_init histedit_path_prefix_edit`
	local orig_commit=`git_show_head $testroot/repo`

	echo "modified zeta" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "changing zeta"
	local old_commit1=`git_show_head $testroot/repo`

	got diff -r $testroot/repo $orig_commit $old_commit1 \
		> $testroot/diff.expected

	got checkout -c $orig_commit -p gamma $testroot/repo \
		$testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "edit $old_commit1" > $testroot/histedit-script
	echo "mesg modified zeta" >> $testroot/histedit-script

	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout 2> $testroot/stderr)

	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "histedit succeeded unexpectedly" >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo -n "got: cannot edit branch history which contains changes " \
		> $testroot/stderr.expected
	echo "outside of this work tree's path prefix" \
		>> $testroot/stderr.expected

	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
		test_done "$testroot" "$ret"
		return 1
	fi

	rm -rf $testroot/wt
	got checkout -c $orig_commit -p epsilon $testroot/repo \
		$testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi
	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout)

	local short_old_commit1=`trim_obj_id 28 $old_commit1`

	echo "G  zeta" > $testroot/stdout.expected
	echo "Stopping histedit for amending commit $old_commit1" \
		>> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "modified zeta" > $testroot/content.expected
	cat $testroot/wt/zeta > $testroot/content
	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got status > $testroot/stdout)

	echo "M  zeta"> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got histedit -c > $testroot/stdout)

	local new_commit1=`git_show_head $testroot/repo`
	local short_new_commit1=`trim_obj_id 28 $new_commit1`

	echo -n "$short_old_commit1 -> $short_new_commit1: " \
		> $testroot/stdout.expected
	echo "modified zeta" >> $testroot/stdout.expected
	echo "Switching work tree to refs/heads/master" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got log -l3 | grep ^commit > $testroot/stdout)
	echo "commit $new_commit1 (master)" > $testroot/stdout.expected
	echo "commit $orig_commit" >> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	got diff -r $testroot/repo $orig_commit $new_commit1 \
		> $testroot/diff
	sed -i -e "s/$old_commit1/$new_commit1/" $testroot/diff.expected
	cmp -s $testroot/diff.expected $testroot/diff
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/diff.expected $testroot/diff
	fi
	test_done "$testroot" "$ret"
}

test_histedit_outside_refs_heads() {
	local testroot=`test_init histedit_outside_refs_heads`
	local commit1=`git_show_head $testroot/repo`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got checkout failed unexpectedly"
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "modified alpha" > $testroot/wt/alpha

	(cd $testroot/wt && got commit -m 'change alpha' \
		> $testroot/stdout 2> $testroot/stderr)
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got commit failed unexpectedly" >&2
		test_done "$testroot" "1"
		return 1
	fi
	local commit2=`git_show_head $testroot/repo`

	got ref -r $testroot/repo -c master refs/remotes/origin/master
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got ref failed unexpectedly" >&2
		test_done "$testroot" "1"
		return 1
	fi

	(cd $testroot/wt && got update -b origin/master -c $commit1 >/dev/null)
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "got update failed unexpectedly"
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "edit $commit2" > $testroot/histedit-script
	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		2> $testroot/stderr)

	echo -n "got: will not edit commit history of a branch outside the " \
		> $testroot/stderr.expected
	echo '"refs/heads/" reference namespace' \
		>> $testroot/stderr.expected
	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
	fi
	test_done "$testroot" "$ret"
}

test_histedit_fold_last_commit_swap() {
	local testroot=`test_init histedit_fold_last_commit_swap`

	local orig_commit=`git_show_head $testroot/repo`

	echo "modified alpha on master" > $testroot/repo/alpha
	(cd $testroot/repo && git rm -q beta)
	echo "new file on master" > $testroot/repo/epsilon/new
	(cd $testroot/repo && git add epsilon/new)
	git_commit $testroot/repo -m "committing changes"
	local old_commit1=`git_show_head $testroot/repo`

	echo "modified zeta on master" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "committing to zeta on master"
	local old_commit2=`git_show_head $testroot/repo`

	got checkout -c $orig_commit $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	# fold commit2 into commit1 (requires swapping commits)
	echo "fold $old_commit2" > $testroot/histedit-script
	echo "pick $old_commit1" >> $testroot/histedit-script
	echo "mesg committing folded changes" >> $testroot/histedit-script

	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout 2> $testroot/stderr)

	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "histedit failed unexpectedly" >&2
		test_done "$testroot" "$ret"
		return 1
	fi

	local new_commit=`git_show_head $testroot/repo`

	local short_old_commit1=`trim_obj_id 28 $old_commit1`
	local short_old_commit2=`trim_obj_id 28 $old_commit2`
	local short_new_commit=`trim_obj_id 28 $new_commit`

	echo "G  epsilon/zeta" >> $testroot/stdout.expected
	echo -n "$short_old_commit2 ->  fold commit: committing to zeta " \
		>> $testroot/stdout.expected
	echo "on master" >> $testroot/stdout.expected
	echo "G  alpha" >> $testroot/stdout.expected
	echo "D  beta" >> $testroot/stdout.expected
	echo "A  epsilon/new" >> $testroot/stdout.expected
	echo -n "$short_old_commit1 -> $short_new_commit: " \
		>> $testroot/stdout.expected
	echo "committing folded changes" >> $testroot/stdout.expected
	echo "Switching work tree to refs/heads/master" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

test_histedit_split_commit() {
	local testroot=`test_init histedit_split_commit`

	local orig_commit=`git_show_head $testroot/repo`

	echo "modified alpha on master" > $testroot/repo/alpha
	(cd $testroot/repo && git rm -q beta)
	echo "new file on master" > $testroot/repo/epsilon/new
	(cd $testroot/repo && git add epsilon/new)
	git_commit $testroot/repo -m "committing changes 1"
	local old_commit1=`git_show_head $testroot/repo`
	local short_old_commit1=`trim_obj_id 28 $old_commit1`

	echo "modified zeta on master" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "committing changes 2"
	local old_commit2=`git_show_head $testroot/repo`
	local short_old_commit2=`trim_obj_id 28 $old_commit2`

	got checkout -c $orig_commit $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	# split commit1 into commitA and commitB and commitC
	echo "e $old_commit1" > $testroot/histedit-script
	echo "p $old_commit2" >> $testroot/histedit-script

	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout 2> $testroot/stderr)
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "histedit failed unexpectedly:" >&2
		cat $testroot/stderr >&2
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "G  alpha" > $testroot/stdout.expected
	echo "D  beta" >> $testroot/stdout.expected
	echo "A  epsilon/new" >> $testroot/stdout.expected
	echo "Stopping histedit for amending commit $old_commit1" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got ci -m "commitA" alpha >/dev/null)
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "commit failed unexpectedly" >&2
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got ci -m "commitB" beta >/dev/null)
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "commit failed unexpectedly" >&2
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got ci -m "commitC" epsilon/new >/dev/null)
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "commit failed unexpectedly" >&2
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got histedit -c \
		> $testroot/stdout 2> $testroot/stderr)
	ret="$?"
	if [ "$ret" != "0" ]; then
		echo "histedit failed unexpectedly:" >&2
		cat $testroot/stderr >&2
		test_done "$testroot" "$ret"
		return 1
	fi
	local new_commit2=`git_show_head $testroot/repo`
	local short_new_commit2=`trim_obj_id 28 $new_commit2`

	echo "$short_old_commit1 -> no-op change: committing changes 1" \
		> $testroot/stdout.expected
	echo "G  epsilon/zeta" >> $testroot/stdout.expected
	echo "$short_old_commit2 -> $short_new_commit2: committing changes 2" \
		>> $testroot/stdout.expected
	echo "Switching work tree to refs/heads/master" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"

}

test_histedit_duplicate_commit_in_script() {
	local testroot=`test_init histedit_duplicate_commit_in_script`

	local orig_commit=`git_show_head $testroot/repo`

	echo "modified alpha on master" > $testroot/repo/alpha
	(cd $testroot/repo && git rm -q beta)
	echo "new file on master" > $testroot/repo/epsilon/new
	(cd $testroot/repo && git add epsilon/new)
	git_commit $testroot/repo -m "committing changes 1"
	local old_commit1=`git_show_head $testroot/repo`

	echo "modified zeta on master" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "committing changes 2"
	local old_commit2=`git_show_head $testroot/repo`

	got checkout -c $orig_commit $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	# This histedit script lists commit1 more than once
	echo "p $old_commit1" > $testroot/histedit-script
	echo "p $old_commit1" >> $testroot/histedit-script
	echo "p $old_commit2" >> $testroot/histedit-script

	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout 2> $testroot/stderr)
	ret="$?"
	if [ "$ret" == "0" ]; then
		echo "histedit succeeded unexpectedly:" >&2
		cat $testroot/stdout >&2
		test_done "$testroot" "$ret"
		return 1
	fi

	echo -n "got: commit $old_commit1 is listed more than once " \
		> $testroot/stderr.expected
	echo "in histedit script" >> $testroot/stderr.expected

	cmp -s $testroot/stderr.expected $testroot/stderr
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stderr.expected $testroot/stderr
	fi
	test_done "$testroot" "$ret"

}

# if a previous commit introduces a new file, and it is folded into a commit
# that deletes the same file, the file still exists after the histedit
test_histedit_fold_add_delete() {
	local testroot=`test_init histedit_fold`

	local orig_commit=`git_show_head $testroot/repo`

	echo "added new file epsilon/psi" > $testroot/repo/epsilon/psi
	(cd $testroot/repo && git add epsilon/psi)
	git_commit $testroot/repo -m "committing changes"
	local old_commit1=`git_show_head $testroot/repo`

	echo "modified epsilon/psi" > $testroot/repo/epsilon/psi
	git_commit $testroot/repo -m "editing psi"
	local old_commit2=`git_show_head $testroot/repo`

	(cd $testroot/repo && git rm -q epsilon/psi)
	git_commit $testroot/repo -m "removing psi"
	local old_commit3=`git_show_head $testroot/repo`

	got checkout -c $orig_commit $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "fold $old_commit1" > $testroot/histedit-script
	echo "fold $old_commit2" >> $testroot/histedit-script
	echo "pick $old_commit3" >> $testroot/histedit-script
	echo "mesg folded changes" >> $testroot/histedit-script

	(cd $testroot/wt && got histedit -F $testroot/histedit-script \
		> $testroot/stdout)

	local new_commit1=`git_show_head $testroot/repo`

	local short_old_commit1=`trim_obj_id 28 $old_commit1`
	local short_old_commit2=`trim_obj_id 28 $old_commit2`
	local short_old_commit3=`trim_obj_id 28 $old_commit3`
	local short_new_commit1=`trim_obj_id 28 $new_commit1`

	echo "A  epsilon/psi" >> $testroot/stdout.expected
	echo "$short_old_commit1 ->  fold commit: committing changes" \
		>> $testroot/stdout.expected
	echo "G  epsilon/psi" >> $testroot/stdout.expected
	echo "$short_old_commit2 ->  fold commit: editing psi" \
		>> $testroot/stdout.expected
	echo "D  epsilon/psi" >> $testroot/stdout.expected
	echo "$short_old_commit3 -> no-op change: folded changes" \
		>> $testroot/stdout.expected
	echo "Switching work tree to refs/heads/master" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	if [ -e $testroot/wt/epsilon/psi ]; then
		echo "removed file psi still exists on disk" >&2
		test_done "$testroot" "1"
		return 1
	fi

	(cd $testroot/wt && got status > $testroot/stdout)

	echo -n > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got log -l3 | grep ^commit > $testroot/stdout)
	echo "commit $new_commit1 (master)" > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	got tree -r $testroot/repo epsilon > $testroot/stdout
	echo "zeta" > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

test_histedit_fold_only() {
	local testroot=`test_init histedit_fold_only`

	local orig_commit=`git_show_head $testroot/repo`

	echo "modified alpha on master" > $testroot/repo/alpha
	(cd $testroot/repo && git rm -q beta)
	echo "new file on master" > $testroot/repo/epsilon/new
	(cd $testroot/repo && git add epsilon/new)
	git_commit $testroot/repo -m "committing changes"
	local old_commit1=`git_show_head $testroot/repo`

	echo "modified zeta on master" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "committing to zeta on master"
	local old_commit2=`git_show_head $testroot/repo`

	echo "modified delta on master" > $testroot/repo/gamma/delta
	git_commit $testroot/repo -m "committing to delta on master"
	local old_commit3=`git_show_head $testroot/repo`

	got checkout -c $orig_commit $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	cat > $testroot/editor.sh <<EOF
#!/bin/sh
sed -i 's/.*/committing folded changes/' "\$1"
EOF
	chmod +x $testroot/editor.sh

	(cd $testroot/wt && env EDITOR="$testroot/editor.sh" \
		VISUAL="$testroot/editor.sh" got histedit -f > $testroot/stdout)

	local new_commit1=`git_show_head $testroot/repo`

	local short_old_commit1=`trim_obj_id 28 $old_commit1`
	local short_old_commit2=`trim_obj_id 28 $old_commit2`
	local short_old_commit3=`trim_obj_id 28 $old_commit3`
	local short_new_commit1=`trim_obj_id 28 $new_commit1`
	local short_new_commit2=`trim_obj_id 28 $new_commit2`

	echo "G  alpha" > $testroot/stdout.expected
	echo "D  beta" >> $testroot/stdout.expected
	echo "A  epsilon/new" >> $testroot/stdout.expected
	echo "$short_old_commit1 ->  fold commit: committing changes" \
		>> $testroot/stdout.expected
	echo "G  epsilon/zeta" >> $testroot/stdout.expected
	echo -n "$short_old_commit2 ->  " >> $testroot/stdout.expected
	echo "fold commit: committing to zeta on master" \
		>> $testroot/stdout.expected
	echo "G  gamma/delta" >> $testroot/stdout.expected
	echo -n "$short_old_commit3 -> $short_new_commit1: " \
		>> $testroot/stdout.expected
	echo "committing folded changes" >> $testroot/stdout.expected
	echo "Switching work tree to refs/heads/master" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "modified alpha on master" > $testroot/content.expected
	cat $testroot/wt/alpha > $testroot/content
	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	if [ -e $testroot/wt/beta ]; then
		echo "removed file beta still exists on disk" >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo "new file on master" > $testroot/content.expected
	cat $testroot/wt/epsilon/new > $testroot/content
	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got status > $testroot/stdout)

	echo -n > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got log | grep ^commit > $testroot/stdout)
	echo "commit $new_commit1 (master)" > $testroot/stdout.expected
	echo "commit $orig_commit" >> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

test_histedit_fold_only_empty_logmsg() {
	local testroot=`test_init histedit_fold_only_empty_logmsg`

	local orig_commit=`git_show_head $testroot/repo`

	echo "modified alpha on master" > $testroot/repo/alpha
	(cd $testroot/repo && git rm -q beta)
	echo "new file on master" > $testroot/repo/epsilon/new
	(cd $testroot/repo && git add epsilon/new)
	git_commit $testroot/repo -m "committing changes"
	local old_commit1=`git_show_head $testroot/repo`

	echo "modified zeta on master" > $testroot/repo/epsilon/zeta
	git_commit $testroot/repo -m "committing to zeta on master"
	local old_commit2=`git_show_head $testroot/repo`

	echo "modified delta on master" > $testroot/repo/gamma/delta
	git_commit $testroot/repo -m "committing to delta on master"
	local old_commit3=`git_show_head $testroot/repo`

	got checkout -c $orig_commit $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	cat > $testroot/editor.sh <<EOF
#!/bin/sh
sed -i 'd' "\$1"
EOF
	chmod +x $testroot/editor.sh

	(cd $testroot/wt && env EDITOR="$testroot/editor.sh" \
		VISUAL="$testroot/editor.sh" got histedit -f > $testroot/stdout)

	local new_commit1=`git_show_head $testroot/repo`

	local short_old_commit1=`trim_obj_id 28 $old_commit1`
	local very_short_old_commit1=`trim_obj_id 29 $old_commit1`
	local short_old_commit2=`trim_obj_id 28 $old_commit2`
	local short_old_commit3=`trim_obj_id 28 $old_commit3`
	local short_new_commit1=`trim_obj_id 28 $new_commit1`
	local short_new_commit2=`trim_obj_id 28 $new_commit2`

	echo "G  alpha" > $testroot/stdout.expected
	echo "D  beta" >> $testroot/stdout.expected
	echo "A  epsilon/new" >> $testroot/stdout.expected
	echo "$short_old_commit1 ->  fold commit: committing changes" \
		>> $testroot/stdout.expected
	echo "G  epsilon/zeta" >> $testroot/stdout.expected
	echo -n "$short_old_commit2 ->  " >> $testroot/stdout.expected
	echo "fold commit: committing to zeta on master" \
		>> $testroot/stdout.expected
	echo "G  gamma/delta" >> $testroot/stdout.expected
	echo -n "$short_old_commit3 -> $short_new_commit1: " \
		>> $testroot/stdout.expected
	echo "# log message of folded commit $very_short_old_commit1" \
		>> $testroot/stdout.expected
	echo "Switching work tree to refs/heads/master" \
		>> $testroot/stdout.expected

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "modified alpha on master" > $testroot/content.expected
	cat $testroot/wt/alpha > $testroot/content
	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	if [ -e $testroot/wt/beta ]; then
		echo "removed file beta still exists on disk" >&2
		test_done "$testroot" "1"
		return 1
	fi

	echo "new file on master" > $testroot/content.expected
	cat $testroot/wt/epsilon/new > $testroot/content
	cmp -s $testroot/content.expected $testroot/content
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/content.expected $testroot/content
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got status > $testroot/stdout)

	echo -n > $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	(cd $testroot/wt && got log | grep ^commit > $testroot/stdout)
	echo "commit $new_commit1 (master)" > $testroot/stdout.expected
	echo "commit $orig_commit" >> $testroot/stdout.expected
	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

test_parseargs "$@"
run_test test_histedit_no_op
run_test test_histedit_swap
run_test test_histedit_drop
run_test test_histedit_fold
run_test test_histedit_edit
run_test test_histedit_fold_last_commit
run_test test_histedit_missing_commit
run_test test_histedit_abort
run_test test_histedit_path_prefix_drop
run_test test_histedit_path_prefix_edit
run_test test_histedit_outside_refs_heads
run_test test_histedit_fold_last_commit_swap
run_test test_histedit_split_commit
run_test test_histedit_duplicate_commit_in_script
run_test test_histedit_fold_add_delete
run_test test_histedit_fold_only
run_test test_histedit_fold_only_empty_logmsg
