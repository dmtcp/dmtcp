#!/bin/bash

# Copy this script to the DMTCP root director.
# Then modify this script first for your purposes.
# In particular, look for the two grep strings (test if good, test if bad), and
#   modify them for your test.
# Then execute something like:
# git bisect start
# git bisect bad
# git bisect good 2.0  # tag 2.0, or pick any older commit that was good
# git bisect run ./git-bisect.sh

# While this is executing, in another window, you can see the progress with:
# git bisect log
# To modify and redo:
# git bisect log > git-bisect.log
# tail -f git-bisect.out

# The bad commit could be a false alarm witin a range of good commit.
# To analyze a given range, try something like:
# git log --oneline 1a7d8dbb0dc48..master | wc
# git checkout master~<NUM>    # where <NUM> is the number of lines, from wc
#   and try testing a few more recent branches
# Then EDIT git-bisect.log
# git bisect replay git-bisect.log
# git bisect ./git-bisect.sh


# Test if good
git log -1 > git-bisect.out
echo
echo "************ NEW RUN **************"
git log -1 | cat
good_match_1='ckpt:[\t ]*PASSED[\t ]*rstr:[\t ]*PASSED[\t ]*;'
good_match_2='[\t ]*ckpt:[\t ]*PASSED[\t ]*rstr:[\t ]*PASSED'
good_match="$good_match_1$good_match_2"
# older DMTCP had Makefile that tried to call automake
# older DMTCP required '.' in PATH for 'cd mtcp && make build'
# older DMTCP had dmtcp/src directory
# older DMTCP needed mtcp_restart instead of mtcp_restart-32
./configure --enable-m32 AUTOMAKE=echo && make -j clean && rm -rf bin lib && \
    (PATH=$PATH:. make -j) && (cd src && make -j || true) && \
    (cd dmtcp/src && make -j || true) && \
    (mkdir -p bin && cd bin && ln -sf ../lib/dmtcp/32/bin/* ./ || true) && \
    (cd lib/dmtcp && ln -sf 32/lib/dmtcp/* ./ || true) && \
    (cd lib/dmtcp/32/bin && ln -s mtcp_restart-32 mtcp_restart || true) && \
  make -j check-dmtcp1 >> git-bisect.out 2>&1 && \
  grep "$good_match" git-bisect.out \
     > /dev/null && \
  exit 0

# Test if bad
# We consider a run "bad" only if it compiled and started autotext.py.
#   Anything else should be skipped as a problem for that single revision.
grep 'ckpt:' git-bisect.out > /dev/null && exit 1

# Skip it (unsure)
exit 125
