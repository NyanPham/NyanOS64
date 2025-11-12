#include <stdbool.h>
#include "vmm.h"
#include "pmm.h"

#define VMM_FLAG_PRESENT 1
#define VMM_FLAG_WRITABLE (1 << 1)
#define VMM_FLAG_USER (1 << 2)

#define ENTRIES_NUM (4096 / sizeof(uint64_t))

#define PML4_INDEX 0x27
#define PDPT_INDEX 0x1e
#define PD_INDEX 0x15
#define PT_INDEX 0xc

extern void* memset(void *s, int c, size_t n);
extern uint64_t hhdm_offset;

static uint64_t* kernel_pml4 = NULL;

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
    size_t pml4_idx = (virt_addr >> PML4_INDEX) & 0x1FF;
    size_t pdpt_idx = (virt_addr >> PDPT_INDEX) & 0x1FF;
    size_t pd_idx = (virt_addr >> PD_INDEX) & 0x1FF;
    size_t pt_idx = (virt_addr >> PT_INDEX) & 0x1FF;

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

void vmm_map_page(uint64_t* pml4_virt, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
    uint64_t* pte = vmm_walk_to_pte(pml4_virt, virt_addr, true);

    if (pte == NULL)
    {
        return;
    }

    *pte = pte_set_addr(0, phys_addr) | flags;
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
    uint64_t pml4_phys;
    asm volatile(
        "mov %%cr3, %0"
        : "=r"(pml4_phys)
        : 
        : "memory"
    );

    kernel_pml4 = (uint64_t*)(pml4_phys + hhdm_offset);

    void* test_page_virt = pmm_alloc_frame(); 
    if (test_page_virt) {
        uint64_t test_page_phys = (uintptr_t)test_page_virt - hhdm_offset;
        uint64_t flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE;

        vmm_map_page(kernel_pml4, 0xABCD000, test_page_phys, flags);

        volatile uint64_t* test_ptr = (uint64_t*)0xABCD000;
        *test_ptr = 0xDEADBEEF; 
        
        if (*test_ptr != 0xDEADBEEF) {
            for (;;) { asm ("hlt"); }
        }

        vmm_unmap_page(kernel_pml4, 0xABCD000);
        pmm_free_frame(test_page_virt);
        
        // TODO: need to handle the PAGE FAULT
        // *test_ptr = 0xCAFEBABE; // running this will cause PAGE FAULT
    }
}