#include <stddef.h>

void *memset(void *destination, int value, size_t bytes)
{
    unsigned char *out = destination;
    unsigned char byte = (unsigned char)value;
    while (bytes--) *out++ = byte;
    return destination;
}

void *memcpy(void *destination, const void *source, size_t bytes)
{
    unsigned char *out = destination;
    const unsigned char *in = source;
    while (bytes--) *out++ = *in++;
    return destination;
}
