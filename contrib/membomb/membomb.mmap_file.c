// SPDX-License-Identifier: MIT

/* Use up all memory that we can get, as fast as we can.
 * As progress information, prints how much memory we already
 * have.
 *
 * This file is part of the earlyoom project: https://github.com/rfjakob/earlyoom
 */

#include <stdio.h>

#include "eat_all_memory.h"

int main()
{
    printf("Using mmap on normal file\n");
    eat_all_memory(EAT_MMAP_FILE);
}
