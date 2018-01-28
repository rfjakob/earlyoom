/* SPDX-License-Identifier: MIT */

struct meminfo {
	long MemTotal;
	long MemAvailable;
	long SwapTotal;
	long SwapFree;
	/* -1 means no data available */
};

struct meminfo parse_meminfo();
