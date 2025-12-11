#include <stdbool.h>
#include "cpu.h"
#include "vmm.h"
#include "pmm.h"

extern void* memset(void *s, int c, size_t n);  // from main.c
extern void *memcpy(void *restrict dest, const void *restrict src, size_t n);
extern uint64_t hhdm_offset; // from pmm.c

uint64_t* kern_pml4 = NULL; // shared to other components

uint64_t vmm_new_pml4()
{
    void* pml4_virt = pmm_alloc_frame();
    if (pml4_virt == NULL)
    {
        return NULL;
    }

    uint64_t pml4_phys = ((uint64_t)pml4_virt - hhdm_offset);
    memset(pml4_virt, 0, PAGE_SIZE);
    memcpy(
        &((uint64_t*)pml4_virt)[256], 
        &kern_pml4[256], 
        256 * sizeof(uint64_t)
    );

    return pml4_phys;
}

void vmm_ret_pml4(uint64_t pml4_phys)
{
    void* pml4_virt = pml4_phys + hhdm_offset;
    pmm_free_frame(pml4_virt);
}

void vmm_free_table(uint64_t* table, int level)
{
    for (int i = 0; i < 512; i++)
    {
        uint64_t entry = table[i];
        if ((entry & VMM_FLAG_PRESENT) == 0)
        {
            continue;
        }

        uint64_t* virt_addr = (uint64_t*)(pte_get_addr(entry) + hhdm_offset);

        if (level == 1)
        {
            pmm_free_frame(virt_addr);
        }
        else if (level == 4 && i > 255)
        {
            continue;
        }
        else
        {
            vmm_free_table(virt_addr, level - 1);
        }
    }
    pmm_free_frame(table);
}

uint64_t pte_set_addr(uint64_t page_tab_entry, uint64_t phys_addr)
{
    uint64_t flags = page_tab_entry & 0xFFF;
    uint64_t addr = phys_addr & (uint64_t)(~0xFFF);
    return addr | flags;
}

uint64_t pte_get_addr(uint64_t page_tab_entry)
{
    return page_tab_entry & (uint64_t)(~0xFFF);
}

static uint64_t* vmm_walk_to_pte(uint64_t* pml4_virt, uint64_t virt_addr, bool create_if_missing)
{
    size_t pml4_idx = (virt_addr >> PML4_INDEX) & 0x1FF;    // PML4
    size_t pdpt_idx = (virt_addr >> PDPT_INDEX) & 0x1FF;    // Page Directory Pointer Table
    size_t pd_idx = (virt_addr >> PD_INDEX) & 0x1FF;        // Page Directory
    size_t pt_idx = (virt_addr >> PT_INDEX) & 0x1FF;        // Page Table

    uint64_t pml4_entry = pml4_virt[pml4_idx];

    uint64_t* pdpt_virt;
    if (!(pml4_entry & VMM_FLAG_PRESENT))
    {
        if (create_if_missing)
        {
            /*
            PDPT not exists, and create_if_missing
            - request a new phys memory from PMM
            - convert the phys addr to virt addr using HHDM
            - clear the page
            - update the entry in PML4
            */

            void *new_tab_phys = pmm_alloc_frame();
            if (new_tab_phys == NULL)
            {
                return NULL;
            }

            pdpt_virt = (uint64_t*)((uintptr_t)new_tab_phys); // Note that our pmm already converts the addr to virt addr
            memset(pdpt_virt, 0, PAGE_SIZE);

            uint64_t new_phys_addr = (uintptr_t)new_tab_phys - hhdm_offset;
            uint64_t _flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER;
            pml4_virt[pml4_idx] = pte_set_addr(0, new_phys_addr) | _flags;
        }
        else
        {
            return NULL;
        }
    }
    else 
    {
        // PDPT exists
        uint64_t pdpt_phys = pte_get_addr(pml4_entry);
        pdpt_virt = (uint64_t*)(pdpt_phys + hhdm_offset);
    }

    uint64_t pdpt_entry = pdpt_virt[pdpt_idx];
    uint64_t* pd_virt; 
    if (!(pdpt_entry & VMM_FLAG_PRESENT))
    {
        if (create_if_missing)
        {
            void* new_tab_phys = pmm_alloc_frame();
            if (new_tab_phys == NULL)
            {
                return NULL;
            }
            pd_virt = (uint64_t*)((uintptr_t)new_tab_phys);
            memset(pd_virt, 0, PAGE_SIZE);

            uint64_t new_phys_addr = (uintptr_t)new_tab_phys - hhdm_offset;
            uint64_t _flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER;
            pdpt_virt[pdpt_idx] = pte_set_addr(0, new_phys_addr) | _flags;
        }
        else 
        {
            return NULL;
        }
    }
    else 
    {
        uint64_t pd_phys = pte_get_addr(pdpt_entry);
        pd_virt = (uint64_t*)(pd_phys + hhdm_offset);
    }

    uint64_t pd_entry = pd_virt[pd_idx];
    uint64_t* pt_virt;
    if (!(pd_entry & VMM_FLAG_PRESENT))
    {
        if (create_if_missing)
        {
            void* new_tab_phys = pmm_alloc_frame();
            if (new_tab_phys == NULL)
            {
                return NULL;
            }
            pt_virt = (uint64_t*)((uintptr_t)new_tab_phys);
            memset(pt_virt, 0, PAGE_SIZE);

            uint64_t new_phys_addr = (uintptr_t)new_tab_phys - hhdm_offset;
            uint64_t _flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER;
            pd_virt[pd_idx] = pte_set_addr(0, new_phys_addr) | _flags;
        }
        else 
        {
            return NULL;
        }
    }
    else 
    {
        uint64_t pt_phys = pte_get_addr(pd_entry);
        pt_virt = (uint64_t*)(pt_phys + hhdm_offset);
    }

    return &pt_virt[pt_idx];
}

