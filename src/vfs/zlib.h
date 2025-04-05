#pragma once

// status codes returned by decompress_gzip_buffer()
#define DECOMPRESS_SUCCESS 0
#define DECOMPRESS_INIT_FAILURE 1
#define DECOMPRESS_MEM_FAILURE 2
#define DECOMPRESS_STREAM_FAILURE 3
#define DECOMPRESS_BUFFER_OVERFLOW 4
#define DECOMPRESS_DATA_ERROR 5

// decompress a gzip-compressed buffer
int decompress_gzip_buffer(const char* input_buffer, size_t input_size, char** output_buffer, size_t* output_size);
