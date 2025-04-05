#pragma once

#include <string>
#include <map>
// #include <iostream>

#include "vfs/vfs.h"

struct VFSSummary {
    size_t total_size;
    size_t file_count;
    size_t dir_count;
    size_t compressed_size;
};

class VFS {
public:
    // VFS() { vfs_info = init_vfs([# strip_components = #] 0 [# 1 #]); load(); }
    VFS(embedded_data_buffer buffer, int strip_components = 0) { _vfs_info = init_vfs_from_buffer(buffer, strip_components); vfs_info = &_vfs_info; load(); }
    ~VFS() { free_vfs(vfs_info); }

    void load() {
        const auto& _entries = vfs_info->entries;
        for(int i=0, sz=vfs_info->n_entries; i<sz; i++) {
            TarEntry& entry = _entries[i];
            std::string path = std::string("/") + entry.name;
            entries[path] = &entry;
        }
    }

    const TarEntry& operator[](const std::string& path) const { return *entries.at(path); }

    bool is_file(std::string path) {
        // if(endsWith(path, '/'))
        if(!path.empty() && path.back() == '/')
            return false;
        auto it = entries.find(path);
        if(it == entries.end())
            return false;
        return it->second->type == TarEntryRegularFile;
    }

    bool is_directory(std::string path) {
        if(!path.empty() && path.back() == '/') {
            auto it = entries.find(path);
            if(it != entries.end())
                return it->second->type == TarEntryDirectory;
        }
        auto it = entries.find(path + "/");
        if(it == entries.end())
            return false;
        return it->second->type == TarEntryDirectory;
    }

    VFSSummary summary() {
        VFSSummary summary = { 0, 0, 0 };
        for (const auto& entry : entries) {
            if(entry.second->type == TarEntryRegularFile) {
                summary.file_count++; summary.total_size += entry.second->size;
            } else if(entry.second->type == TarEntryDirectory)
                summary.dir_count++;
        }
        summary.compressed_size = vfs_info->compressed_size;
        return summary;
        // return std::to_string(total_size) + " bytes in " + std::to_string(file_count) + " files and " + std::to_string(dir_count) + " directories";
    }

    // void list() {
    //     // std::cout << "VFS" << std::endl;
    //     // list_vfs(vfs_info);
    //     std::cerr << "VFS:" << std::endl;
    //     for (const auto& entry : entries) {
    //         if(entry.second->type == TarEntryRegularFile)
    //             std::cout << "FILE " << entry.first << " ; size = " << entry.second->size << std::endl;
    //         else if(entry.second->type == TarEntryDirectory)
    //             std::cout << " DIR " << entry.first << std::endl;
    //     }
    // }

    std::vector<std::string> list() {
        std::vector<std::string> list;
        for (const auto& entry : entries) {
            if(entry.second->type == TarEntryRegularFile)
                list.push_back(entry.first);
        }
        return list;
    }

private:
    struct vfs_info *vfs_info;
    struct vfs_info _vfs_info;
    std::map<std::string,TarEntry*> entries;
};