uint64_t vmm_virt2phys(uint64_t* pml4, uint64_t virt_addr)
{
    uint64_t* pte = vmm_walk_to_pte(pml4, virt_addr, false);
    if (pte == NULL)
    {
        return 0;
    }

    return pte_get_addr(*pte) + (virt_addr & 0xFFF);
}

void vmm_map_page(uint64_t* pml4_virt, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
    uint64_t* pte = vmm_walk_to_pte(pml4_virt, virt_addr, true);

    if (pte == NULL)
    {
        return;
    }

    *pte = pte_set_addr(0, phys_addr) | flags;

    __asm__ volatile ("invlpg (%0)" :: "r"(virt_addr) : "memory");
}

void vmm_unmap_page(uint64_t* pml4, uint64_t virt_addr) {
    uint64_t* pte = vmm_walk_to_pte(pml4, virt_addr, false);

    if (pte == NULL) {
        return;
    }
    
    *pte = 0;

    __asm__ volatile ("invlpg (%0)" :: "r"(virt_addr) : "memory");
}

void vmm_init()
{
    uint64_t pml4_phys = read_cr3();
    kern_pml4 = (uint64_t*)(pml4_phys + hhdm_offset);

    void* test_page_virt = pmm_alloc_frame(); 
    if (test_page_virt) {
        uint64_t test_page_phys = (uintptr_t)test_page_virt - hhdm_offset;
        uint64_t flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE;

        vmm_map_page(kern_pml4, 0xABCD000, test_page_phys, flags);

        volatile uint64_t* test_ptr = (uint64_t*)0xABCD000;
        *test_ptr = 0xDEADBEEF; 
        
        if (*test_ptr != 0xDEADBEEF) {
            for (;;) { asm ("hlt"); }
        }

        vmm_unmap_page(kern_pml4, 0xABCD000);
        pmm_free_frame(test_page_virt);
        
        // TODO: need to handle the PAGE FAULT
        // *test_ptr = 0xCAFEBABE; // running this will cause PAGE FAULT
    }
}

/*
More explanation, for learning notes :))
A 64-bit virtual address (in practice, only 48 bits are used for addressing) is divided into the following parts

---
  63                               48 47            39 38            30 29            21 20            12 11            0
+------------------------------------+----------------+----------------+----------------+----------------+----------------+
|          Sign Extension            |   PML4 Index   |   PDPT Index   |    PD Index    |    PT Index    |  Page Offset   |
+------------------------------------+----------------+----------------+----------------+----------------+----------------+
^                                    ^                ^                ^                ^                ^
|                                    |                |                |                |                |
Bits 48-63 must be a copy of bit 47  |                |                |                |                +-> 12 bits
(Canonical Address)                  |                |                |                +------------------> 9 bits
                                     |                |                +------------------------------------> 9 bits
                                     |                +------------------------------------------------------> 9 bits
                                     +------------------------------------------------------------------------> 9 bits
---

Address Translation Process:
1. The CPU uses bits 47-39 of the virtual address as an index to find a PML4 Entry.
2. This PML4 Entry contains the physical address of a PDPT.
3. The CPU then uses the next 9 bits (38-30) as an index to find a PDPT Entry within that table.
4. This PDPT Entry contains the physical address of a Page Directory (PD).
5. The CPU continues by using bits 29-21 as an index to find a PD Entry.
6. This PD Entry contains the physical address of a Page Table (PT).
7. The CPU uses the final 9 index bits (20-12) to find a Page Table Entry (PTE).
8. This PTE contains the physical address of the target physical frame.
9. Finally, the CPU adds the 12-bit Page Offset (bits 11-0) from the virtual address to the physical frame address to get the final physical address.

*/