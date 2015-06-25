struct meminfo {
  unsigned long MemTotal;
  unsigned long MemAvailable;
  unsigned long SwapTotal;
  unsigned long SwapFree;
};

struct meminfo parse_meminfo();
