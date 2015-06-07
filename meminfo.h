struct meminfo {
  unsigned long MemTotal;
  unsigned long MemAvailable;
};

struct meminfo parse_meminfo();
