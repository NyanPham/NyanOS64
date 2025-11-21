#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/*
0: NULL
1: Kernel Code
2: Kernel Data
3&4: TSS Descriptor
5: User Data Senment (ring 3)
6: User Code Segment (ring 3)
*/

#define GDT_ENTRIES 7

// Access Byte
#define SEG_ACCESS_P(x)      ((x) << 7)  // Present
#define SEG_ACCESS_DPL(x)    (((x) & 0x03) << 5) // Descriptor Privilege Level
#define SEG_ACCESS_S(x)      ((x) << 4)  // Descriptor type (0 for system, 1 for code/data)

// Segment types
#define SEG_TYPE_DATA_RDWR   0x02 // Read/Write
#define SEG_TYPE_CODE_EXRD   0x0A // Execute/Read

// Granularity Byte (high nibble)
#define SEG_GRAN_G(x)      ((x) << 7) // Granularity (0 for 1B, 1 for 4KB)
#define SEG_GRAN_DB(x)     ((x) << 6) // Size (0 for 16-bit, 1 for 32)
#define SEG_GRAN_L(x)      ((x) << 5) // Long mode

// Access byte for kernel code/data segments (Ring 0)
#define GDT_ACCESS_KERNEL_CODE (SEG_ACCESS_P(1) | SEG_ACCESS_DPL(0) | SEG_ACCESS_S(1) | SEG_TYPE_CODE_EXRD)
#define GDT_ACCESS_KERNEL_DATA (SEG_ACCESS_P(1) | SEG_ACCESS_DPL(0) | SEG_ACCESS_S(1) | SEG_TYPE_DATA_RDWR)

// Granularity flags for 64-bit kernel segments
#define GDT_GRAN_KERNEL_CODE (SEG_GRAN_G(1) | SEG_GRAN_L(1)) // 4k pages, long mode
#define GDT_GRAN_KERNEL_DATA (SEG_GRAN_G(1)) // 4k pages

#define GDT_OFFSET_KERNEL_CODE 0x08
#define GDT_OFFSET_KERNEL_DATA 0x10

#define GDT_OFFSET_TSS 0x18

#define GDT_ACCESS_USER_CODE (SEG_ACCESS_P(1) | SEG_ACCESS_DPL(3) | SEG_ACCESS_S(1) | SEG_TYPE_CODE_EXRD)
#define GDT_ACCESS_USER_DATA (SEG_ACCESS_P(1) | SEG_ACCESS_DPL(3) | SEG_ACCESS_S(1) | SEG_TYPE_DATA_RDWR)

#define GDT_GRAN_USER_CODE GDT_GRAN_KERNEL_CODE
#define GDT_GRAN_USER_DATA GDT_GRAN_KERNEL_DATA

#define GDT_OFFSET_USER_DATA 0x28
#define GDT_OFFSET_USER_CODE 0x30

struct gdt_entry 
{
    uint16_t limit_low;           
    uint16_t base_low;            
    uint8_t  base_middle;        
    uint8_t  access;              
    uint8_t  granularity;        
    uint8_t  base_high; 
} __attribute__((packed));

struct gdt_ptr 
{
    uint16_t limit;               
    uint64_t base;
} __attribute__((packed));

void gdt_init(void);

/* TSS */
struct tss_t
{
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];    // interrupt stack tables
    uint64_t reserved2;
    uint64_t reserved3;
    uint64_t iomap_base;
} __attribute__((packed));

struct gdt_tss_entry
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;    // with last 4 bits of limits
    uint8_t base_high;
    uint32_t base_highest;  // 32 highest bits of the base addr
    uint32_t reserved;
} __attribute__((packed));

void tss_set_stack(uint64_t stk_ptr);

#endif