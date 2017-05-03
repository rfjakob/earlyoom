struct meminfo {
	long MemTotal;
	long MemAvailable;
	long SwapTotal;
	long SwapFree;
	/* -1 means no data available */
};

void parse_meminfo(struct meminfo * pmi);
