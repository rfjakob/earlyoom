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
earlyoom monitors the amount of available memory. "Available memory" is what
free(1) calls "free -/+ buffers/cache", corrected for tmpfs memory usage
[(more info)][1]. In the "free" output below, the available memory is 5918376 kiB.
That number is checked 10 times a second.

		         total       used       free     shared    buffers     cached
	Mem:       7894520    5887116    2007404    1519648     245436    3665536
	-/+ buffers/cache:    1976144    5918376
	Swap:            0          0          0

When that number drops below 10% of your total physical RAM, it will kill -9 the
process that has the most resident memory ("VmRSS" in `/proc/*/status`).

Note that swap is not taken into account at all. That means that processes
may get killed before swap is used. This, however, is the point of
earlyoom: To keep the system responsive at all costs. earlyoom is designed
for systems with lots of RAM (6 GB or more) and with swap disabled.

Why not trigger the kernel oom killer?
--------------------------------------
Earlyoom does not use "echo f > /proc/sysrq-trigger" because the Chrome people made
their browser always be the first (innocent!) victim by setting oom_score_adj
very high ( https://code.google.com/p/chromium/issues/detail?id=333617 ).
Instead, earlyoom finds out itself by reading trough `/proc/*/status`
(actually `/proc/*/statm`, which contains the same information but is easier to
parse programmatically).

How much memory does earlyoom use?
----------------------------------
About 0.6MB RSS. All memory is locked using mlockall() to make sure
earlyoom does not slow down in low memory situations.

Download and compile
--------------------
Easy:

	git clone https://github.com/rfjakob/earlyoom.git
	cd earlyoom
	make
	sudo make install # Optional, if you want earlyoom to start
	                  # automatically as a service (works on Fedora)

Use
---
Just start the executable you have just compiled:

	./earlyoom

It will inform you how much physical RAM you have, how much is currently
available and what the minimum amount of available memory is:

	earlyoom v0.4
	total:  7842 MiB
	min:     784 MiB
	avail:  5071 MiB
	avail:  5071 MiB
	avail:  5071 MiB
	[...]

If the available memory drops below the minimum, processes are killed until it
is above the minimum again. Every action is logged to stderr. If you are on
Fedora and running earlyoom as a service, you can view the last 10 lines
using

	systemctl status earlyoom

Contribute
----------
Bug reports and pull requests are welcome via github. In particular, I am glad to
accept

* <del>An init script that works on Debian/Ubuntu</del> Thanks joeytwiddle!
* Use case reports and feedback
* <del>An update to use the new `MemAvailable` from `/proc/meminfo`
  when available (Linux 3.14+, [commit][4])</del> Added in commit
  [b6395872a049be78d636a9515f1c18c6997ea8a8][5].

[1]: http://www.freelists.org/post/procps/library-properly-handle-memory-used-by-tmpfs
[2]: http://superuser.com/questions/406101/is-it-possible-to-make-the-oom-killer-intervent-earlier
[3]: http://unix.stackexchange.com/questions/38507/is-it-possible-to-trigger-oom-killer-on-forced-swapping
[4]: https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=34e431b0ae398fc54ea69ff85ec700722c9da773
[5]: https://github.com/rfjakob/earlyoom/commit/b6395872a049be78d636a9515f1c18c6997ea8a8
