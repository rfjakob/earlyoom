#!/bin/bash
# Testcase for https://github.com/rfjakob/earlyoom/issues/110
#
# earlyoom output should look like this:
#
#   sending SIGTERM to process 28570 "tail_😀😀": oom_score 629, VmRSS 15048 MiB
#
# and not like this:
#
#   sending SIGTERM to process 28491 "tail_😀😀�": oom_score 630, VmRSS 15076 MiB

set -eu
cd $(mktemp -d)
TAIL=$(which tail)
ln -s $TAIL tail_😀😀😀😀😀😀😀😀
exec ./tail_😀😀😀😀😀😀😀😀 /dev/zero
