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
    
    for (size_t i = 0; i < n; i++) 
    {
        pdest[i] = psrc[i];
    }

    return dest;
}
 
void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *) s;

    for (size_t i = 0; i < n; i++)
    {
        p[i] = (uint8_t) c;
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