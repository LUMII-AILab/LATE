#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <libgen.h>
#include <stdbool.h>

#include "untar.h"

#define TAR_BLOCK_SIZE 512

typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag[1];
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} TarHeader;

unsigned int octalToDecimal(const char *octal) {
    unsigned int result = 0;
    while (*octal && *octal >= '0' && *octal <= '7') {
        result = (result << 3) + (*octal - '0');
        octal++;
    }
    return result;
}


/*
// Function to resolve relative path relative to base path
char* resolve_relative_path(const char* base_path, const char* relative_path, bool is_dir) {
    char* result = (char*)malloc(PATH_MAX);
    if (result == NULL) {
        perror("malloc");
        return NULL;
    }

    char base_path_copy[PATH_MAX];

    if (!is_dir) {
        // get directory component of base_path

        // create a copy of the file path because dirname might modify it
        strncpy(base_path_copy, base_path, sizeof(base_path_copy));

        base_path = dirname(base_path_copy);
    }

    // Combine base path and relative path
    snprintf(result, PATH_MAX, "%s/%s", base_path, relative_path);

    // Normalize the combined path
    char* resolved_path = realpath(result, NULL);
    if (resolved_path == NULL) {
        perror("realpath");
        free(result);
        return NULL;
    }

    // Copy the resolved path to the result buffer
    strncpy(result, resolved_path, PATH_MAX);
    free(resolved_path);

    return result;
}
*/

char* resolve_relative_path(const char* base_path, const char* relative_path, bool is_dir) {

    char base_path_copy[PATH_MAX];

    if (!is_dir) {
        // get directory component of base_path

        // create a copy of the file path because dirname might modify it
        strncpy(base_path_copy, base_path, sizeof(base_path_copy));

        base_path = dirname(base_path_copy);
    }

    // Allocate memory for the combined path
    size_t combined_length = strlen(base_path) + strlen(relative_path) + 2;
    char* combined_path = (char*)malloc(combined_length);
    if (combined_path == NULL) {
        perror("malloc");
        return NULL;
    }

    // Combine base path and relative path
    snprintf(combined_path, combined_length, "%s/%s", base_path, relative_path);

    // Split the path into components and normalize it
    char* components[256];
    size_t component_count = 0;
    char* token = strtok(combined_path, "/");
    while (token != NULL) {
        if (strcmp(token, ".") == 0) {
            // Ignore current directory components
        } else if (strcmp(token, "..") == 0) {
            // Go up one directory level
            if (component_count > 0) {
                component_count--;
            }
        } else {
            // Add the component to the list
            components[component_count++] = token;
        }
        token = strtok(NULL, "/");
    }

    // Construct the normalized path
    char* resolved_path = (char*)malloc(combined_length);
    if (resolved_path == NULL) {
        perror("malloc");
        free(combined_path);
        return NULL;
    }
    resolved_path[0] = '\0';
    for (size_t i = 0; i < component_count; i++) {
        strcat(resolved_path, components[i]);
        if (i < component_count - 1) {
            strcat(resolved_path, "/");
        }
    }

    // Free the combined path memory
    free(combined_path);
    return resolved_path;
}

void strip_and_prefix_path(char path[100], int n) {
    // Strip "./" if present at the beginning
    while (strncmp(path, "./", 2) == 0) {
        memmove(path, path + 2, strlen(path + 2) + 1);
    }
    if (n == 0)
        return;
    if (strncmp(path, "/", 1) == 0)
        return;

    // Strip n path components from the beginning
    while (n > 0) {
        // Check if the path starts with "../"
        if (strncmp(path, "../", 3) == 0) {
            // Add another "../" prefix
            memmove(path + 3, path, strlen(path) + 1);
            memcpy(path, "../", 3);
            n--;
            continue;
        }
        char *first_slash = strchr(path, '/');
        if (first_slash != NULL) {
            memmove(path, first_slash + 1, strlen(first_slash));
            n--;
        } else {
            // Path is just a filename, prepend "../"
            memmove(path + 3, path, strlen(path) + 1);
            memcpy(path, "../", 3);
            n--;
        }
    }
}

