#!/usr/bin/env python3
#
# fillmem.py allocates a configurable amount of memory
# and sleeps forever.
#
# Originally posted by swiftcoder at
# https://unix.stackexchange.com/a/99365/45823

import sys
import time
import os

if len(sys.argv) != 2:
    print("usage: ./fillmem.py MEGABYTES")
    sys.exit()

count = int(sys.argv[1])
data = bytearray(1024*1024*count)

while True:
    os.system("grep VmRSS /proc/%d/status" % (os.getpid()))
    time.sleep(1)
