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