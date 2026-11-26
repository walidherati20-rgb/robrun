#ifndef RR_ASCII_H
#define RR_ASCII_H
#include <stddef.h>
#include <string.h>
/* Accept complete printable names only; no unsequenced index expression and
 * no reading beyond the successful window on a short memory read. */
static int rr_ascii_copy(const char* bytes, size_t readable, char* output, size_t capacity) {
    if (!output || !capacity) return 0;
    output[0] = 0;
    if (!bytes) return 0;
    const char* end = (const char*)memchr(bytes, 0, readable);
    if (!end || end == bytes || (size_t)(end - bytes) >= capacity) return 0;
    size_t length = (size_t)(end - bytes);
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)bytes[i] < 32 || (unsigned char)bytes[i] > 126) return 0;
    memcpy(output, bytes, length);
    output[length] = 0;
    return 1;
}
#endif
