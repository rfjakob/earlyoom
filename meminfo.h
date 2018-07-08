/* SPDX-License-Identifier: MIT */

struct meminfo {
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
};

struct meminfo parse_meminfo();
