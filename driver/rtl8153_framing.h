#ifndef RTL8153_FRAMING_H
#define RTL8153_FRAMING_H
#include <stddef.h>
#include <stdint.h>
/* Parser state belongs to an immutable copy of one completed USB aggregate. */
int rtl8153_unwrap(const uint8_t *data, size_t size, size_t *offset,
                   void *frame, size_t capacity);
int rtl8153_wrap(void *output, size_t capacity, const void *frame, size_t size);
#endif
