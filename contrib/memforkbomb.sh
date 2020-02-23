#!/bin/bash
#
# Combined memory- and fork-bomb. You may
# want to save all your open files before
# running this.
#
# Forks a new memory hog every two seconds,
# setting oom_score_adj to 1000 to avoid
# causing damage to other processes.
#
# Each memory hog eats up memory as fast as
# it can (I just use `tail /dev/zero`).
#
# On my machine, earlyoom keeps up killing
# the hogs fast enough to not cause any
# slowdown.

set -ux

while sleep 2 ; do
	tail /dev/zero &
	PID=$!
	echo 1000 > /proc/$PID/oom_score_adj || exit 1
done
