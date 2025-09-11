#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

int main()
{
    const long gigabyte = 1024 * 1024 * 1024;
    const long size = 10 * gigabyte;
    char tmp_path[] = "/var/tmp/keep_active.XXXXXX";
    int fd = mkstemp(tmp_path);
    if (fd == -1) {
        perror("mkstemp failed");
        exit(1);
    }
    int ret = unlink(tmp_path);
    if (ret) {
        perror("unlink failed");
        exit(1);
    }
    ret = posix_fallocate(fd, 0, size);
    if (ret) {
        errno = ret;
        perror("posix_fallocate failed");
        exit(1);
    }
    fsync(fd);

    printf("Allocated %ld GiB\n", size / gigabyte);
    printf("Spinning on file reads...\n");
    struct timeval tv1;
    gettimeofday(&tv1, NULL);
    while (1) {
        long increment = sysconf(_SC_PAGESIZE);
        char buf[1];
        for (long off = 0; off < size; off += increment) {
            ret = pread(fd, buf, sizeof(buf), off);
            if (ret <= 0) {
                perror("pread");
                exit(1);
            }
        }
        // Print stats
        struct timeval tv2;
        gettimeofday(&tv2, NULL);
        long delta = tv2.tv_sec - tv1.tv_sec;
        // Convert to microseconds
        delta *= 1000000;
        // Add microsecond delta
        delta = delta + tv2.tv_usec - tv1.tv_usec;
        // Byte-per-Microsecond = MB/s
        long mbps = size / delta;
        printf("%4ld MiB (%4ld MiB/s)\n", size / 1024 / 1024, mbps);
        gettimeofday(&tv1, NULL);
    }
}
