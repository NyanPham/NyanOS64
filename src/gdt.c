#include "gdt.h"

struct gdt_entry gdt[GDT_ENTRIES];
struct gdt_ptr gdt_ptr;

extern void gdt_set(uint64_t);

static void gdt_encode_entry(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    // Check the limit to make sure that it can be encoded
    // if (limit > 0xFFFFF) 
    // {
    //     kerror("GDT cannot encode limits larger than 0xFFFFF");
    // }

    // Encode the limit, granularity and flags
    gdt[num].limit_low = limit & 0xFFFF;
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;

    // Encode the base
    gdt[num].base_low = base & 0xFFFF;
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;

    // Encode the access byte
    gdt[num].access = access;
}

void gdt_init(void)
{
    gdt_ptr.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gdt_ptr.base = (uint64_t)&gdt;

    gdt_encode_entry(0, 0, 0, 0, 0); // null descriptor
    gdt_encode_entry(1, 0, 0xFFFFFFFF, GDT_ACCESS_KERNEL_CODE, GDT_GRAN_KERNEL_CODE); // code segment
    gdt_encode_entry(2, 0, 0xFFFFFFFF, GDT_ACCESS_KERNEL_DATA, GDT_GRAN_KERNEL_DATA); // data

    gdt_set((uint64_t)&gdt_ptr);
}
