#include <stdio.h>
#include <stdlib.h>

#include "vfs.h"
/* #include "untar.h" */
#ifndef NO_ZLIB
#include "zlib.h"
#endif

// extern const char vfs_data_start[];
// extern const char vfs_data_end[];
// DECLARE_EMBEDDED_DATA(vfs_data)

struct vfs_info _vfs;


struct vfs_info init_vfs_from_buffer(embedded_data_buffer buffer, int strip_components /* = 1 */) {
    struct vfs_info _vfs = {NULL, 0, NULL, 0, 0};

    _vfs.buffer_to_free = NULL;

#ifndef NO_ZLIB
    char* decompressed_buffer;
    size_t decompressed_size;

    if (DECOMPRESS_SUCCESS == decompress_gzip_buffer(buffer.data, buffer.size, &decompressed_buffer, &decompressed_size)) {
        _vfs.buffer_to_free = decompressed_buffer;
        _vfs.n_entries = untar_buffer(decompressed_buffer, decompressed_size, strip_components, &_vfs.entries);
        _vfs.compressed_size = buffer.size;
        _vfs.size = decompressed_size;
        return _vfs;
    }
#endif

    _vfs.n_entries = untar_buffer(buffer.data, buffer.size, strip_components, &_vfs.entries);
    _vfs.size = buffer.size;

    return _vfs;
}

/*
struct vfs_info* init_vfs(int strip_components [# = 1 #]) {
    _vfs.buffer_to_free = NULL;

    const char* start = vfs_data_start;
    const char* end = vfs_data_end;
    size_t size = end - start;

#ifndef NO_ZLIB
    char* decompressed_buffer;
    size_t decompressed_size;

    if (DECOMPRESS_SUCCESS == decompress_gzip_buffer(start, size, &decompressed_buffer, &decompressed_size)) {
        _vfs.buffer_to_free = decompressed_buffer;
        _vfs.n_entries = untar_buffer(decompressed_buffer, decompressed_size, strip_components, &_vfs.entries);
        _vfs.compressed_size = size;
        _vfs.size = decompressed_size;
        return &_vfs;
    }
#endif

    _vfs.n_entries = untar_buffer(start, size, strip_components, &_vfs.entries);
    _vfs.size = size;

    return &_vfs;
}
*/

void free_vfs(struct vfs_info* vfs) {
    free_tar_entries(vfs->entries);
    if (vfs->buffer_to_free != NULL) {
        free(vfs->buffer_to_free);
        vfs->buffer_to_free = NULL;
    }
}

void list_vfs(struct vfs_info* vfs) {

    /*
    printf("Size of embedded data: %zu bytes\n", size);
    
    // Use the data as required...
    printf("content: %s", start);
    
    size_t bufferSize = size;
    [# char* buffer = readFileIntoBuffer("data.tar", &bufferSize); #]
    [# if (buffer) { #]
    [#     printf("Read %zu bytes from the file.\n", bufferSize); #]
    [# } #]
    const char* buffer = start;
    [# const char* tarBuffer = ...; // Your TAR data here #]
    [# size_t bufferSize = ...;    // Size of your TAR data #]
    
    TarEntry* entries = NULL;
    size_t n_entries = untar_buffer(buffer, bufferSize, 1, &entries);
    */

    /* init_vfs(1); */

    const char *typestring[] = { "FILE", /* "HARDLINK", */ "SYMLINK", "DIR" };

    for(int k=0; k<vfs->n_entries; k++) {
        TarEntry* entry = &vfs->entries[k];
        /* printf("%s: %s, size: %zu\n", entry->type == TarEntryRegularFile ? "FILE" : "DIR", entry->name, entry->size); */
        printf("%s: %s%s%s, size: %zu\n", typestring[entry->type], entry->name,
                entry->linked != NULL ? " -> " : "", entry->linked != NULL ? entry->linked : "", entry->size);
    }

    /* free_vfs(vfs); */

    /* free(entries); */
    /* free_tar_entries(entries); */

    /* free(buffer); */

    return;
}

