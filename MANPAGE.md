% earlyoom(1) | General Commands Manual

# NAME

earlyoom - Early OOM Daemon

# SYNOPSIS

**earlyoom** [**OPTION**]...

# DESCRIPTION

The oom-killer generally has a bad reputation among Linux users. One may have
to sit in front of an unresponsive system, listening to the grinding disk for
minutes, and press the reset button to quickly get back to what one was doing
after running out of patience.

**earlyoom** checks the amount of available memory and free swap 10 times a
second. If both are below 10%, it will kill the largest process. The
percentage value is configurable via command line arguments.

If there is a failure when trying to kill a process, **earlyoom** sleeps for
1 second to limit log spam due to recurring errors.

# OPTIONS

#### -m PERCENT
set available memory minimum to PERCENT of total (default 10 %)

#### -s PERCENT
set free swap minimum to PERCENT of total (default 10 %)

#### -M SIZE
set available memory minimum to SIZE KiB

#### -S SIZE
set free swap minimum to SIZE KiB

#### -k
removed in earlyoom v1.2, ignored for compatibility

#### -i
user-space oom killer should ignore positive oom_score_adj values

#### -d
enable debugging messages

#### -v
print version information and exit

#### -r INTERVAL
memory report interval in seconds (default 1), set to 0 to disable completely.
With earlyoom v1.2 and higher, floating point numbers are accepted.

#### -p
set niceness of earlyoom to -20 and oom_score_adj to -1000

#### \-\-prefer REGEX
prefer killing processes matching REGEX (adds 300 to oom_score)

#### \-\-avoid REGEX
avoid killing processes matching REGEX (subtracts 300 from oom_score)

#### -h, \-\-help
this help text

# EXIT STATUS

0: Successful program execution.

1: Usage printed (using -h).

2: Switch conflict.

4: Could not cd to /proc

5: Could not open proc

7: Could not open /proc/sysrq-trigger

13: Unknown options.

14: Wrong parameters for other options.

15: Wrong parameters for memory threshold.

16: Wrong parameters for swap threshold.

102: Could not open /proc/meminfo

103: Could not read /proc/meminfo

104: Could not find a specific entry in /proc/meminfo

105: Could not convert number when parse the contents of /proc/meminfo

# Why not trigger the kernel oom killer?

Earlyoom does not use `echo f > /proc/sysrq-trigger` because the Chrome people
made their browser always be the first (innocent!)  victim by setting
`oom_score_adj` very high. Instead, earlyoom finds out itself by reading through
`/proc/*/status` (actually `/proc/*/statm`, which contains the same information
but is easier to parse programmatically).

Additionally, in recent kernels (tested on 4.0.5), triggering the kernel oom
killer manually may not work at all. That is, it may only free some graphics
memory (that will be allocated immediately again) and not actually kill any
process.

# MEMORY USAGE

About 2 MiB VmRSS. All memory is locked using mlockall() to make sure earlyoom
does not slow down in low memory situations.

# AUTHOR

The author of earlyoom is Jakob Unterwurzacher ⟨jakobunt@gmail.com⟩.

This manual page was written by Yangfl ⟨mmyangfl@gmail.com⟩, for the Debian
project (and may be used by others).
