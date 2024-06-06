#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/*
HOW TO USE THIS:
truncate -s 10G test.bin && ./mmap_test
*/

int main()
{
    int fd;
    char* addr;
    struct stat sb;

    fd = open("test.bin", O_RDWR);
    if (fd == -1) {
        perror("open test.bin");
        exit(1);
    }

    if (fstat(fd, &sb) == -1) {
        perror("fstat");
        exit(2);
    }

    addr = mmap(NULL, sb.st_size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        exit(3);
    }

    printf("pid %d, mmap ok (%ld MiB)\n", getpid(), sb.st_size/1024/1024);

    for(long i = 0; i < sb.st_size; i += 1024) {
        volatile char x = addr[i];
        x++;
    }

    printf("sleeping 1h\n");
    sleep(3600);

    munmap(addr, sb.st_size);
    close(fd);

    exit(0);
}
