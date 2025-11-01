#include "idt.h"
#include "gdt.h"

extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq2_stub(void);
extern void irq3_stub(void);
extern void irq4_stub(void);
extern void irq5_stub(void);
extern void irq6_stub(void);
extern void irq7_stub(void);
extern void irq8_stub(void);
extern void irq9_stub(void);
extern void irq10_stub(void);
extern void irq11_stub(void);
extern void irq12_stub(void);
extern void irq13_stub(void);
extern void irq14_stub(void);
extern void irq15_stub(void);

__attribute((aligned(0x10)))
static idt_entry_t idt[256];

static idtr_t idtr;

void exception_handler()
{
    __asm__ volatile ("cli; hlt");
}

void idt_set_descriptor(uint8_t vector, void* isr, uint8_t flags)
{
    idt_entry_t* descriptor = &idt[vector];

    descriptor->isr_low = (uint64_t)isr & 0xFFFF;
    descriptor->kernel_cs = GDT_OFFSET_KERNEL_CODE;
    descriptor->ist = 0;
    descriptor->attributes = flags;
    descriptor->isr_mid = ((uint64_t)isr >> 16) & 0xFFFF;
    descriptor->isr_high = ((uint64_t)isr >> 32) & 0xFFFFFFFF;
    descriptor->reserved = 0;
}

static bool vectors[IDT_MAX_DESCRIPTORS];
extern void* isr_stub_table[];

void idt_init()
{   
    idtr.base = (uintptr_t)&idt[0];
    idtr.limit = (uint16_t)sizeof(idt_entry_t) * IDT_MAX_DESCRIPTORS - 1;

    for (uint8_t vector = 0; vector < 32; vector++)
    {
        idt_set_descriptor(vector, isr_stub_table[vector], 0x8E);
        vectors[vector] = true;
    }
    
    idt_set_descriptor(32, irq0_stub, 0x8E);
    idt_set_descriptor(33, irq1_stub, 0x8E);
    idt_set_descriptor(34, irq2_stub, 0x8E);
    idt_set_descriptor(35, irq3_stub, 0x8E);
    idt_set_descriptor(36, irq4_stub, 0x8E);
    idt_set_descriptor(37, irq5_stub, 0x8E);
    idt_set_descriptor(38, irq6_stub, 0x8E);
    idt_set_descriptor(39, irq7_stub, 0x8E);
    idt_set_descriptor(40, irq8_stub, 0x8E);
    idt_set_descriptor(41, irq9_stub, 0x8E);
    idt_set_descriptor(42, irq10_stub, 0x8E);
    idt_set_descriptor(43, irq11_stub, 0x8E);
    idt_set_descriptor(44, irq12_stub, 0x8E);
    idt_set_descriptor(45, irq13_stub, 0x8E);
    idt_set_descriptor(46, irq14_stub, 0x8E);
    idt_set_descriptor(47, irq15_stub, 0x8E);


    __asm__ volatile ("lidt %0" : : "m"(idtr));
    __asm__ volatile ("sti");
}
