/* ===========================================================================
 * BOLT OS - String Utilities Implementation
 * =========================================================================== */

#include "string.hpp"

namespace bolt::str {

void itoa(i32 value, char* buffer, int base) {
    char* ptr = buffer;
    char* ptr1 = buffer;
    char tmp;
    i32 v;
    
    if (value < 0 && base == 10) {
        *ptr++ = '-';
        ptr1++;
        value = -value;
    }
    
    do {
        v = value % base;
        *ptr++ = (v < 10) ? '0' + v : 'a' + v - 10;
        value /= base;
    } while (value);
    
    *ptr-- = '\0';
    
    while (ptr1 < ptr) {
        tmp = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp;
    }
}

void utoa(u32 value, char* buffer, int base) {
    char* ptr = buffer;
    char* ptr1 = buffer;
    char tmp;
    u32 v;
    
    do {
        v = value % base;
        *ptr++ = (v < 10) ? '0' + v : 'a' + v - 10;
        value /= base;
    } while (value);
    
    *ptr-- = '\0';
    
    while (ptr1 < ptr) {
        tmp = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp;
    }
}

} // namespace bolt::str
