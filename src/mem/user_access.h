#ifndef NYANOS_USER_ACCESS_H
#define NYANOS_USER_ACCESS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../string.h"

#define USER_SPACE_MAX 0x00007FFFFFFFFFFF

static inline bool is_valid_user_region(const void *ptr, size_t size) {
    uint64_t start = (uint64_t)ptr;
    
    if (size == 0) return true;
    if (start + size < start) return false;
    if (start + size > USER_SPACE_MAX) return false;
    
    return true;
}

static inline int copy_from_user(void *dest, const void *src, size_t size) {
    if (!is_valid_user_region(src, size)) {
        return -1;
    }
    memcpy(dest, src, size);
    return 0;
}

static inline int copy_to_user(void *dest, const void *src, size_t size) {
    if (!is_valid_user_region(dest, size)) {
        return -1;
    }
    memcpy(dest, src, size);
    return 0;
}

static inline int strncpy_from_user(char *dest, const char *src, size_t max_len) {
    if (!is_valid_user_region(src, 1)) return -1;
    
    size_t i;
    for (i = 0; i < max_len; i++) {
        if (!is_valid_user_region(src + i, 1)) return -1;
        dest[i] = src[i];
        if (src[i] == '\0') return i;
    }
    dest[max_len - 1] = '\0'; 
    return i;
}

#endif 