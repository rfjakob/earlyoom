/* If the available memory goes below this percentage, we start killing
 * processes. 10 is a good start. */
#define MIN_AVAIL_PERCENT 10

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fts.h>
#include <err.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "sysinfo.h"

/* "free -/+ buffers/cache"
 * Memory that is actually available to applications */
static unsigned long get_kb_avail(void)
{
	meminfo();
	return kb_main_free + kb_main_buffers + kb_main_cached;
}

static int isnumeric(char* str)
{
	int i=0;

	// Empty string is not numeric
	if(str[0]==0)
		return 0;

	while(1)
	{
		if(str[i]==0) // End of string
			return 1;

		if(isdigit(str[i])==0)
			return 0;

		i++;
	}
}

#ifndef USE_KERNEL_OOM_KILLER
/*
 * Find the process with the largest RSS and kill it.
 * See trigger_oom_killer() for the reason why this is done in userspace.
 */
static void kill_by_rss(DIR *procdir, int sig)
{
	struct dirent * d;
	char buf[PATH_MAX];
	int pid;
	int hog_pid=0;
	unsigned long hog_rss=0;
	char name[PATH_MAX];

	rewinddir(procdir);
	while(1)
	{
		d=readdir(procdir);
		if(d==NULL)
			break;

		if(!isnumeric(d->d_name))
			continue;

		pid=strtoul(d->d_name, NULL, 10);

		snprintf(buf, PATH_MAX, "%d/statm", pid);

		FILE * statm = fopen(buf, "r");
		if(statm == 0)
		{
			// Process may have died in the meantime
			//fprintf(stderr, "Info: Could not open /proc/%s: %s\n", buf, strerror(errno));
			continue;
		}

		long VmSize=0, VmRSS=0;
		if(fscanf(statm, "%lu %lu", &VmSize, &VmRSS) < 2)
		{
			fprintf(stderr, "Error: Could not parse %s\n", buf);
			exit(8);
		}
		fclose(statm);

		if(VmRSS > hog_rss)
		{
			hog_pid=pid;
			hog_rss=VmRSS;
		}
	}

	if(hog_pid==0)
	{
		fprintf(stderr, "Error: Could not find a process to kill\n");
		exit(9);
	}

	name[0]=0;
	snprintf(buf, PATH_MAX, "%d/stat", hog_pid);
	FILE * stat = fopen(buf, "r");
	fscanf(stat, "%d %s", &pid, name);
	fclose(stat);

	if(sig!=0)
		fprintf(stderr, "Killing process %d %s\n", hog_pid, name);

	if(kill(hog_pid, sig) != 0)
	{
		fprintf(stderr, "Warning: Could not kill process: %s\n", strerror(errno));
	}
}
#else
/*
 * Invoke the kernel oom killer by writing "f" into /proc/sysrq-trigger
 *
 * This approach has a few problems:
 * 1) It is disallowed by default (even for root) on Fedora 20.
 *    You have to first write "1" into /proc/sys/kernel/sysrq to enable the "f"
 *    trigger.
 * 2) The Chrome web browser assigns a penalty of 300 onto its own tab renderer
 *    processes. On an 8GB RAM machine, this means 2400MB, and will lead to every
 *    tab being killed before the actual memory hog
 *    See https://code.google.com/p/chromium/issues/detail?id=333617 for more info
 * Because of these issues, kill_by_rss() is used instead by default.
 */
void trigger_oom_killer(int sig)
{
	int trig_fd;
	trig_fd = open("sysrq-trigger", O_WRONLY);
	if(trig_fd == -1)
	{
		fprintf(stderr, "Warning: Cannot open /proc/sysrq-trigger: %s. ");
		return;
	}
	if(sig!=9)
		return;
	fprintf(stderr, "Invoking oom killer: ");
	if(write(trig_fd, "f", 1) == -1)
		fprintf("%s\n", strerror(errno));
	else
		fprintf(stderr, "done\n");
}
#endif


void handle_oom(DIR * procdir, int sig)
{
#ifndef USE_KERNEL_OOM_KILLER
	kill_by_rss(procdir, sig);
#else
	trigger_oom_killer(sig);
#endif
}

int main(int argc, char *argv[])
{
	unsigned long kb_avail, kb_min, oom_cnt=0;

	/* To be able to observe in real time what is happening when the
	 * output is redirected we have to explicitely request line
	 * buffering */
	setvbuf(stdout , NULL , _IOLBF , 80);

	fprintf(stderr, "earlyoom %s\n", GITVERSION);

	if(chdir("/proc")!=0)
	{
		fprintf(stderr, "Error: Could not cd to /proc: %s\n", strerror(errno));
		exit(4);
	}

	DIR *procdir = opendir(".");
	if(procdir==NULL)
	{
		fprintf(stderr, "Error: Could not open /proc: %s\n", strerror(errno));
		exit(5);
	}

	kb_avail = get_kb_avail();
	kb_min = kb_main_total/100*MIN_AVAIL_PERCENT;

	fprintf(stderr, "total: %5lu MiB\n", kb_main_total/1024);
	fprintf(stderr, "min:   %5lu MiB\n", kb_min/1024);
	fprintf(stderr, "avail: %5lu MiB\n", kb_avail/1024);

	/* Dry-run oom kill to make sure stack grows to maximum size before
	 * calling mlockall()
	 */
	handle_oom(procdir, 0);

	if(mlockall(MCL_FUTURE)!=0)
	{
		fprintf(stderr, "Error: Could not lock memory: %s\n", strerror(errno));
		exit(10);
	}

	unsigned char c=1; // So we do not print another status line immediately
	while(1)
	{
		kb_avail = get_kb_avail();

		if(c % 10 == 0)
		{
			printf("avail: %5lu MiB\n", kb_avail/1024);
			/*printf("kb_main_free: %lu kb_main_buffers: %lu kb_main_cached: %lu kb_main_shared: %lu\n",
				kb_main_free, kb_main_buffers, kb_main_cached, kb_main_shared);
			*/
			c=0;
		}
		c++;

		if(kb_avail < kb_min)
		{
			fprintf(stderr, "Out of memory! avail: %lu MiB < min: %lu MiB\n",
				kb_avail/1024, kb_min/1024);
			handle_oom(procdir, 9);
			oom_cnt++;
		}
		
		usleep(100000); // 100ms
	}
	
	return 0;
}
