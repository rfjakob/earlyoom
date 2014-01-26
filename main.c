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
 * Find the process with the largest RSS and kill -9 it.
 * See trigger_oom_killer() for the reason why this is done in userspace.
 */
static void kill_by_rss()
{
	struct dirent * d;
	DIR *proc = opendir(".");
	char buf[PATH_MAX];
	int pid;

	int hog_pid=0;
	unsigned long hog_rss=0;

	if(proc==NULL)
	{
		printf("Error: Could not open /proc: %s\n", strerror(errno));
		exit(5);
	}

	while(1)
	{
		d=readdir(proc);
		if(d==NULL)
			break;

		if(strcmp(".", d->d_name) == 0 || strcmp("..", d->d_name) == 0)
			continue;

		if(!isnumeric(d->d_name))
			continue;

		pid=strtoul(d->d_name, NULL, 10);

		snprintf(buf, PATH_MAX, "%d/statm", pid);

		FILE * statm = fopen(buf, "r");
		if(statm == 0)
		{
			printf("Error: Could not open %s: %s\n", buf, strerror(errno));
			exit(7);
		}

		long VmSize=0, VmRSS=0;
		if(fscanf(statm, "%lu %lu", &VmSize, &VmRSS) < 2)
		{
			printf("Error: Could not parse %s\n", buf);
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
		printf("Error: Could not find a process to kill\n");
		exit(9);
	}

	char name[PATH_MAX];
	name[0]=0;
	snprintf(buf, PATH_MAX, "%d/stat", hog_pid);
	FILE * stat = fopen(buf, "r");
	fscanf(stat, "%d %s", &pid, name);
	fclose(stat);

	printf("Killing process %d %s\n", hog_pid, name);

	if(kill(hog_pid, 0)!=0)
	{
		printf("Warning: Could not kill process: %s\n", strerror(errno));
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
void trigger_oom_killer(void)
{
	int trig_fd;
	trig_fd = open("sysrq-trigger", O_WRONLY);
	if(trig_fd == -1)
	{
		printf("Warning: Cannot open /proc/sysrq-trigger: %s. ");
		return;
	}
	printf("Invoking oom killer: ");
	if(write(trig_fd, "f", 1) == -1)
		printf("%s\n", strerror(errno));
	else
		printf("done\n");
}
#endif


void handle_oom(void)
{
#ifndef USE_KERNEL_OOM_KILLER
	kill_by_rss();
#else
	trigger_oom_killer();
#endif
	sleep(2);
}

int main(int argc, char *argv[])
{
	unsigned long kb_avail, kb_min, oom_cnt=0;

	if(chdir("/proc")!=0)
	{
		printf("Error: Could not cd to /proc: %s\n", strerror(errno));
		exit(4);
	}

	kb_avail = get_kb_avail();
	kb_min = kb_main_total/100*MIN_AVAIL_PERCENT;
	
	printf("Physical RAM: %lu, currently available: %lu, minimum: %lu (kiB)\n",
		kb_main_total, kb_avail, kb_min);

	while(1)
	{
		kb_avail = get_kb_avail();

		//printf("kb_avail: %lu\n", kb_avail);

		if(kb_avail < kb_min)
		{
			printf("OOM: Currently available: %lu < minimum: %lu (kiB)\n", 
				kb_avail, kb_min);
			handle_oom();
			oom_cnt++;
		}
		
		usleep(100000);
	}
	
	return 0;
}
