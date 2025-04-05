#ifndef __UNTAR_H__
#define __UNTAR_H__

typedef enum {
    TarEntryRegularFile = 0,
    // TarEntryHardlink,  // better just have non-zero linked
    TarEntrySymlink,
    TarEntryDirectory
} TarEntryType;

typedef struct {
    TarEntryType type;
    const char* name;
    const char* content;
    const char* linked;
    size_t size;
} TarEntry;

size_t untar_buffer(const char* buffer, size_t buffer_size, int strip_components, TarEntry** entries);
void free_tar_entries(TarEntry* entries);

#endif
