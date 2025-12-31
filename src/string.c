#include "./string.h"

int strcmp(const char* s1, const char* s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }

    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char* strcpy(char* dest, const char* src)
{
    char* ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

char* strncpy(char* dest, const char* src, size_t n)
{
    char* ret = dest;
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

size_t strlen(const char* s)
{
    size_t size = 0;

    while (*s != '\0')
    {
        size++;
        s++;
    }

    return size;
}

char* strcat(char* dest, const char* src)
{
    char *tmp = dest;

    while (*dest != '\0')
    {
        dest++;
    }

    while ((*dest++ = *src++));

    return tmp;
}

void *memcpy(void *restrict dest, const void *restrict src, size_t n) 
{
    uint8_t *restrict pdest = (uint8_t *restrict) dest;
    const uint8_t *restrict psrc = (const uint8_t *restrict) src;
    
    // HEAD: Copy each byte until pdest to ensure alignment
    size_t i = 0;
    while (((uint64_t)(&pdest[i]) & 0x7) != 0 && i < n)
    {
        *(uint8_t* restrict)(&pdest[i]) = *(uint8_t* restrict)(&psrc[i]);
        i += 1;
    }

    // BODY
    // Loop unrolling for 64-bytes at a time
    while (i + 64 <= n)
    {
        *(uint64_t* restrict)(&pdest[i]) = *(uint64_t* restrict)(&psrc[i]);
        *(uint64_t* restrict)(&pdest[i+8]) = *(uint64_t* restrict)(&psrc[i+8]);
        *(uint64_t* restrict)(&pdest[i+16]) = *(uint64_t* restrict)(&psrc[i+16]);
        *(uint64_t* restrict)(&pdest[i+24]) = *(uint64_t* restrict)(&psrc[i+24]);
        *(uint64_t* restrict)(&pdest[i+32]) = *(uint64_t* restrict)(&psrc[i+32]);
        *(uint64_t* restrict)(&pdest[i+40]) = *(uint64_t* restrict)(&psrc[i+40]);
        *(uint64_t* restrict)(&pdest[i+48]) = *(uint64_t* restrict)(&psrc[i+48]);
        *(uint64_t* restrict)(&pdest[i+56]) = *(uint64_t* restrict)(&psrc[i+56]);
        i += 64;
    }

    // Loop unrolling for 32-bytes at a time
    while (i + 32 <= n)
    {
        *(uint64_t* restrict)(&pdest[i]) = *(uint64_t* restrict)(&psrc[i]);
        *(uint64_t* restrict)(&pdest[i+8]) = *(uint64_t* restrict)(&psrc[i+8]);
        *(uint64_t* restrict)(&pdest[i+16]) = *(uint64_t* restrict)(&psrc[i+16]);
        *(uint64_t* restrict)(&pdest[i+24]) = *(uint64_t* restrict)(&psrc[i+24]);
        i += 32;
    }

    // Loop unrolling for 16-bytes at a time
    while (i + 16 <= n)
    {
        *(uint64_t* restrict)(&pdest[i]) = *(uint64_t* restrict)(&psrc[i]);
        *(uint64_t* restrict)(&pdest[i+8]) = *(uint64_t* restrict)(&psrc[i+8]);
        i += 16;
    }

    // Binary Decomposition
    while (i + 8 <= n)
    {
        *(uint64_t* restrict)(&pdest[i]) = *(uint64_t* restrict)(&psrc[i]);
        i += 8;
    }

    while (i + 4 <= n)
    {
        *(uint32_t* restrict)(&pdest[i]) = *(uint32_t* restrict)(&psrc[i]);
        i += 4;
    }

    while (i + 2 <= n)
    {
        *(uint16_t* restrict)(&pdest[i]) = *(uint16_t* restrict)(&psrc[i]);
        i += 2;
    }

    // TAIL
    while (i < n)
    {
        *(uint8_t* restrict)(&pdest[i]) = *(uint8_t* restrict)(&psrc[i]);
        i += 1;
    }

    return dest;
}

void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *) s;
    uint64_t c64 = (uint64_t)(uint8_t)c * 0x0101010101010101;
    uint32_t c32 = (uint32_t)(uint8_t)c * 0x01010101;
    uint32_t c16 = (uint16_t)(uint8_t)c * 0x0101;

    // HEAD: Copy each byte until pdest to ensure alignment
    size_t i = 0;
    while (((uint64_t)(&p[i]) & 0x7) != 0 && i < n)
    {
        *(uint8_t* restrict)(&p[i]) = (uint8_t)c;
        i += 1;
    }

    // BODY
    // Loop unrolling for 64-bytes at a time
    while (i + 64 <= n)
    {
        *(uint64_t* restrict)(&p[i]) = c64;
        *(uint64_t* restrict)(&p[i+8]) = c64;
        *(uint64_t* restrict)(&p[i+16]) = c64;
        *(uint64_t* restrict)(&p[i+24]) = c64;
        *(uint64_t* restrict)(&p[i+32]) = c64;
        *(uint64_t* restrict)(&p[i+40]) = c64;
        *(uint64_t* restrict)(&p[i+48]) = c64;
        *(uint64_t* restrict)(&p[i+56]) = c64;
        i += 64;
    }

    // Binary Decomposition
    while (i + 8 <= n)
    {
        *(uint64_t* restrict)(&p[i]) = c64;
        i += 8;
    }

    while (i + 4 <= n)
    {
        *(uint32_t* restrict)(&p[i]) = c32;
        i += 4;
    }

    while (i + 2 <= n)
    {
        *(uint16_t* restrict)(&p[i]) = c16;
        i += 2;
    }

    // TAIL
    while (i < n)
    {
        *(uint8_t* restrict)(&p[i]) = c;
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
            pdest[i-1] = psrc[i-1];
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