size_t untar_buffer(const char* buffer, size_t buffer_size, int strip_components, TarEntry** entries) {

    // first get the number of entries

    size_t i = 0, n_entries = 0;

    while (i < buffer_size) {
        TarHeader* header = (TarHeader*)(buffer + i);

        // check for end of archive
        if (strlen(header->name) == 0)
            break;

        // check magic to ensure it's a USTAR formatted tar
        if (strncmp(header->magic, "ustar", 5) != 0) {
            fprintf(stderr, "Invalid or unsupported TAR format for entry %s.\n", header->name);
            return 0;
        }

        unsigned int fileSize = octalToDecimal(header->size);
        unsigned int blocks = (fileSize / TAR_BLOCK_SIZE) + (fileSize % TAR_BLOCK_SIZE ? 1 : 0);

        // header->typeflag[0] values:
        // '0' or '\0' - regular file
        // '1' - hardlink, target = header->linkname
        // '2' - symbolic link, target = header->linkname
        // '5' - directory

        // count only regular files or directories
        char tf = header->typeflag[0];

        if (tf == '0' || tf == '\0' || tf == '1' || tf == '2' || tf == '5')
            n_entries++;

        i += (blocks + 1) * TAR_BLOCK_SIZE; // move to the next header
    }

    if(entries == NULL) {
        return n_entries;
    }

    if(*entries == NULL) {
        *entries = (TarEntry*)malloc(sizeof(TarEntry) * (n_entries + 1));
        if(*entries == NULL) {
            return 0;
        }
    }

    /* size_t */ i = 0;
    int n_entry = 0;
    while (i < buffer_size && n_entry < n_entries) {
        TarHeader* header = (TarHeader*)(buffer + i);

        if (strlen(header->name) == 0)
            break;

        char* name = header->name;
        char* linkname = header->linkname;

        if (header->typeflag[0] == '2' /* symlink */) {
            if (strncmp(linkname, "/", 1) != 0) {
                char* resolved_path = resolve_relative_path(name, linkname, false);
                if (resolved_path != NULL) {
                    strncpy(header->linkname, resolved_path, sizeof(header->linkname));
                    free(resolved_path);
                }
            }
        }

        // TODO: if strip_components, then also linkname must be stripped in case of a  relative filename not starting with ../
        // NOTE: when stripping component from linkname and only file component is left, add '../', and if there is already ../, then add one more ../

        strip_and_prefix_path(header->name, strip_components);
        if (strlen(header->linkname) > 0)
            strip_and_prefix_path(header->linkname, strip_components);

        /*
        int nstrip = strip_components;

        if (strncmp(name, "./", 2) == 0) {
            nstrip++;
        }

        for (int j = 0; j < nstrip; j++) {
            char* new_start = strchr(name, '/');
            if (new_start) {
                name = new_start + 1;
            }
        }
        */

        /*
        if (strncmp(header->magic, "ustar", 5) != 0) {
            fprintf(stderr, "Invalid or unsupported TAR format for entry %s.\n", name);
            return 0;
        }
        */

        unsigned int fileSize = octalToDecimal(header->size);
        unsigned int blocks = (fileSize / TAR_BLOCK_SIZE) + (fileSize % TAR_BLOCK_SIZE ? 1 : 0);

        // header->typeflag[0] values:
        // '0' or '\0' - regular file
        // '1' - hardlink, target = header->linkname
        // '2' - symbolic link, target = header->linkname
        // '5' - directory

        char tf = header->typeflag[0];

        // handle only regular files and directories
        if (/* strlen(name) > 0 && */ (tf == '0' || tf == '\0' || tf == '1' || tf == '2' || tf == '5')) {
            TarEntry* entry = &(*entries)[n_entry];

            if (tf == '0' || tf == '\0' || tf == '1') // include also hardlinked files
                entry->type = TarEntryRegularFile;
            // else if (tf == '1')
            //     entry->type = TarEntryHardlink;
            else if (tf == '2')
                entry->type = TarEntrySymlink;
            else if (tf == '5')
                entry->type = TarEntryDirectory;
            entry->name = name;
            entry->linked = (tf == '1' || tf == '2') ? header->linkname : NULL;

            entry->size = fileSize;
            entry->content = buffer + (i + TAR_BLOCK_SIZE);

            if (tf == '1' || tf == '2') {
                // try to find the file in archive
                for (int j = 0; j < n_entry; j++) {
                    TarEntry* e = &(*entries)[j];
                    if (e->type != TarEntryRegularFile) // cannot process directories or 2nd-level symlinks (at the moment)
                        continue;
                    // NOTE: in case of a relative symlink, the name must be resolved relative entry->name, but strip components must be taken into account...
                    if (strcmp(entry->linked, e->name) == 0) {
                        entry->size = e->size;
                        entry->content = e->content;
                        break;
                    }
                }
            }


            /*
            size_t offset = i + TAR_BLOCK_SIZE; // move past the header
            const char* content = buffer + offset
            */

            n_entry++;
        }

        i += (blocks + 1) * TAR_BLOCK_SIZE; // move to the next header
    }

    TarEntry* entry = &(*entries)[n_entry];
    entry->name = NULL;
    entry->linked = NULL;
    entry->size = 0;
    entry->content = NULL;

    return n_entry;
}

void free_tar_entries(TarEntry* entries) {
    free(entries);
}
