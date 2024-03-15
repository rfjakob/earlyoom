// SPDX-License-Identifier: MIT

/* In a subthread, eat up all memory.
 * The main thread exits and will show up as a zombie.
 *
 * This file is part of the earlyoom project: https://github.com/rfjakob/earlyoom
 */

#define _GNU_SOURCE

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "eat_all_memory.h"

void* eat_all_memory_thread(__attribute__((__unused__)) void* arg)
{
    printf("sub  thread = pid %d\n", gettid());
    eat_all_memory();
    return NULL;
}

int main()
{
    printf("main thread = pid %d\n", gettid());
    pthread_t thread;
    pthread_create(&thread, NULL, &eat_all_memory_thread, NULL);
    pthread_exit(NULL);
}
