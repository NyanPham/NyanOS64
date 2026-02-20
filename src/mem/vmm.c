#include <stdbool.h>
#include "cpu.h"
#include "vmm.h"
#include "pmm.h"
#include "kmalloc.h"
#include "../string.h"
#include "kern_defs.h"
#include "utils/asm_instrs.h"
#include "sched/sched.h"

#include <stddef.h>

uint64_t *kern_pml4 = NULL; // shared to other components

static VmAllocatedList *g_vm_allocated_head;
static VmFreeRegion *g_vm_free_head;

static inline size_t get_aligned_size(size_t size)
{
    return (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static void get_vm_ctx(VmFreeRegion ***free_head_ptr, VmAllocatedList ***alloc_head_ptr)
{
    Task *curr = get_curr_task();

    if (curr != NULL && curr->pid > 0 && curr->vm_free_head != NULL)
    {
        *free_head_ptr = &curr->vm_free_head;
        *alloc_head_ptr = &curr->vm_alloc_head;
    }
    else
    {
        *free_head_ptr = &g_vm_free_head;
        *alloc_head_ptr = &g_vm_allocated_head;
    }
}

uint64_t vmm_new_pml4()
{
    void *pml4_virt = pmm_alloc_frame();
    if (pml4_virt == NULL)
    {
        return NULL;
    }

    uint64_t pml4_phys = vmm_hhdm_to_phys(pml4_virt);
    memset(pml4_virt, 0, PAGE_SIZE);
    memcpy(
        &((uint64_t *)pml4_virt)[256],
        &kern_pml4[256],
        256 * sizeof(uint64_t));

    return pml4_phys;
}

void vmm_ret_pml4(uint64_t pml4_phys)
{
    void *pml4_virt = vmm_phys_to_hhdm(pml4_phys);
    pmm_free_frame(pml4_virt);
}

void vmm_free_table(uint64_t *table, int level)
{
    for (int i = 0; i < 512; i++)
    {
        uint64_t entry = table[i];
        if ((entry & VMM_FLAG_PRESENT) == 0)
        {
            continue;
        }

        uint64_t *virt_addr = vmm_phys_to_hhdm(pte_get_addr(entry));

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

uint64_t pte_get_flags(uint64_t page_tab_entry)
{
    return page_tab_entry & (uint64_t)(0xFFF);
}

static uint64_t *vmm_walk_to_pte(uint64_t *pml4_virt, uint64_t virt_addr, uint8_t create_if_missing)
{
    size_t pml4_idx = (virt_addr >> PML4_INDEX) & 0x1FF; // PML4
    size_t pdpt_idx = (virt_addr >> PDPT_INDEX) & 0x1FF; // Page Directory Pointer Table
    size_t pd_idx = (virt_addr >> PD_INDEX) & 0x1FF;     // Page Directory
    size_t pt_idx = (virt_addr >> PT_INDEX) & 0x1FF;     // Page Table

    uint64_t pml4_entry = pml4_virt[pml4_idx];

    uint64_t *pdpt_virt;
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

            pdpt_virt = (uint64_t *)((uintptr_t)new_tab_phys); // Note that our pmm already converts the addr to virt addr
            memset(pdpt_virt, 0, PAGE_SIZE);

            uint64_t new_phys_addr = vmm_hhdm_to_phys(new_tab_phys);
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
        pdpt_virt = vmm_phys_to_hhdm(pdpt_phys);
    }

    uint64_t pdpt_entry = pdpt_virt[pdpt_idx];
    uint64_t *pd_virt;
    if (!(pdpt_entry & VMM_FLAG_PRESENT))
    {
        if (create_if_missing)
        {
            void *new_tab_phys = pmm_alloc_frame();
            if (new_tab_phys == NULL)
            {
                return NULL;
            }
            pd_virt = (uint64_t *)((uintptr_t)new_tab_phys);
            memset(pd_virt, 0, PAGE_SIZE);

            uint64_t new_phys_addr = vmm_hhdm_to_phys(new_tab_phys);
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
        pd_virt = vmm_phys_to_hhdm(pd_phys);
    }

    uint64_t pd_entry = pd_virt[pd_idx];
    uint64_t *pt_virt;
    if (!(pd_entry & VMM_FLAG_PRESENT))
    {
        if (create_if_missing)
        {
            void *new_tab_phys = pmm_alloc_frame();
            if (new_tab_phys == NULL)
            {
                return NULL;
            }
            pt_virt = (uint64_t *)((uintptr_t)new_tab_phys);
            memset(pt_virt, 0, PAGE_SIZE);

            uint64_t new_phys_addr = vmm_hhdm_to_phys(new_tab_phys);
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
        pt_virt = vmm_phys_to_hhdm(pt_phys);
    }

    return &pt_virt[pt_idx];
}

uint64_t vmm_virt2phys(uint64_t *pml4, uint64_t virt_addr)
{
    uint64_t *pte = vmm_walk_to_pte(pml4, virt_addr, 0);
    if (pte == NULL)
    {
        return 0;
    }

    return pte_get_addr(*pte) + (virt_addr & 0xFFF);
}

void vmm_map_page(uint64_t *pml4_virt, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
    uint64_t *pte = vmm_walk_to_pte(pml4_virt, virt_addr, 1);

    if (pte == NULL)
    {
        return;
    }

    *pte = pte_set_addr(0, phys_addr) | flags;

    __asm__ volatile("invlpg (%0)" ::"r"(virt_addr) : "memory");
}

void vmm_unmap_page(uint64_t *pml4, uint64_t virt_addr)
{
    uint64_t *pte = vmm_walk_to_pte(pml4, virt_addr, 0);

    if (pte == NULL)
    {
        return;
    }

    *pte = 0;

    __asm__ volatile("invlpg (%0)" ::"r"(virt_addr) : "memory");
}

uint64_t find_free_addr(VmFreeRegion **head_ref, size_t size)
{
    if (head_ref == NULL || *head_ref == NULL)
    {
        kprint("VMM: empty or invalid VmFreeRegion list!\n");
        return 0;
    }

    VmFreeRegion *curr = *head_ref;
    VmFreeRegion *prev = NULL;
    while (curr != NULL)
    {
        if (curr->size > size)
        {
            uint64_t addr = curr->addr;
            curr->addr += size;
            curr->size -= size;

            return addr;
        }
        else if (curr->size == size)
        {
            uint64_t addr = curr->addr;
            if (prev != NULL)
            {
                prev->next = curr->next;
                curr->next = NULL;
            }
            else
            {
                *head_ref = curr->next;
            }
            kfree(curr);

            return addr;
        }
        prev = curr;
        curr = curr->next;
    }

    return 0;
}

void *vmm_alloc(size_t size)
{
    cli();
    VmFreeRegion **free_head;
    VmAllocatedList **alloc_head;
    get_vm_ctx(&free_head, &alloc_head);

    size_t aligned_size = get_aligned_size(size);
    uint64_t virt_start_addr = find_free_addr(free_head, aligned_size);
    if (virt_start_addr == 0)
    {
        kprint("VMM ALLOC: Out of memory\n");
        sti();
        return NULL;
    }

    uint64_t npages = aligned_size / PAGE_SIZE;
    uint64_t i = 0;
    uint64_t *pml4 = vmm_phys_to_hhdm(read_cr3());

    for (i = 0; i < npages; i++)
    {
        void *phys_hhdm_addr = pmm_alloc_frame();
        if (phys_hhdm_addr == NULL)
        {
            break;
        }

        vmm_map_page(
            pml4,
            virt_start_addr + i * PAGE_SIZE,
            vmm_hhdm_to_phys(phys_hhdm_addr),
            VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
    }

    if (i < npages) // Partial failure, roll back
    {
        kprint("VMM ALLOC: Out of memory\n");

        for (uint64_t j = 0; j < i; j++)
        {
            uint64_t virt_addr = virt_start_addr + j * PAGE_SIZE;
            uint64_t phys_addr = vmm_virt2phys(pml4, virt_addr);
            vmm_unmap_page(pml4, virt_addr);
            pmm_free_frame(vmm_phys_to_hhdm(phys_addr));
        }

        vmm_add_free_region(free_head, virt_start_addr, aligned_size);
        sti();
        return NULL;
    }

    // append to the allocated list to keep track
    if (vmm_add_allocated_mem(alloc_head, virt_start_addr, aligned_size, 0) < 0)
    {
        for (uint64_t i = 0; i < npages; i++)
        {
            uint64_t virt_addr = virt_start_addr + i * PAGE_SIZE;
            uint64_t phys_addr = vmm_virt2phys(pml4, virt_addr);
            vmm_unmap_page(pml4, virt_addr);
            pmm_free_frame(vmm_phys_to_hhdm(phys_addr));
        }

        vmm_add_free_region(free_head, virt_start_addr, aligned_size);
        sti();
        return NULL;
    }

    sti();
    return (void *)virt_start_addr;
}

void vmm_add_free_region(VmFreeRegion **head_ref, uint64_t addr, size_t size)
{
    if (size == 0)
    {
        return;
    }

    VmFreeRegion *curr = *head_ref;
    VmFreeRegion *prev = NULL;

    while (curr != NULL && curr->addr < addr)
    {
        prev = curr;
        curr = curr->next;
    }

    uint8_t merged = 0;

    // Merge left with Prev
    if (prev != NULL)
    {
        if (prev->addr + prev->size == addr)
        {
            // merge left
            prev->size += size;
            merged = 1;

            // merge right?
            if (curr != NULL && prev->addr + prev->size == curr->addr)
            {
                prev->size += curr->size;
                prev->next = curr->next;
                kfree(curr);
            }
        }
    }

    // If not merged, need a new node
    if (!merged)
    {
        VmFreeRegion *new_node = (VmFreeRegion *)kmalloc(sizeof(VmFreeRegion));
        if (new_node == NULL)
        {
            kprint("VMM FREE: Metadata allocation failed! Memory leaked\n");
            return;
        }

        new_node->addr = addr;
        new_node->size = size;
        new_node->next = curr;

        if (prev != NULL)
        {
            prev->next = new_node;
        }
        else
        {
            *head_ref = new_node;
        }

        // then try Merge right with Next
        if (curr != NULL && new_node->addr + new_node->size == curr->addr)
        {
            new_node->size += curr->size;
            new_node->next = curr->next;
            kfree(curr);
        }
    }
}

void vmm_free(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    VmFreeRegion **free_head;
    VmAllocatedList **alloc_head;
    get_vm_ctx(&free_head, &alloc_head);

    // find and remove from the allocated list
    VmAllocatedList *curr_node = vmm_pop_allocated_mem(alloc_head, (uint64_t)ptr);

    if (curr_node == NULL && (uint64_t)ptr >= KERN_HEAP_START)
    {
        free_head = &g_vm_free_head;
        alloc_head = &g_vm_allocated_head;
        curr_node = vmm_pop_allocated_mem(alloc_head, (uint64_t)ptr);
    }

    if (curr_node == NULL)
    {
        kprint("Trying to free a not allocated memory\n");
        return;
    }

    uint64_t size = curr_node->size;
    // the saved size is already aligned when appended, but we double check here
    size_t aligned_size = get_aligned_size(size);

    uint64_t npages = aligned_size / PAGE_SIZE;
    uint64_t *pml4 = vmm_phys_to_hhdm(read_cr3());
    uint64_t virt_start_addr = (uint64_t)ptr;

    // Physical free
    for (uint64_t i = 0; i < npages; i++)
    {
        uint64_t virt_addr = virt_start_addr + i * PAGE_SIZE;
        uint64_t phys_addr = vmm_virt2phys(pml4, virt_addr);
        if (phys_addr != 0)
        {
            if (!(curr_node->flags & VMM_FLAG_SHM))
            {
                pmm_free_frame(vmm_phys_to_hhdm(phys_addr));
            }
            vmm_unmap_page(pml4, virt_addr);
        }
    }
    // Virtual Allocator Free
    vmm_add_free_region(free_head, virt_start_addr, aligned_size);
    kfree(curr_node);
}

void *vmm_realloc(void *ptr, size_t new_size)
{
    cli();
    if (ptr == NULL || new_size == 0)
    {
        sti();
        return NULL;
    }

    VmFreeRegion **free_head;
    VmAllocatedList **alloc_head;
    get_vm_ctx(&free_head, &alloc_head);

    VmAllocatedList *vm_node = vmm_find_allocated_mem(alloc_head, (uint64_t)ptr);
    if (vm_node == NULL)
    {
        kprint("VMM_REALLOC failed: memory not allocated before\n");
        sti();
        return NULL;
    }

    size_t curr_size = vm_node->size;

    void *new_ptr = vmm_alloc(new_size);
    if (new_ptr == NULL)
    {
        kprint("VMM_REALLOC failed: out of memory!\n");
        sti();
        return NULL;
    }
    memcpy(new_ptr, ptr, (curr_size < new_size) ? curr_size : new_size);
    vmm_free(ptr);
    sti();
    return new_ptr;
}

void vmm_init()
{
    uint64_t pml4_phys = read_cr3();
    kern_pml4 = vmm_phys_to_hhdm(pml4_phys);

    g_vm_free_head = (VmFreeRegion *)kmalloc(sizeof(VmFreeRegion));
    if (g_vm_free_head == NULL)
    {
        kprint("Failed to allocate memory for g_vm_free_head\n");
        return;
    }

    g_vm_free_head->addr = KERN_HEAP_START;
    g_vm_free_head->size = 0x10000000; // 256MB
    g_vm_free_head->next = NULL;
}

int8_t vmm_add_allocated_mem(VmAllocatedList **head_ref, uint64_t addr, size_t size, uint32_t flags)
{
    VmAllocatedList *allocated_node = (VmAllocatedList *)kmalloc(sizeof(VmAllocatedList));
    if (allocated_node == NULL)
    {
        kprint("VMM: Failed to alloc meta data node!\n");
        return -1;
    }
    allocated_node->addr = addr;
    allocated_node->size = size;
    allocated_node->flags = flags;
    allocated_node->next = *head_ref;
    *head_ref = allocated_node;

    return 0;
}

VmAllocatedList *vmm_pop_allocated_mem(VmAllocatedList **head_ref, uint64_t addr)
{
    VmAllocatedList *prev_node = NULL;
    VmAllocatedList *curr_node = *head_ref;
    while (curr_node != NULL)
    {
        if (curr_node->addr == addr)
        {
            break;
        }
        prev_node = curr_node;
        curr_node = curr_node->next;
    }

    if (curr_node != NULL)
    {
        if (prev_node == NULL)
        {
            *head_ref = curr_node->next;
        }
        else
        {
            prev_node->next = curr_node->next;
        }
        curr_node->next = NULL;
    }

    return curr_node;
}

VmAllocatedList *vmm_find_allocated_mem(VmAllocatedList **head_ref, uint64_t addr)
{
    VmAllocatedList *curr_node = head_ref;
    while (curr_node != NULL)
    {
        if (curr_node->addr == addr)
        {
            break;
        }
        curr_node = curr_node->next;
    }

    return curr_node;
}

uint64_t vmm_copy_hierarchy(uint64_t *parent_tbl_virt, int level)
{
    uint64_t *child_tbl_virt = (uint64_t *)pmm_alloc_frame();
    memset(child_tbl_virt, 0, PAGE_SIZE);
    uint64_t child_tbl_phys = vmm_hhdm_to_phys(child_tbl_virt);

    int limit = level == 4 ? 256 : 512;

    if (level > 1)
    {
        // pml4, pdpt, pd
        for (int16_t i = 0; i < limit; i++)
        {
            uint64_t entry = parent_tbl_virt[i];
            if ((entry & VMM_FLAG_PRESENT) == 0)
            {
                continue;
            }

            uint64_t parent_phys = pte_get_addr(entry);
            uint64_t *virt_addr = vmm_phys_to_hhdm(parent_phys);
            uint64_t child_phys = vmm_copy_hierarchy(virt_addr, level - 1);
            child_tbl_virt[i] = child_phys | pte_get_flags(entry);
        }

        if (level == 4)
        {
            for (int16_t i = 256; i < 512; i++)
            {
                child_tbl_virt[i] = parent_tbl_virt[i];
            }
        }
    }
    else
    {
        // pt
        for (int16_t i = 0; i < 512; i++)
        {
            uint64_t entry = parent_tbl_virt[i];
            if ((entry & VMM_FLAG_PRESENT) == 0)
            {
                continue;
            }

            uint64_t parent_phys = pte_get_addr(entry);
            void *parent_virt_hhdm = vmm_phys_to_hhdm(parent_phys);

            parent_tbl_virt[i] &= ~VMM_FLAG_WRITABLE;
            child_tbl_virt[i] = parent_tbl_virt[i];
            pmm_inc_ref(parent_virt_hhdm);
        }
    }

    write_cr3(read_cr3());
    return child_tbl_phys;
}

void *vmm_alloc_global(size_t size)
{
    cli();
    VmFreeRegion **free_head = &g_vm_free_head;
    VmAllocatedList **alloc_head = &g_vm_allocated_head;

    size_t aligned_size = get_aligned_size(size);
    uint64_t virt_start_addr = find_free_addr(free_head, aligned_size);

    if (virt_start_addr == 0)
    {
        kprint("VMM GLOBAL ALLOC: Out of memory\n");
        sti();
        return NULL;
    }

    uint64_t npages = aligned_size / PAGE_SIZE;
    uint64_t i = 0;

    for (i = 0; i < npages; i++)
    {
        void *phys_hhdm_addr = pmm_alloc_frame();
        if (phys_hhdm_addr == NULL)
        {
            break;
        }

        uint64_t phys_addr = vmm_hhdm_to_phys(phys_hhdm_addr);
        uint64_t virt_addr = virt_start_addr + i * PAGE_SIZE;

        vmm_map_page(
            kern_pml4,
            virt_addr,
            phys_addr,
            VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE);

        uint64_t curr_cr3 = read_cr3();
        uint64_t kern_cr3 = vmm_hhdm_to_phys(kern_pml4);

        if (curr_cr3 != kern_cr3)
        {
            uint64_t *curr_pml4 = vmm_phys_to_hhdm(curr_cr3);
            vmm_map_page(curr_pml4, virt_addr, phys_addr, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE);
        }
    }

    if (i < npages) // Partial failure, roll back
    {
        kprint("VMM GLOBAL ALLOC: Out of memory\n");

        for (uint64_t j = 0; j < i; j++)
        {
            uint64_t virt_addr = virt_start_addr + j * PAGE_SIZE;
            uint64_t phys_addr = vmm_virt2phys(kern_pml4, virt_addr);
            vmm_unmap_page(kern_pml4, virt_addr);
            pmm_free_frame(vmm_phys_to_hhdm(phys_addr));
        }

        vmm_add_free_region(free_head, virt_start_addr, aligned_size);
        sti();
        return NULL;
    }

    // append to the allocated list to keep track
    vmm_add_allocated_mem(alloc_head, virt_start_addr, aligned_size, 0);
    sti();
    return (void *)virt_start_addr;
}

VmAllocatedList *vmm_copy_alloc_list(VmAllocatedList *node)
{
    if (node == NULL)
    {
        return NULL;
    }

    VmAllocatedList *new_node = (VmAllocatedList *)kmalloc(sizeof(VmAllocatedList));
    new_node->addr = node->addr;
    new_node->size = node->size;
    new_node->flags = node->flags;
    new_node->next = vmm_copy_alloc_list(node->next);

    return new_node;
}

VmFreeRegion *vmm_copy_free_list(VmFreeRegion *node)
{
    if (node == NULL)
    {
        return NULL;
    }

    VmFreeRegion *new_node = (VmFreeRegion *)kmalloc(sizeof(VmFreeRegion));
    new_node->addr = node->addr;
    new_node->size = node->size;
    new_node->next = vmm_copy_free_list(node->next);

    return new_node;
}

void vmm_cleanup_task(Task *tsk)
{
    VmAllocatedList *a_curr = tsk->vm_alloc_head;
    while (a_curr != NULL)
    {
        VmAllocatedList *next = a_curr->next;
        kfree(a_curr);
        a_curr = next;
    }

    tsk->vm_alloc_head = NULL;

    VmFreeRegion *f_curr = tsk->vm_free_head;
    while (f_curr != NULL)
    {
        VmFreeRegion *next = f_curr->next;
        kfree(f_curr);
        f_curr = next;
    }

    tsk->vm_free_head = NULL;
}

/**
 * @brief Handles #PF caused by the Copy-on-Write (CoW).
 * Checks the page's ref_count.
 * If > 1, it allocates a new physical frame,
 * copies the data, updates the PTE with WRITABLE permission, and frees the old frame.
 * If == 0, just turns on the WRITABLE flag for the address.
 */
int vmm_handle_cow(uint64_t fault_addr)
{
    uint64_t *pml4 = vmm_phys_to_hhdm(pte_get_addr(read_cr3()));
    uint64_t *pte = vmm_walk_to_pte(pml4, fault_addr, 0);

    if (pte == NULL)
    {
        return -1;
    }

    uint64_t *virt_addr = vmm_phys_to_hhdm(pte_get_addr(*pte));
    uint32_t ref_count = pmm_get_ref_count(virt_addr);

    if (ref_count == 1)
    {
        *pte |= VMM_FLAG_WRITABLE;
    }
    else if (ref_count > 1)
    {
        void *hhdm_addr = pmm_alloc_frame();
        memcpy(hhdm_addr, virt_addr, PAGE_SIZE);
        uint64_t flags = pte_get_flags(*pte) | VMM_FLAG_WRITABLE;
        *pte = vmm_hhdm_to_phys(hhdm_addr) | (flags & 0xFFF);
        pmm_free_frame(virt_addr);
    }

    asm volatile("invlpg %0" : : "m"(*(char *)fault_addr) : "memory");
    return 0;
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