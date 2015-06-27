/* Check available memory and swap in a loop and start killing
 * processes if they get too low */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "meminfo.h"
#include "kill.h"

int main(int argc, char *argv[])
{
	int kernel_oom_killer = 0;
	unsigned long oom_cnt = 0;
	/* If the available memory goes below this percentage, we start killing
	 * processes. 10 is a good start. */
	int mem_min_percent = 10, swap_min_percent = 10;
	unsigned long mem_min, swap_min; /* Same thing in kiB */

	/* request line buffering for stdout - otherwise the output
	 * may lag behind stderr */
	setlinebuf(stdout);

	fprintf(stderr, "earlyoom %s\n", GITVERSION);

	if(chdir("/proc")!=0)
	{
		perror("Could not cd to /proc");
		exit(4);
	}

	DIR *procdir = opendir(".");
	if(procdir==NULL)
	{
		perror("Could not open /proc");
		exit(5);
	}

	int c;
	while((c = getopt (argc, argv, "m:s:kh")) != -1)
	{
		switch(c)
		{
			case 'm':
				mem_min_percent = strtol(optarg, NULL, 10);
				if(mem_min_percent <= 0) {
					fprintf(stderr, "-m: Invalid percentage\n");
					exit(15);
				}
				break;
			case 's':
				swap_min_percent = strtol(optarg, NULL, 10);
				if(swap_min_percent <= 0 || swap_min_percent > 100) {
					fprintf(stderr, "-s: Invalid percentage\n");
					exit(16);
				}
				break;
			case 'k':
				kernel_oom_killer = 1;
				fprintf(stderr, "Using kernel oom killer\n");
				break;
			case 'h':
				fprintf(stderr,
					"Usage: earlyoom [-m PERCENT] [-s PERCENT] [-k] [-h]\n"
					"-m ... set available memory minimum to PERCENT of total (default 10 %)\n"
					"-s ... set free swap minimum to PERCENT of total (default 10 %)\n"
					"-k ... use kernel oom killer instead of own user-space implmentation\n"
					"-h ... this help text\n");
				exit(1);
			case '?':
				exit(13);
		}
	}

	struct meminfo m = parse_meminfo();
	mem_min = m.MemTotal * mem_min_percent / 100;
	swap_min = m.SwapTotal * swap_min_percent / 100;

	fprintf(stderr, "mem total: %lu MiB, min: %lu MiB (%d %%)\n",
		m.MemTotal / 1024, mem_min / 1024, mem_min_percent);
	fprintf(stderr, "swap total: %lu MiB, min: %lu MiB (%d %%)\n",
		m.SwapTotal / 1024, swap_min / 1024, swap_min_percent);

	/* Dry-run oom kill to make sure stack grows to maximum size before
	 * calling mlockall()
	 */
	handle_oom(procdir, 0, kernel_oom_killer);

	if(mlockall(MCL_FUTURE)!=0)
	{
		perror("Could not lock memory");
		exit(10);
	}

	c = 1; // Start at 1 so we do not print another status line immediately
	while(1)
	{
		m = parse_meminfo();

		if(c % 10 == 0)
		{
			printf("mem avail: %5lu MiB, swap free: %5lu MiB\n",
				m.MemAvailable / 1024, m.SwapFree / 1024);
			c=0;
		}
		c++;

		if(m.MemAvailable <= mem_min && m.SwapFree <= swap_min)
		{
			fprintf(stderr, "Out of memory! avail: %lu MiB < min: %lu MiB\n",
				m.MemAvailable / 1024, mem_min / 1024);
			handle_oom(procdir, 9, kernel_oom_killer);
			oom_cnt++;
		}
		
		usleep(100000); // 100ms
	}
	
	return 0;
}
