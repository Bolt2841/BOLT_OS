#pragma once
/* ===========================================================================
 * BOLT OS - String Utilities
 * =========================================================================== */

#include "types.hpp"

namespace bolt::str {

inline usize len(const char* s) {
    usize n = 0;
    while (s[n]) n++;
    return n;
}

inline int cmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

inline int ncmp(const char* a, const char* b, usize n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? *a - *b : 0;
}

inline char* cpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

inline char* ncpy(char* dest, const char* src, usize n) {
    char* d = dest;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = '\0';
    return dest;
}

inline char* cat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;  // Find end of dest
    while ((*d++ = *src++));  // Copy src
    return dest;
}

inline char* ncat(char* dest, const char* src, usize n) {
    char* d = dest;
    while (*d) d++;  // Find end of dest
    while (n && (*d++ = *src++)) n--;
    if (n == 0) *d = '\0';
    return dest;
}

inline void* set(void* dest, int c, usize n) {
    u8* d = static_cast<u8*>(dest);
    while (n--) *d++ = static_cast<u8>(c);
    return dest;
}

inline void* memcpy(void* dest, const void* src, usize n) {
    u8* d = static_cast<u8*>(dest);
    const u8* s = static_cast<const u8*>(src);
    while (n--) *d++ = *s++;
    return dest;
}

inline int memcmp(const void* a, const void* b, usize n) {
    const u8* pa = static_cast<const u8*>(a);
    const u8* pb = static_cast<const u8*>(b);
    while (n--) {
        if (*pa != *pb) return *pa - *pb;
        pa++; pb++;
    }
    return 0;
}

// Integer to string conversion
void itoa(i32 value, char* buffer, int base = 10);
void utoa(u32 value, char* buffer, int base = 10);

} // namespace bolt::str
