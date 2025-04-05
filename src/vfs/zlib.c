#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "zlib.h" // only for the same status codes as commented below

#define CHUNK 1024

// status codes returned by decompress_gzip_buffer()
// #define DECOMPRESS_SUCCESS 0
// #define DECOMPRESS_INIT_FAILURE 1
// #define DECOMPRESS_MEM_FAILURE 2
// #define DECOMPRESS_STREAM_FAILURE 3
// #define DECOMPRESS_BUFFER_OVERFLOW 4
// #define DECOMPRESS_DATA_ERROR 5

// decompress a gzip-compressed buffer
int decompress_gzip_buffer(const char* input_buffer, size_t input_size, char** output_buffer, size_t* output_size) {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.next_in = (Bytef*)input_buffer;
    strm.avail_in = input_size;

    if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK) {
        return DECOMPRESS_INIT_FAILURE;
    }

    size_t buffer_size = CHUNK;
    *output_buffer = malloc(buffer_size);
    if (*output_buffer == NULL) {
        inflateEnd(&strm);
        return DECOMPRESS_MEM_FAILURE;
    }

    size_t total_size = 0;
    int ret;
    do {
        if (total_size + CHUNK > buffer_size) {
            buffer_size *= 2;
            char* temp_buffer = realloc(*output_buffer, buffer_size);
            if (temp_buffer == NULL) {
                free(*output_buffer);
                inflateEnd(&strm);
                return DECOMPRESS_MEM_FAILURE;
            }
            *output_buffer = temp_buffer;
        }

        strm.next_out = (Bytef*)(*output_buffer + total_size);
        strm.avail_out = CHUNK;

        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR) {
            inflateEnd(&strm);
            return DECOMPRESS_STREAM_FAILURE;
        }

        switch (ret) {
            case Z_NEED_DICT:
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                inflateEnd(&strm);
                return DECOMPRESS_DATA_ERROR;
        }

        total_size += CHUNK - strm.avail_out;

    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        return DECOMPRESS_BUFFER_OVERFLOW;
    }

    *output_size = total_size;
    return DECOMPRESS_SUCCESS;
}
