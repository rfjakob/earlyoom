typedef enum {
    EAT_MALLOC,
    EAT_MMAP_ANON,
    EAT_MMAP_FILE,
} eat_how_enum;

void eat_all_memory(eat_how_enum eat_how);
