#include "./string.h"

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }

    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n > 0 && *s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
    {
        return 0;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcpy(char *dest, const char *src)
{
    char *ret = dest;
    while ((*dest++ = *src++))
        ;
    return ret;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    char *ret = dest;
    for (size_t i = 0; i < n; i++)
    {
        if (*src != 0)
        {
            *dest = *src;
            dest++;
            src++;
        }
        else // padding
        {
            *dest = 0;
            dest++;
        }
    }
    return ret;
}

size_t strlen(const char *s)
{
    size_t size = 0;

    while (*s != '\0')
    {
        size++;
        s++;
    }

    return size;
}

char *strcat(char *dest, const char *src)
{
    char *tmp = dest;

    while (*dest != '\0')
    {
        dest++;
    }

    while ((*dest++ = *src++))
        ;

    return tmp;
}

void *memcpy(void *restrict dest, const void *restrict src, size_t n)
{
    uint8_t *restrict pdest = (uint8_t *restrict)dest;
    const uint8_t *restrict psrc = (const uint8_t *restrict)src;

    // HEAD: Copy each byte until pdest to ensure alignment
    size_t i = 0;
    while (((uint64_t)(&pdest[i]) & 0x7) != 0 && i < n)
    {
        *(uint8_t *restrict)(&pdest[i]) = *(uint8_t *restrict)(&psrc[i]);
        i += 1;
    }

    // BODY
    // Loop unrolling for 64-bytes at a time
    while (i + 64 <= n)
    {
        *(uint64_t *restrict)(&pdest[i]) = *(uint64_t *restrict)(&psrc[i]);
        *(uint64_t *restrict)(&pdest[i + 8]) = *(uint64_t *restrict)(&psrc[i + 8]);
        *(uint64_t *restrict)(&pdest[i + 16]) = *(uint64_t *restrict)(&psrc[i + 16]);
        *(uint64_t *restrict)(&pdest[i + 24]) = *(uint64_t *restrict)(&psrc[i + 24]);
        *(uint64_t *restrict)(&pdest[i + 32]) = *(uint64_t *restrict)(&psrc[i + 32]);
        *(uint64_t *restrict)(&pdest[i + 40]) = *(uint64_t *restrict)(&psrc[i + 40]);
        *(uint64_t *restrict)(&pdest[i + 48]) = *(uint64_t *restrict)(&psrc[i + 48]);
        *(uint64_t *restrict)(&pdest[i + 56]) = *(uint64_t *restrict)(&psrc[i + 56]);
        i += 64;
    }

    // Loop unrolling for 32-bytes at a time
    while (i + 32 <= n)
    {
        *(uint64_t *restrict)(&pdest[i]) = *(uint64_t *restrict)(&psrc[i]);
        *(uint64_t *restrict)(&pdest[i + 8]) = *(uint64_t *restrict)(&psrc[i + 8]);
        *(uint64_t *restrict)(&pdest[i + 16]) = *(uint64_t *restrict)(&psrc[i + 16]);
        *(uint64_t *restrict)(&pdest[i + 24]) = *(uint64_t *restrict)(&psrc[i + 24]);
        i += 32;
    }

    // Loop unrolling for 16-bytes at a time
    while (i + 16 <= n)
    {
        *(uint64_t *restrict)(&pdest[i]) = *(uint64_t *restrict)(&psrc[i]);
        *(uint64_t *restrict)(&pdest[i + 8]) = *(uint64_t *restrict)(&psrc[i + 8]);
        i += 16;
    }

    // Binary Decomposition
    while (i + 8 <= n)
    {
        *(uint64_t *restrict)(&pdest[i]) = *(uint64_t *restrict)(&psrc[i]);
        i += 8;
    }

    while (i + 4 <= n)
    {
        *(uint32_t *restrict)(&pdest[i]) = *(uint32_t *restrict)(&psrc[i]);
        i += 4;
    }

    while (i + 2 <= n)
    {
        *(uint16_t *restrict)(&pdest[i]) = *(uint16_t *restrict)(&psrc[i]);
        i += 2;
    }

    // TAIL
    while (i < n)
    {
        *(uint8_t *restrict)(&pdest[i]) = *(uint8_t *restrict)(&psrc[i]);
        i += 1;
    }

    return dest;
}

void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;
    uint64_t c64 = (uint64_t)(uint8_t)c * 0x0101010101010101;
    uint32_t c32 = (uint32_t)(uint8_t)c * 0x01010101;
    uint32_t c16 = (uint16_t)(uint8_t)c * 0x0101;

    // HEAD: Copy each byte until pdest to ensure alignment
    size_t i = 0;
    while (((uint64_t)(&p[i]) & 0x7) != 0 && i < n)
    {
        *(uint8_t *restrict)(&p[i]) = (uint8_t)c;
        i += 1;
    }

    // BODY
    // Loop unrolling for 64-bytes at a time
    while (i + 64 <= n)
    {
        *(uint64_t *restrict)(&p[i]) = c64;
        *(uint64_t *restrict)(&p[i + 8]) = c64;
        *(uint64_t *restrict)(&p[i + 16]) = c64;
        *(uint64_t *restrict)(&p[i + 24]) = c64;
        *(uint64_t *restrict)(&p[i + 32]) = c64;
        *(uint64_t *restrict)(&p[i + 40]) = c64;
        *(uint64_t *restrict)(&p[i + 48]) = c64;
        *(uint64_t *restrict)(&p[i + 56]) = c64;
        i += 64;
    }

    // Binary Decomposition
    while (i + 8 <= n)
    {
        *(uint64_t *restrict)(&p[i]) = c64;
        i += 8;
    }

    while (i + 4 <= n)
    {
        *(uint32_t *restrict)(&p[i]) = c32;
        i += 4;
    }

    while (i + 2 <= n)
    {
        *(uint16_t *restrict)(&p[i]) = c16;
        i += 2;
    }

    // TAIL
    while (i < n)
    {
        *(uint8_t *restrict)(&p[i]) = c;
        i += 1;
    }

    return s;
}

