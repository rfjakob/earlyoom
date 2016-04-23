The Early OOM Daemon
====================

[![Build Status](https://api.travis-ci.org/rfjakob/earlyoom.svg)](https://travis-ci.org/rfjakob/earlyoom)

The oom-killer generally has a bad reputation among Linux users. This may be
part of the reason Linux invokes it only when it has absolutely no other choice.
It will swap out the desktop environment, drop the whole page cache and empty
every buffer before it will ultimately kill a process. At least that's what I
think what it will do. I have yet to be patient enough to wait for it.

Instead of sitting in front of an unresponsive system, listening to the grinding
disk for minutes, I usually press the reset button and get back to what I was
doing quickly.

If you want to see what I mean, open something like
http://www.unrealengine.com/html5/
in a few Firefox windows. Save your work to disk beforehand, though.

The downside of the reset button is that it kills all processes, whereas it
would probably have been enough to kill a single one. This made people wonder
if the oom-killer could be configured to step in earlier: [superuser.com][2]
, [unix.stackexchange.com][3].

As it turns out, no, it can't. At least using the in-kernel oom killer.

In the user space however, we can do whatever we want.

What does it do
---------------
earlyoom checks the amount of available memory and (since version 0.5)
free swap 10 times a second. If both are below 10%, it will kill the
largest process. The percentage value is configurable via command line
arguments.

In the `free -m` output below, the available memory is 2170 MiB and
the free swap is 231 MiB.

                  total        used        free      shared  buff/cache   available
    Mem:           7842        4523         137         841        3182        2170
    Swap:          1023         792         231

Why is "available" memory checked as opposed to "free" memory?
On a healthy Linux system, "free" memory is supposed to be close to zero,
because Linux uses all available physical memory to cache disk access.
These caches can be dropped any time the memory is needed for something
else.

The "available" memory accounts for that. It sums up all memory that
is unused or can be freed immediately.

Note that you need a recent version of
`free` and Linux kernel 3.14+ to see the "available" column. If you have
a recent kernel, but an old version of `free`, you can get the value
from `cat /proc/meminfo | grep MemAvailable`.

When both your available memory and free swap drop below 10% of the total,
it will `kill -9` the process that uses the most memory in the opinion of
the kernel (`/proc/*/oom_score`). It can optionally (`-i` option) ignore
any positive adjustments set in `/proc/*/oom_score_adj` to protect innocent
victims (see below).

Why not trigger the kernel oom killer?
--------------------------------------
Earlyoom does not use `echo f > /proc/sysrq-trigger` because [the Chrome people made
their browser always be the first (innocent!) victim by setting `oom_score_adj`
very high]( https://code.google.com/p/chromium/issues/detail?id=333617).
Instead, earlyoom finds out itself by reading through `/proc/*/status`
(actually `/proc/*/statm`, which contains the same information but is easier to
parse programmatically).

Additionally, in recent kernels (tested on 4.0.5), triggering the kernel
oom killer manually may not work at all.
That is, it may only free some graphics
memory (that will be allocated immediately again) and not actually kill
any process. [Here](https://gist.github.com/rfjakob/346b7dc611fc3cdf4011) you
can see how this looks like on my machine (Intel integrated graphics).

How much memory does earlyoom use?
----------------------------------
About `0.6MB RSS`. All memory is locked using `mlockall()` to make sure
earlyoom does not slow down in low memory situations.

Download and compile
--------------------
Easy:

```bash
git clone https://github.com/rfjakob/earlyoom.git
cd earlyoom
make
sudo make install # Optional, if you want earlyoom to start
                  # automatically as a service (works on Fedora)
```

For Arch Linux, there's an [AUR package](https://aur.archlinux.org/packages/earlyoom/):
```bash
yaourt -S earlyoom
sudo systemctl enable earlyoom
sudo systemctl start earlyoom
```

Use
---
Just start the executable you have just compiled:

```bash
./earlyoom
```

It will inform you how much memory and swap you have, what the minimum
is, how much memory is available and how much swap is free.

```
earlyoom v0.10
mem total: 7842 MiB, min: 784 MiB (10 %)
swap total: 1023 MiB, min: 102 MiB (10 %)
mem avail: 5115 MiB (65 %), swap free: 1023 MiB (100 %)
mem avail: 5115 MiB (65 %), swap free: 1023 MiB (100 %)
mem avail: 5115 MiB (65 %), swap free: 1023 MiB (100 %)
[...]
```

If the values drop below the minimum, processes are killed until it
is above the minimum again. Every action is logged to stderr. If you are on
running earlyoom as a systemd service, you can view the last 10 lines
using

```bash
systemctl status earlyoom
```

Command line options
--------------------
```
$ ./earlyoom -h
earlyoom v0.10
Usage: earlyoom [-m PERCENT] [-s PERCENT] [-k|-i] [-h]
-m ... set available memory minimum to PERCENT of total (default 10 %)
-s ... set free swap minimum to PERCENT of total (default 10 %)
-k ... use kernel oom killer instead of own user-space implementation
-i ... user-space oom killer should ignore positive oom_score_adj values
-d ... enable debugging messages
-v ... print version information and exit
-h ... this help text
```

Contribute
----------
Bug reports and pull requests are welcome via github. In particular, I am glad to
accept

* Use case reports and feedback

Changelog
---------
* v0.10: Allow to override Makefile's VERSION variable to make packaging easier,
  add `-v` command-line option
* v0.9: If oom_score of all processes is 0, use VmRss to find a victim
* v0.8: Use a guesstimate if the kernel does not provide MemAvailable
* v0.7: Select victim by oom_score instead of VmRSS, add options `-i` and `-d`
* v0.6: Add command-line options `-m`, `-s`, `-k`
* v0.5: Add swap support
* v0.4: Add SysV init script (thanks [@joeytwiddle](https://github.com/joeytwiddle)), use the new `MemAvailable` from `/proc/meminfo`
  (needs Linux 3.14+, [commit][4])
* v0.2: Add systemd unit file
* v0.1: Initial release

[1]: http://www.freelists.org/post/procps/library-properly-handle-memory-used-by-tmpfs
[2]: http://superuser.com/questions/406101/is-it-possible-to-make-the-oom-killer-intervent-earlier
[3]: http://unix.stackexchange.com/questions/38507/is-it-possible-to-trigger-oom-killer-on-forced-swapping
[4]: https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=34e431b0ae398fc54ea69ff85ec700722c9da773
