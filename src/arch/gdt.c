#include "gdt.h"
#include "mem/pmm.h"

union gdt_union
{
    struct gdt_entry entries[GDT_ENTRIES];
    struct 
    {
        struct gdt_entry null;
        struct gdt_entry kernel_code;
        struct gdt_entry kernel_data;
        struct gdt_tss_entry tss;
        struct gdt_entry user_data;
        struct gdt_entry user_code;
    } structured;
} gdt;

struct gdt_ptr gdt_ptr;
extern void gdt_set(uint64_t);
extern void gdt_load_tss(uint16_t selector);

static struct tss_t g_tss;

static void gdt_encode_entry(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    // Check the limit to make sure that it can be encoded
    // if (limit > 0xFFFFF) 
    // {
    //     kerror("GDT cannot encode limits larger than 0xFFFFF");
    // }

    // Encode the limit, granularity and flags
    gdt.entries[num].limit_low = limit & 0xFFFF;
    gdt.entries[num].granularity = (limit >> 16) & 0x0F;
    gdt.entries[num].granularity |= gran & 0xF0;

    // Encode the base
    gdt.entries[num].base_low = base & 0xFFFF;
    gdt.entries[num].base_middle = (base >> 16) & 0xFF;
    gdt.entries[num].base_high = (base >> 24) & 0xFF;

    // Encode the access byte
    gdt.entries[num].access = access;
}

static void gdt_encode_tss(void)
{
    g_tss.rsp0 = 0;

    uint64_t tss_base = (uint64_t)&g_tss;
    uint32_t tss_limit = sizeof(struct tss_t) - 1;

    struct gdt_tss_entry* tss_entry = &gdt.structured.tss;

    // assign the base addr
    tss_entry->base_low = tss_base & 0xFFFF;
    tss_entry->base_middle = (tss_base >> 16) & 0xFF;
    tss_entry->base_high = (tss_base >> 24) & 0xFF;
    tss_entry->base_highest = (tss_base >> 32) & 0xFFFFFFFF;

    // assign the limit
    tss_entry->limit_low = tss_limit & 0xFFFF;
    tss_entry->granularity = (tss_limit >> 16) & 0x0F;

    // access flags
    // 0x89 = Present(1), DPL(0), System(0), Type=9 (TSS 64-bit)
    tss_entry->access = 0x89;

    tss_entry->granularity |= 0x00;
    tss_entry->reserved = 0x00;
}

void tss_set_stack(uint64_t stk_ptr)
{
    g_tss.rsp0 = stk_ptr;
}

void gdt_init(void)
{
    gdt_ptr.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gdt_ptr.base = (uint64_t)&gdt;

    gdt_encode_entry(0, 0, 0, 0, 0); // null descriptor
    gdt_encode_entry(1, 0, 0xFFFFFFFF, GDT_ACCESS_KERNEL_CODE, GDT_GRAN_KERNEL_CODE); // kernel code segment
    gdt_encode_entry(2, 0, 0xFFFFFFFF, GDT_ACCESS_KERNEL_DATA, GDT_GRAN_KERNEL_DATA); // kernel data
    gdt_encode_tss();
    gdt_encode_entry(5, 0, 0xFFFFFFFF, GDT_ACCESS_USER_DATA, GDT_GRAN_USER_DATA); // user code
    gdt_encode_entry(6, 0, 0xFFFFFFFF, GDT_ACCESS_USER_CODE, GDT_GRAN_USER_CODE); // user code

    gdt_set((uint64_t)&gdt_ptr);
    gdt_load_tss(GDT_OFFSET_TSS);
}