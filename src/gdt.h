#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define GDT_ENTRIES 3

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
#define GDT_GRAN_KERNEL_DATA (SEG_GRAN_G(1) | SEG_GRAN_DB(1)) // 4k pages, 32-bit default size

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

#endif