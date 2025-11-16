#ifndef IDT_H
#define IDT_H
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Defines the total number of entries in the Interrupt Descriptor Table (IDT).
 * The x86 architecture supports up to 256 interrupts, from vector 0 to 255.
 */
#define IDT_MAX_DESCRIPTORS 256

/**
 * @struct idt_entry_t
 * @brief Represents an Interrupt Gate Descriptor in the IDT for 64-bit mode.
 *
 * This structure defines the handler for a single interrupt or exception vector.
 * The CPU uses this information to transfer control to the appropriate
 * Interrupt Service Routine (ISR) when an interrupt occurs.
 */
typedef struct 
{
    uint16_t isr_low;       // The lower 16 bits of the ISR's address.
    uint16_t kernel_cs;     // The GDT segment selector that the CPU will load into CS before calling the ISR.
    uint8_t  ist;           // Interrupt Stack Table offset. 0 for not using a separate stack.
    uint8_t  attributes;    // Type and attributes of the descriptor (e.g., present, DPL, gate type).
    uint16_t isr_mid;       // The middle 16 bits (16-31) of the ISR's address.
    uint32_t isr_high;      // The upper 32 bits (32-63) of the ISR's address.
    uint32_t reserved;      // Reserved, must be set to 0.
} __attribute__((packed)) idt_entry_t;

/**
 * @struct idtr_t
 * @brief Defines the pointer to the Interrupt Descriptor Table (IDT).
 *
 * This structure is loaded into the IDTR register using the `lidt` instruction.
 * It tells the CPU where the IDT is located in memory and its size.
 * The __attribute__((packed)) is used to prevent compiler padding.
 */
typedef struct
{
    uint16_t limit; // The size of the IDT in bytes, minus 1.
    uint64_t base;  // The 64-bit linear base address of the IDT.
} __attribute__((packed)) idtr_t;

// A generic, simple exception handler that halts the CPU.
// This is used as a default for unhandled exceptions to prevent triple faults.
__attribute__((noreturn))
void exception_handler(void);

// Sets a descriptor (gate) in the IDT for an interrupt vector.
void idt_set_descriptor(uint8_t vector, void* isr, uint8_t flags);

void idt_init(void);

#endif