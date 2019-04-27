#!/bin/bash
#
# Monitor the memory usage and state of a PID in
# a 0.1 second loop.
#
# Example with "sudo memtester 10G" running in the background:
#
#	$ ./mon.sh $(pgrep memtester)
#	0 MemAvailable:   10158052 kB VmRSS:	10487200 kB statm: 2622018 2621800 346 4 0 2621518 0 stat: 9067 (memtester) R 9065 9065 4
#	0 MemAvailable:   10153776 kB VmRSS:	10487200 kB statm: 2622018 2621800 346 4 0 2621518 0 stat: 9067 (memtester) R 9065 9065 4
#	0 MemAvailable:   10153532 kB VmRSS:	10487200 kB statm: 2622018 2621800 346 4 0 2621518 0 stat: 9067 (memtester) R 9065 9065 4
# ***sudo pkill memtester***
#	0 MemAvailable:   10154260 kB  statm: 0 0 0 0 0 0 0 stat: 9067 (memtester) R 9065 9065 4
#	0 MemAvailable:   10154296 kB  statm: 0 0 0 0 0 0 0 stat: 9067 (memtester) R 9065 9065 4
#	0 MemAvailable:   10154280 kB  statm: 0 0 0 0 0 0 0 stat: 9067 (memtester) R 9065 9065 4
#	0 MemAvailable:   10154256 kB  statm: 0 0 0 0 0 0 0 stat: 9067 (memtester) R 9065 9065 4
#	0 MemAvailable:   10146932 kB  statm: 0 0 0 0 0 0 0 stat: 9067 (memtester) R 9065 9065 4
#	0 MemAvailable:   11038764 kB  statm: 0 0 0 0 0 0 0 stat: 9067 (memtester) R 9065 9065 4
#	0 MemAvailable:   13773280 kB  statm: 0 0 0 0 0 0 0 stat: 9067 (memtester) R 9065 9065 4
#	0 MemAvailable:   16593848 kB  statm: 0 0 0 0 0 0 0 stat: 9067 (memtester) R 9065 9065 4
#	0 MemAvailable:   19330180 kB  statm: 0 0 0 0 0 0 0 stat: 9067 (memtester) R 9065 9065 4
# ***actual exit***
#	1 MemAvailable:   20632628 kB  statm:  stat: 
#	1 MemAvailable:   20632992 kB  statm:  stat: 
#	1 MemAvailable:   20633244 kB  statm:  stat: 

set -u
while sleep 0.1; do
	test -e /proc/$1
	PROC=$?
	AVAIL=$(grep MemAvailable /proc/meminfo)
	RSS=$(grep VmRSS /proc/$1/status 2> /dev/null)
	STATM=$(cat /proc/$1/statm 2> /dev/null)
	STAT=$(head -c 30 /proc/$1/stat 2> /dev/null)
	echo "$PROC $AVAIL $RSS statm: $STATM stat: $STAT"
done
