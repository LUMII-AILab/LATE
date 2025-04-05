#pragma once

#define DECLARE_EMBEDDED_DATA(name) \
    extern const char name##_start[]; \
    extern const char name##_end[];

typedef struct {
    const char* data;
    size_t size;
} embedded_data_buffer;

#define EMBEDDED_DATA_BUFFER(name)    (embedded_data_buffer){name##_start, (size_t)(name##_end - name##_start)}
