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

function test_status_basic {
	local testroot=`test_init status_basic`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "modified alpha" > $testroot/wt/alpha
	(cd $testroot/wt && got rm beta >/dev/null)
	echo "unversioned file" > $testroot/wt/foo
	rm $testroot/wt/epsilon/zeta
	touch $testroot/wt/beta
	echo "new file" > $testroot/wt/new
	(cd $testroot/wt && got add new >/dev/null)
	mkdir -m 0000 $testroot/wt/bar

	echo 'M  alpha' > $testroot/stdout.expected
	echo 'D  beta' >> $testroot/stdout.expected
	echo '!  epsilon/zeta' >> $testroot/stdout.expected
	echo '?  foo' >> $testroot/stdout.expected
	echo 'A  new' >> $testroot/stdout.expected

	(cd $testroot/wt && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	chmod 700 $testroot/wt/bar
	rmdir $testroot/wt/bar
	test_done "$testroot" "$ret"
}

function test_status_subdir_no_mods {
	local testroot=`test_init status_subdir_no_mods 1`

	mkdir $testroot/repo/Basic/
	mkdir $testroot/repo/Basic/Targets/
	touch $testroot/repo/Basic/Targets/AArch64.cpp
	touch $testroot/repo/Basic/Targets.cpp
	touch $testroot/repo/Basic/Targets.h
	(cd $testroot/repo && git add .)
	git_commit $testroot/repo -m "add subdir with files"

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	touch $testroot/stdout.expected

	# This used to erroneously print:
	#
	# !  Basic/Targets.cpp
	# ?  Basic/Targets.cpp
	(cd $testroot/wt && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_status_subdir_no_mods2 {
	local testroot=`test_init status_subdir_no_mods2 1`

	mkdir $testroot/repo/AST
	touch $testroot/repo/AST/APValue.cpp
	mkdir $testroot/repo/ASTMatchers
	touch $testroot/repo/ASTMatchers/ASTMatchFinder.cpp
	mkdir $testroot/repo/Frontend
	touch $testroot/repo/Frontend/ASTConsumers.cpp
	mkdir $testroot/repo/Frontend/Rewrite
	touch $testroot/repo/Frontend/Rewrite/CMakeLists.txt
	mkdir $testroot/repo/FrontendTool
	touch $testroot/repo/FrontendTool/CMakeLists.txt
	touch $testroot/repo/FrontendTool/ExecuteCompilerInvocation.cpp
	(cd $testroot/repo && git add .)
	git_commit $testroot/repo -m "add subdir with files"

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	touch $testroot/stdout.expected

	# This used to erroneously print:
	#
	# !  AST/APValue.cpp
	# ?  AST/APValue.cpp
	# !  Frontend/ASTConsumers.cpp
	# !  Frontend/Rewrite/CMakeLists.txt
	# ?  Frontend/ASTConsumers.cpp
	# ?  Frontend/Rewrite/CMakeLists.txt
	(cd $testroot/wt && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_status_obstructed {
	local testroot=`test_init status_obstructed`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	rm $testroot/wt/epsilon/zeta
	mkdir $testroot/wt/epsilon/zeta

	echo '~  epsilon/zeta' > $testroot/stdout.expected

	(cd $testroot/wt && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_status_shows_local_mods_after_update {
	local testroot=`test_init status_shows_local_mods_after_update 1`

	echo "1" > $testroot/repo/numbers
	echo "2" >> $testroot/repo/numbers
	echo "3" >> $testroot/repo/numbers
	echo "4" >> $testroot/repo/numbers
	echo "5" >> $testroot/repo/numbers
	echo "6" >> $testroot/repo/numbers
	echo "7" >> $testroot/repo/numbers
	echo "8" >> $testroot/repo/numbers
	(cd $testroot/repo && git add numbers)
	git_commit $testroot/repo -m "added numbers file"

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	sed -i 's/2/22/' $testroot/repo/numbers
	git_commit $testroot/repo -m "modified line 2"

	# modify line 7; both changes should merge cleanly
	sed -i 's/7/77/' $testroot/wt/numbers

	echo "G  numbers" > $testroot/stdout.expected
	echo -n "Updated to commit " >> $testroot/stdout.expected
	git_show_head $testroot/repo >> $testroot/stdout.expected
	echo >> $testroot/stdout.expected

	(cd $testroot/wt && got update > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo 'M  numbers' > $testroot/stdout.expected

	(cd $testroot/wt && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_status_unversioned_subdirs {
	local testroot=`test_init status_unversioned_subdirs 1`

	mkdir $testroot/repo/cdfs/
	touch $testroot/repo/cdfs/Makefile
	mkdir $testroot/repo/common/
	touch $testroot/repo/common/Makefile
	mkdir $testroot/repo/iso/
	touch $testroot/repo/iso/Makefile
	mkdir $testroot/repo/ramdisk/
	touch $testroot/repo/ramdisk/Makefile
	touch $testroot/repo/ramdisk/list.local
	mkdir $testroot/repo/ramdisk_cd/
	touch $testroot/repo/ramdisk_cd/Makefile
	touch $testroot/repo/ramdisk_cd/list.local
	(cd $testroot/repo && git add .)
	git_commit $testroot/repo -m "first commit"

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	mkdir $testroot/wt/cdfs/obj
	mkdir $testroot/wt/ramdisk/obj
	mkdir $testroot/wt/ramdisk_cd/obj
	mkdir $testroot/wt/iso/obj

	echo -n > $testroot/stdout.expected

	# This used to erroneously print:
	#
	# !  ramdisk_cd/Makefile
	# !  ramdisk_cd/list.local
	# ?  ramdisk_cd/Makefile
	# ?  ramdisk_cd/list.local
	(cd $testroot/wt && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

# 'got status' ignores symlinks at present; this might change eventually
function test_status_ignores_symlink {
	local testroot=`test_init status_ignores_symlink 1`

	mkdir $testroot/repo/ramdisk/
	touch $testroot/repo/ramdisk/Makefile
	(cd $testroot/repo && git add .)
	git_commit $testroot/repo -m "first commit"

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	ln -s /usr/obj/distrib/i386/ramdisk $testroot/wt/ramdisk/obj

	echo -n > $testroot/stdout.expected

	(cd $testroot/wt && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_status_shows_no_mods_after_complete_merge {
	local testroot=`test_init status_shows_no_mods_after_complete_merge 1`

	# make this file larger than the usual blob buffer size of 8192
	echo -n > $testroot/repo/numbers
	for i in `jot 16384`; do
		echo "$i" >> $testroot/repo/numbers
	done

	(cd $testroot/repo && git add numbers)
	git_commit $testroot/repo -m "added numbers file"

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	sed -i 's/2/22/' $testroot/repo/numbers
	git_commit $testroot/repo -m "modified line 2"

	sleep 1
	# modify line 2 again; no local changes are left after merge
	sed -i 's/2/22/' $testroot/wt/numbers

	echo "G  numbers" > $testroot/stdout.expected
	echo -n "Updated to commit " >> $testroot/stdout.expected
	git_show_head $testroot/repo >> $testroot/stdout.expected
	echo >> $testroot/stdout.expected

	(cd $testroot/wt && got update > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo -n > $testroot/stdout.expected

	(cd $testroot/wt && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_status_shows_conflict {
	local testroot=`test_init status_shows_conflict 1`

	echo "1" > $testroot/repo/numbers
	echo "2" >> $testroot/repo/numbers
	echo "3" >> $testroot/repo/numbers
	echo "4" >> $testroot/repo/numbers
	echo "5" >> $testroot/repo/numbers
	echo "6" >> $testroot/repo/numbers
	echo "7" >> $testroot/repo/numbers
	echo "8" >> $testroot/repo/numbers
	(cd $testroot/repo && git add numbers)
	git_commit $testroot/repo -m "added numbers file"

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	sed -i 's/2/22/' $testroot/repo/numbers
	git_commit $testroot/repo -m "modified line 2"

	# modify line 2 in a conflicting way
	sed -i 's/2/77/' $testroot/wt/numbers

	echo "C  numbers" > $testroot/stdout.expected
	echo -n "Updated to commit " >> $testroot/stdout.expected
	git_show_head $testroot/repo >> $testroot/stdout.expected
	echo >> $testroot/stdout.expected

	(cd $testroot/wt && got update > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo 'C  numbers' > $testroot/stdout.expected

	(cd $testroot/wt && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_status_empty_dir {
	local testroot=`test_init status_empty_dir`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	rm $testroot/wt/epsilon/zeta

	echo '!  epsilon/zeta' > $testroot/stdout.expected

	(cd $testroot/wt && got status epsilon > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_status_empty_dir_unversioned_file {
	local testroot=`test_init status_empty_dir_unversioned_file`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	rm $testroot/wt/epsilon/zeta
	touch $testroot/wt/epsilon/unversioned

	echo '?  epsilon/unversioned' > $testroot/stdout.expected
	echo '!  epsilon/zeta' >> $testroot/stdout.expected

	(cd $testroot/wt && got status epsilon > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_status_many_paths {
	local testroot=`test_init status_many_paths`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "modified alpha" > $testroot/wt/alpha
	(cd $testroot/wt && got rm beta >/dev/null)
	echo "unversioned file" > $testroot/wt/foo
	rm $testroot/wt/epsilon/zeta
	touch $testroot/wt/beta
	echo "new file" > $testroot/wt/new
	mkdir $testroot/wt/newdir
	(cd $testroot/wt && got add new >/dev/null)

	(cd $testroot/wt && got status newdir > $testroot/stdout.expected)
	(cd $testroot/wt && got status alpha >> $testroot/stdout.expected)
	(cd $testroot/wt && got status epsilon >> $testroot/stdout.expected)
	(cd $testroot/wt && got status foo >> $testroot/stdout.expected)
	(cd $testroot/wt && got status new >> $testroot/stdout.expected)
	(cd $testroot/wt && got status beta >> $testroot/stdout.expected)
	(cd $testroot/wt && got status . >> $testroot/stdout.expected)

	(cd $testroot/wt && got status newdir alpha epsilon foo new beta . \
		> $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_status_cvsignore {
	local testroot=`test_init status_cvsignore`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "unversioned file" > $testroot/wt/foo
	echo "unversioned file" > $testroot/wt/foop
	echo "unversioned file" > $testroot/wt/epsilon/bar
	echo "unversioned file" > $testroot/wt/epsilon/boo
	echo "unversioned file" > $testroot/wt/epsilon/moo
	echo "foo" > $testroot/wt/.cvsignore
	echo "bar" > $testroot/wt/epsilon/.cvsignore
	echo "moo" >> $testroot/wt/epsilon/.cvsignore

	echo '?  .cvsignore' > $testroot/stdout.expected
	echo '?  epsilon/.cvsignore' >> $testroot/stdout.expected
	echo '?  epsilon/boo' >> $testroot/stdout.expected
	echo '?  foop' >> $testroot/stdout.expected
	(cd $testroot/wt && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo '?  .cvsignore' > $testroot/stdout.expected
	echo '?  epsilon/.cvsignore' >> $testroot/stdout.expected
	echo '?  epsilon/boo' >> $testroot/stdout.expected
	echo '?  foop' >> $testroot/stdout.expected
	(cd $testroot/wt/gamma && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

function test_status_gitignore {
	local testroot=`test_init status_gitignore`

	got checkout $testroot/repo $testroot/wt > /dev/null
	ret="$?"
	if [ "$ret" != "0" ]; then
		test_done "$testroot" "$ret"
		return 1
	fi

	echo "unversioned file" > $testroot/wt/foo
	echo "unversioned file" > $testroot/wt/foop
	echo "unversioned file" > $testroot/wt/barp
	echo "unversioned file" > $testroot/wt/epsilon/bar
	echo "unversioned file" > $testroot/wt/epsilon/boo
	echo "unversioned file" > $testroot/wt/epsilon/moo
	mkdir -p $testroot/wt/a/b/c/
	echo "unversioned file" > $testroot/wt/a/b/c/foo
	echo "unversioned file" > $testroot/wt/a/b/c/zoo
	echo "foo" > $testroot/wt/.gitignore
	echo "bar*" >> $testroot/wt/.gitignore
	echo "epsilon/**" >> $testroot/wt/.gitignore
	echo "a/**/foo" >> $testroot/wt/.gitignore
	echo "**/zoo" >> $testroot/wt/.gitignore

	echo '?  .gitignore' > $testroot/stdout.expected
	echo '?  foop' >> $testroot/stdout.expected
	(cd $testroot/wt && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
		test_done "$testroot" "$ret"
		return 1
	fi

	echo '?  .gitignore' > $testroot/stdout.expected
	echo '?  foop' >> $testroot/stdout.expected
	(cd $testroot/wt/gamma && got status > $testroot/stdout)

	cmp -s $testroot/stdout.expected $testroot/stdout
	ret="$?"
	if [ "$ret" != "0" ]; then
		diff -u $testroot/stdout.expected $testroot/stdout
	fi
	test_done "$testroot" "$ret"
}

run_test test_status_basic
run_test test_status_subdir_no_mods
run_test test_status_subdir_no_mods2
run_test test_status_obstructed
run_test test_status_shows_local_mods_after_update
run_test test_status_unversioned_subdirs
run_test test_status_ignores_symlink
run_test test_status_shows_no_mods_after_complete_merge
run_test test_status_shows_conflict
run_test test_status_empty_dir
run_test test_status_empty_dir_unversioned_file
run_test test_status_many_paths
run_test test_status_cvsignore
run_test test_status_gitignore
