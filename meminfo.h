/* SPDX-License-Identifier: MIT */
#ifndef MEMINFO_H
#define MEMINFO_H

typedef struct {
    // Values from /proc/meminfo, in KiB or converted to MiB.
    long MemTotalKiB;
    int MemTotalMiB;
    int MemAvailableMiB; // -1 means no data available
    int SwapTotalMiB;
    long SwapTotalKiB;
    int SwapFreeMiB;
    // Calculated percentages
    int MemAvailablePercent; // percent of total memory that is available
    int SwapFreePercent; // percent of total swap that is free
} meminfo_t;

meminfo_t parse_meminfo();

#endif
