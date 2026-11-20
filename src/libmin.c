/* libmin.c - the two functions GCC may call on its own in a -nostdlib build
 * (structure copies and clears). Their names and signatures are fixed by the
 * C standard, which is why this file reuses reserved identifiers:
 * docs/deviations/target.md, record T-3. */
#include <stddef.h>
#include <stdint.h>

void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    for (size_t i = 0U; i < n; i++) {
        d[i] = (uint8_t)c;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0U; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}
