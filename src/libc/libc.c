#include "libc.h"


/* ======= SYSCALL WRAPPERS =======*/
static inline uint64_t syscall(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    uint64_t ret;
    register uint64_t rax asm("rax") = sys_num;
    register uint64_t rdi asm("rdi") = arg1;
    register uint64_t rsi asm("rsi") = arg2;
    register uint64_t rdx asm("rdx") = arg3;

    /*
    Input:
        rax: sys_num
        rsi: arg1
        rdi: arg2
        rdx: arg3
    
    Output:
        rax: ret
    */
    asm volatile (
        "syscall"
        : "=a"(ret)
        : "r"(rax),
          "r"(rdi),
          "r"(rsi),
          "r"(rdx)
        : "rcx", "r11", "memory"
    );

    return ret;
}

void exit(int status)
{
    syscall(8, (uint64_t)status, 0, 0);
    while (1) {};
}

void print(const char* str)
{
    uint64_t len = strlen(str);
    syscall(1, 1, (uint64_t)str, len);
}

void kprint(const char* s)
{
    syscall(13, (uint64_t)s, 0, 0);
}

int open(const char* pathname, uint32_t flags)
{
    // syscall 10: sys_open
    return (int)syscall(10, (uint64_t)pathname, (uint64_t)flags, 0);
}

int close(int fd)
{
    // syscall 11: sys_close
    return (int)syscall(11, (uint64_t)fd, 0, 0);
}

int read(int fd, void* buf, uint64_t count)
{
    // syscall 0: sys_read
    return (int)syscall(0, (uint64_t)fd, (uint64_t)buf, count);
}

/* ======= STRING, MEMORY FUNCTIONS =======*/
size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) 
    {
        len++;
    }
    return len;
}

char* strcpy(char* dest, const char* src)
{
    char* ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

int strncmp(const char* s1, const char* s2, size_t n) {
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
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void* memset(void *s, int c, size_t n)
{
    unsigned char* p = s;
    while(n--) 
    {
        *p++ = (unsigned char)c;
    }
    return s;
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

/* ======= HEAP MANGER (MALLOC) =======*/

void* sbrk(int64_t incr_payload)
{
    return (void*)syscall(12, (uint64_t)incr_payload, 0, 0);
}

typedef struct block_meta
{
    size_t size;
    struct block_meta* next;
    uint8_t free; // 1 = free, 0 - used
    int magic;
} block_meta;

#define META_SIZE sizeof(block_meta)
#define HEAP_MAGIC 0x12345678

static block_meta* g_heap_base = NULL;

/**
 * @brief Find the free block using First Fit
 */
static block_meta* find_free_blk(block_meta** last, size_t size)
{
    block_meta* curr = g_heap_base;
    while (curr && !(curr->free && curr->size >= size))
    {
        *last = curr;
        curr = curr->next;
    }
    return curr;
}

/**
 * @brief Ask the kernel to alloc more pages/frames
 */
static block_meta* request_space(block_meta* last, size_t size)
{
    block_meta* blk;
    // blk = sbrk(0);    // get the current brk, for debugging
    void* request = sbrk(size + META_SIZE);

    if (request == (void*)-1)
    {
        print("SYS_BRK: Out of memory in request_space\n");
        return NULL;
    }

    blk = (block_meta*)request;
    blk->size = size;
    blk->next = NULL;
    blk->free = 0;
    blk->magic = HEAP_MAGIC;
    
    if (last != NULL) 
    {
        last->next = blk;
    }

    return blk;
}

void* malloc(size_t size)
{
    if (size <= 0)
    {
        return NULL;
    }

    block_meta* blk;

    if (g_heap_base == NULL) // First time calling malloc?
    {
        blk = request_space(NULL, size);
        if (blk == NULL)
        {
            return NULL;
        }
        g_heap_base = blk;
    }
    else
    {
        block_meta* last = g_heap_base;
        blk = find_free_blk(&last, size);
        if (blk == NULL) // no more free blocks in our list -> request more
        {
            blk = request_space(last, size);
            if (blk == NULL) 
            {
                return NULL;
            }
        }
        else // or, found free one, reuse it
        {
            blk->free = 0;
            blk->magic = HEAP_MAGIC;
        }
    }

    return ((uint64_t)blk + META_SIZE); // we return the address right after the header
}

void free(void* ptr)
{
    if (!ptr)
    {
        return;
    }
    
    block_meta* blk_ptr = (block_meta*)((uint64_t)ptr - META_SIZE);

    if (blk_ptr->magic != HEAP_MAGIC)
    {
        print("MALLOC_ERROR: Double free or corruption!\n");
        return;
    }

    blk_ptr->free = 1;
    // TODO: merge free blocks to avoid fragmentation
}

void* realloc(void* ptr, size_t size)
{
    if (ptr == NULL)
    {
        return malloc(size);
    }

    if (size == 0)
    {
        free(ptr);
        return NULL;
    }

    block_meta* blk_ptr = (block_meta*)((uint64_t)ptr - META_SIZE);
    if (blk_ptr->size >= size)  // still enough room!
    {
        return ptr;
    }

    void* new_ptr = malloc(size);
    if (new_ptr == NULL)
    {
        print("REALLOC: out of memory");
        return NULL;
    }

    memcpy(new_ptr, ptr, blk_ptr->size);
    free(ptr);
    return new_ptr;
}