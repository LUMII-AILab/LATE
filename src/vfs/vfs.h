#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "untar.h"

#include "embed.h"

struct vfs_info {
    TarEntry* entries;// = NULL;
    size_t n_entries;// = 0;
    void* buffer_to_free;// = 0;
    size_t compressed_size;// = 0;
    size_t size;// = 0;
};

// struct vfs_info* init_vfs(int strip_components [# = 1 #]);
struct vfs_info init_vfs_from_buffer(embedded_data_buffer buffer, int strip_components /* = 1 */);
void free_vfs(struct vfs_info* vfs);
void list_vfs(struct vfs_info* vfs);

extern struct vfs_info _vfs;

#ifdef __cplusplus
}
#endif