void *memmove(void *dest, const void *src, size_t n)
{
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    if (src > dest)
    {
        for (size_t i = 0; i < n; i++)
        {
            pdest[i] = psrc[i];
        }
    }
    else if (src < dest)
    {
        for (size_t i = n; i > 0; i--)
        {
            pdest[i - 1] = psrc[i - 1];
        }
    }

    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    for (size_t i = 0; i < n; i++)
    {
        if (p1[i] != p2[i])
        {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}

__attribute__((target("sse,sse2"))) void *memcpy_sse(void *restrict dest, const void *restrict src, size_t n)
{
    uint8_t *restrict pdest = (uint8_t *restrict)dest;
    const uint8_t *restrict psrc = (const uint8_t *restrict)src;

    // HEAD: Copy each byte until pdest to ensure alignment
    size_t i = 0;
    while (((uint64_t)(&pdest[i]) & 0x7) != 0 && i < n)
    {
        *(uint8_t *restrict)(&pdest[i]) = *(uint8_t *restrict)(&psrc[i]);
        i += 1;
    }

    // BODY
    // Loop unrolling for 64-bytes at a time
    while (i + 64 <= n)
    {
        asm volatile(
            "movdqu 0(%1), %%xmm0\n\t"
            "movdqu 16(%1), %%xmm1\n\t"
            "movdqu 32(%1), %%xmm2\n\t"
            "movdqu 48(%1), %%xmm3\n\t"
            "movdqu %%xmm0, 0(%0)\n\t"
            "movdqu %%xmm1, 16(%0)\n\t"
            "movdqu %%xmm2, 32(%0)\n\t"
            "movdqu %%xmm3, 48(%0)\n\t"
            :
            : "r"(&pdest[i]), "r"(&psrc[i])
            : "memory", "xmm0", "xmm1", "xmm2", "xmm3");
        i += 64;
    }

    // Loop unrolling for 32-bytes at a time
    while (i + 32 <= n)
    {
        asm volatile(
            "movdqu 0(%1), %%xmm0\n\t"
            "movdqu 16(%1), %%xmm1\n\t"
            "movdqu %%xmm0, 0(%0)\n\t"
            "movdqu %%xmm1, 16(%0)\n\t"
            :
            : "r"(&pdest[i]), "r"(&psrc[i])
            : "memory", "xmm0", "xmm1");
        i += 32;
    }

    // Loop unrolling for 16-bytes at a time
    while (i + 16 <= n)
    {
        asm volatile(
            "movdqu 0(%1), %%xmm0\n\t"
            "movdqu %%xmm0, 0(%0)\n\t"
            :
            : "r"(&pdest[i]), "r"(&psrc[i])
            : "memory", "xmm0");
        i += 16;
    }

    // Binary Decomposition
    while (i + 8 <= n)
    {
        *(uint64_t *restrict)(&pdest[i]) = *(uint64_t *restrict)(&psrc[i]);
        i += 8;
    }

    while (i + 4 <= n)
    {
        *(uint32_t *restrict)(&pdest[i]) = *(uint32_t *restrict)(&psrc[i]);
        i += 4;
    }

    while (i + 2 <= n)
    {
        *(uint16_t *restrict)(&pdest[i]) = *(uint16_t *restrict)(&psrc[i]);
        i += 2;
    }

    // TAIL
    while (i < n)
    {
        *(uint8_t *restrict)(&pdest[i]) = *(uint8_t *restrict)(&psrc[i]);
        i += 1;
    }

    return dest;
}

__attribute__((target("sse,sse2"))) void *memset_sse(void *s, int c, size_t n)
{
    uint8_t *restrict pdest = (uint8_t *restrict)s;
    uint64_t c64 = (uint64_t)(uint8_t)c * 0x0101010101010101ULL;

    size_t i = 0;
    while (((uint64_t)(&pdest[i]) & 0x7) != 0 && i < n)
    {
        *(uint8_t *restrict)(&pdest[i]) = (uint8_t)c;
        i += 1;
    }

    // BODY
    // Loop unrolling for 64-bytes at a time
    while (i + 64 <= n)
    {
        asm volatile(
            "movq %1, %%xmm0\n\t"
            "punpcklqdq %%xmm0, %%xmm0\n\t" // Unpack and Interleave Low Quadwords
            "movdqu %%xmm0, 0(%0)\n\t"
            "movdqu %%xmm0, 16(%0)\n\t"
            "movdqu %%xmm0, 32(%0)\n\t"
            "movdqu %%xmm0, 48(%0)\n\t"
            :
            : "r"(&pdest[i]), "r"(c64)
            : "memory", "xmm0");
        i += 64;
    }

    // Loop unrolling for 32-bytes at a time
    while (i + 32 <= n)
    {
        asm volatile(
            "movq %1, %%xmm0\n\t"
            "punpcklqdq %%xmm0, %%xmm0\n\t"
            "movdqu %%xmm0, 0(%0)\n\t"
            "movdqu %%xmm0, 16(%0)\n\t"
            :
            : "r"(&pdest[i]), "r"(c64)
            : "memory", "xmm0");
        i += 32;
    }

    // Loop unrolling for 16-bytes at a time
    while (i + 16 <= n)
    {
        asm volatile(
            "movq %1, %%xmm0\n\t"
            "punpcklqdq %%xmm0, %%xmm0\n\t"
            "movdqu %%xmm0, 0(%0)\n\t"
            :
            : "r"(&pdest[i]), "r"(c64)
            : "memory", "xmm0");
        i += 16;
    }

    // Binary Decomposition
    while (i + 8 <= n)
    {
        *(uint64_t *restrict)(&pdest[i]) = c64;
        i += 8;
    }

    while (i + 4 <= n)
    {
        *(uint32_t *restrict)(&pdest[i]) = (uint32_t)c64;
        i += 4;
    }

    while (i + 2 <= n)
    {
        *(uint16_t *restrict)(&pdest[i]) = (uint16_t)c64;
        i += 2;
    }

    // TAIL
    while (i < n)
    {
        *(uint8_t *restrict)(&pdest[i]) = *(uint8_t *restrict)c64;
        i += 1;
    }

    return s;
}

__attribute__((target("sse,sse2"))) void *memmove_sse(void *dest, const void *src, size_t n)
{
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    if (pdest == psrc || n == 0)
    {
        return dest;
    }

    if (pdest < psrc)
    {
        return memcpy_sse(dest, src, n);
    }
    else
    {
        size_t i = n;

        while (i % 16 != 0)
        {
            i--;
            pdest[i] = psrc[i];
        }

        while (i >= 16)
        {
            i -= 16;
            asm volatile(
                "movdqu 0(%1), %%xmm0\n\t"
                "movdqu %%xmm0, 0(%0)\n\t"
                :
                : "r"(&pdest[i]), "r"(&psrc[i])
                : "memory", "xmm0");
        }
        return dest;
    }
}

__attribute__((target("sse,sse2"))) int memcmp_sse(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    size_t i = 0;

    while (i + 16 <= n)
    {
        /*
        pcmpeqb (Packed Compare for Equal Bytes)
        compares each byte position between 2 xmm registers.
        For each position, if equal -> 0xFF, otherwise -> 0x00

        pmovmskb (Packed Move Mask to Integer) extracts the most significant big (MSG) of each byte in a reg,
        and stores the results somewhere else.
        In this case it's the variable `mask`.

        So `mask` has 16 bits, representing 16 bytes of comparisons.
        If `mask` is not -1, then we know there is at least 1 difference in
        the 16 bytes.
        */
        uint32_t mask;
        asm volatile(
            "movdqu 0(%1), %%xmm0\n\t"
            "movdqu 0(%2), %%xmm1\n\t"
            "pcmpeqb %%xmm1, %%xmm0\n\t"
            "pmovmskb %%xmm0, %0\n\t"
            : "=r"(mask)
            : "r"(&p1[i]), "r"(&p2[i])
            : "memory", "xmm0", "xmm1");

        if (mask != 0xFFFF)
        {
            break;
        }
        i += 16;
    }

    while (i < n)
    {
        if (p1[i] != p2[i])
        {
            return p1[i] < p2[i] ? -1 : 1;
        }
        i++;
    }

    return 0;
}

char *strstr(const char *haystack, const char *needle)
{
    while (*haystack != '\0')
    {
        const char *curr_haystack = haystack;
        const char *curr_needle = needle;
        while (*curr_haystack != '\0' && *curr_haystack == *curr_needle)
        {
            curr_haystack++;
            curr_needle++;
        }

        if (*curr_needle == '\0')
        {
            return (char *)haystack;
        }
        haystack++;
    }
    return NULL;
}

char *strchr(const char *haystack, const char needle)
{
    while (*haystack != '\0')
    {
        if (*haystack == needle)
        {
            return (char *)haystack;
        }
        haystack++;
    }

    if (needle == '\0')
    {
        return (char *)haystack;
    }

    return NULL;
}
