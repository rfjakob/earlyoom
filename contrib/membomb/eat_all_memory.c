#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

#include "eat_all_memory.h"

static void handle_sigterm(int sig)
{
    printf("blocking SIGTERM %d\n", sig);
}

void eat_all_memory(eat_how_enum eat_how)
{
    long page_size = sysconf(_SC_PAGESIZE);
    long num_pages = 10;
    if (eat_how == EAT_MMAP_FILE) {
        num_pages = 10000;
    }
    long bs = page_size * num_pages;
    long cnt = 0, last_sum = 0;
    struct timeval tv1;
    signal(SIGTERM, handle_sigterm);
    gettimeofday(&tv1, NULL);
    while (1) {
        char* p = NULL;
        switch (eat_how) {
        case EAT_MALLOC:
            p = malloc(bs);
            if (!p) {
                perror("malloc failed");
                continue;
            }
            break;
        case EAT_MMAP_ANON:
            p = mmap(NULL, bs, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED) {
                perror("mmap failed");
                continue;
            }
            break;
        case EAT_MMAP_FILE:
            char tmp_path[] = "/var/tmp/membomb.mmap_file.XXXXXX";
            int fd = mkstemp(tmp_path);
            if (fd == -1) {
                perror("mkstemp failed");
                sleep(1);
                continue;
            }
            int ret = unlink(tmp_path);
            if (ret) {
                perror("unlink failed");
            }
            if (ret) {
                errno = ret;
                perror("ftruncate failed");
                sleep(1);
                continue;
            }
            p = mmap(NULL, bs, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (p == MAP_FAILED) {
                perror("mmap failed");
                sleep(1);
                continue;
            }
            break;
        default:
            fprintf(stderr, "BUG: unknown eat_how=%d\n", eat_how);
            exit(1);
        }
        for (int i = 0; i < num_pages; i++) {
            // Write to each page so the kernel really has to allocate it.
            p[i * page_size] = 0xab;
        }
        cnt++;
        const int cnt_per_100mb = 100 * 1024 * 1024 / bs;
        if (cnt % cnt_per_100mb == 0) {
            long sum = bs * cnt / 1024 / 1024;
            struct timeval tv2;
            gettimeofday(&tv2, NULL);
            long delta = tv2.tv_sec - tv1.tv_sec;
            // Convert to microseconds
            delta *= 1000000;
            // Add microsecond delta
            delta = delta + tv2.tv_usec - tv1.tv_usec;
            // Micro-MB-per-Microsecond = MB/s
            long mbps = (sum - last_sum) * 1000000 / delta;
            printf("%4ld MiB (%4ld MiB/s)\n", sum, mbps);
            last_sum = sum;
            gettimeofday(&tv1, NULL);
        }
    }
}
