#include <stdbool.h>
#include <stddef.h>
#include "cpu.h"
#include "vmm.h"
#include "pmm.h"
#include "kmalloc.h"
#include "../string.h"
#include "kern_defs.h"
#include "utils/asm_instrs.h"
#include "utils/spinlock.h"
#include "sched/sched.h"
#include "drivers/serial.h"

uint64_t *kern_pml4 = NULL;

static VmAllocatedList *g_vm_allocated_head = NULL;
static VmFreeRegion *g_vm_free_head = NULL;
static spinlock_t vmm_lock = SPINLOCK_INIT;

static inline size_t get_aligned_size(size_t size)
{
    return (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

uint64_t vmm_new_pml4(void)
{
    uint64_t pml4_phys = pmm_alloc_frame();
    if (pml4_phys == 0)
    {
        return 0;
    }

    void *pml4_virt = vmm_phys_to_hhdm(pml4_phys);
    memset(pml4_virt, 0, PAGE_SIZE);
    memcpy(
        &((uint64_t *)pml4_virt)[256],
        &kern_pml4[256],
        256 * sizeof(uint64_t));

    return pml4_phys;
}

void vmm_ret_pml4(uint64_t pml4_phys)
{
    if (pml4_phys != 0)
    {
        pmm_free_frame(pml4_phys);
    }
}

void vmm_free_table(uint64_t *table, int level)
{
    if (table == NULL)
    {
        return;
    }

    for (int i = 0; i < 512; i++)
    {
        if (level == 4 && i >= 256)
        {
            // Never free higher-half kernel page tables!
            continue;
        }

        uint64_t entry = table[i];
        if ((entry & VMM_FLAG_PRESENT) == 0)
        {
            continue;
        }

        uint64_t phys_addr = pte_get_addr(entry);
        uint64_t *virt_addr = vmm_phys_to_hhdm(phys_addr);

        if (level == 1)
        {
            if (!(entry & PTE_FLAG_SHARED))
            {
                pmm_free_frame(phys_addr);
            }
        }
        else
        {
            vmm_free_table(virt_addr, level - 1);
        }
    }
    pmm_free_frame(vmm_hhdm_to_phys(table));
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
    size_t pml4_idx = (virt_addr >> PML4_INDEX) & 0x1FF;
    size_t pdpt_idx = (virt_addr >> PDPT_INDEX) & 0x1FF;
    size_t pd_idx = (virt_addr >> PD_INDEX) & 0x1FF;
    size_t pt_idx = (virt_addr >> PT_INDEX) & 0x1FF;

    uint64_t pml4_entry = pml4_virt[pml4_idx];
    uint64_t *pdpt_virt;
    if (!(pml4_entry & VMM_FLAG_PRESENT))
    {
        if (create_if_missing)
        {
            uint64_t new_tab_phys = pmm_alloc_frame();
            if (new_tab_phys == 0)
            {
                return NULL;
            }
            pdpt_virt = (uint64_t *)vmm_phys_to_hhdm(new_tab_phys);
            memset(pdpt_virt, 0, PAGE_SIZE);
            pml4_virt[pml4_idx] = pte_set_addr(0, new_tab_phys) | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER;
        }
        else
        {
            return NULL;
        }
    }
    else
    {
        pdpt_virt = vmm_phys_to_hhdm(pte_get_addr(pml4_entry));
    }

    uint64_t pdpt_entry = pdpt_virt[pdpt_idx];
    uint64_t *pd_virt;
    if (!(pdpt_entry & VMM_FLAG_PRESENT))
    {
        if (create_if_missing)
        {
            uint64_t new_phys_addr = pmm_alloc_frame();
            if (new_phys_addr == 0)
            {
                return NULL;
            }
            pd_virt = (uint64_t *)vmm_phys_to_hhdm(new_phys_addr);
            memset(pd_virt, 0, PAGE_SIZE);
            pdpt_virt[pdpt_idx] = pte_set_addr(0, new_phys_addr) | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER;
        }
        else
        {
            return NULL;
        }
    }
    else
    {
        pd_virt = vmm_phys_to_hhdm(pte_get_addr(pdpt_entry));
    }

    uint64_t pd_entry = pd_virt[pd_idx];
    uint64_t *pt_virt;
    if (!(pd_entry & VMM_FLAG_PRESENT))
    {
        if (create_if_missing)
        {
            uint64_t new_phys_addr = pmm_alloc_frame();
            if (new_phys_addr == 0)
            {
                return NULL;
            }
            pt_virt = (uint64_t *)vmm_phys_to_hhdm(new_phys_addr);
            memset(pt_virt, 0, PAGE_SIZE);
            pd_virt[pd_idx] = pte_set_addr(0, new_phys_addr) | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER;
        }
        else
        {
            return NULL;
        }
    }
    else
    {
        pt_virt = vmm_phys_to_hhdm(pte_get_addr(pd_entry));
    }

    return &pt_virt[pt_idx];
}

uint64_t vmm_virt2phys(uint64_t *pml4, uint64_t virt_addr)
{
    uint64_t *pte = vmm_walk_to_pte(pml4, virt_addr, 0);
    if (pte == NULL || !(*pte & VMM_FLAG_PRESENT))
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

    if (*pte & VMM_FLAG_PRESENT)
    {
        uint64_t old_phys = pte_get_addr(*pte);
        if (old_phys != phys_addr && !(*pte & PTE_FLAG_SHARED))
        {
            pmm_dec_ref(old_phys);
        }
    }

    *pte = pte_set_addr(0, phys_addr) | flags | VMM_FLAG_PRESENT;
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

    if (prev != NULL && (prev->addr + prev->size == addr))
    {
        prev->size += size;
        merged = 1;

        if (curr != NULL && (prev->addr + prev->size == curr->addr))
        {
            prev->size += curr->size;
            prev->next = curr->next;
            kfree(curr);
        }
    }

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

        if (curr != NULL && (new_node->addr + new_node->size == curr->addr))
        {
            new_node->size += curr->size;
            new_node->next = curr->next;
            kfree(curr);
        }
    }
}

void *vmm_alloc_global(size_t size)
{
    if (size == 0)
    {
        return NULL;
    }

    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&vmm_lock, rflags);

    size_t aligned_size = get_aligned_size(size);
    uint64_t virt_start_addr = find_free_addr(&g_vm_free_head, aligned_size);

    if (virt_start_addr == 0)
    {
        kprint("VMM GLOBAL ALLOC: Out of memory\n");
        SPIN_UNLOCK_IRQRESTORE(&vmm_lock, rflags);
        return NULL;
    }

    uint64_t npages = aligned_size / PAGE_SIZE;
    uint64_t i = 0;
    uint64_t curr_cr3 = read_cr3();
    uint64_t kern_cr3 = vmm_hhdm_to_phys(kern_pml4);
    uint64_t *curr_pml4 = vmm_phys_to_hhdm(curr_cr3);

    for (i = 0; i < npages; i++)
    {
        uint64_t phys_addr = pmm_alloc_frame();
        if (phys_addr == 0)
        {
            break;
        }

        uint64_t virt_addr = virt_start_addr + i * PAGE_SIZE;

        vmm_map_page(
            kern_pml4,
            virt_addr,
            phys_addr,
            VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE);

        if (curr_cr3 != kern_cr3)
        {
            // Keep upper-half PML4 slot synced in the active task's PML4
            size_t pml4_idx = (virt_addr >> PML4_INDEX) & 0x1FF;
            curr_pml4[pml4_idx] = kern_pml4[pml4_idx];
            __asm__ volatile("invlpg (%0)" ::"r"(virt_addr) : "memory");
        }
    }

    if (i < npages)
    {
        kprint("VMM GLOBAL ALLOC: Out of memory\n");
        for (uint64_t j = 0; j < i; j++)
        {
            uint64_t virt_addr = virt_start_addr + j * PAGE_SIZE;
            uint64_t phys_addr = vmm_virt2phys(kern_pml4, virt_addr);
            vmm_unmap_page(kern_pml4, virt_addr);
            if (phys_addr != 0)
            {
                pmm_free_frame(phys_addr);
            }
        }
        vmm_add_free_region(&g_vm_free_head, virt_start_addr, aligned_size);
        SPIN_UNLOCK_IRQRESTORE(&vmm_lock, rflags);
        return NULL;
    }

    if (vmm_add_allocated_mem(&g_vm_allocated_head, virt_start_addr, aligned_size, 0) < 0)
    {
        for (uint64_t j = 0; j < npages; j++)
        {
            uint64_t virt_addr = virt_start_addr + j * PAGE_SIZE;
            uint64_t phys_addr = vmm_virt2phys(kern_pml4, virt_addr);
            vmm_unmap_page(kern_pml4, virt_addr);
            if (phys_addr != 0)
            {
                pmm_free_frame(phys_addr);
            }
        }
        vmm_add_free_region(&g_vm_free_head, virt_start_addr, aligned_size);
        SPIN_UNLOCK_IRQRESTORE(&vmm_lock, rflags);
        return NULL;
    }

    SPIN_UNLOCK_IRQRESTORE(&vmm_lock, rflags);
    return (void *)virt_start_addr;
}

void *vmm_alloc(size_t size)
{
    return vmm_alloc_global(size);
}

void vmm_free(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&vmm_lock, rflags);

    VmAllocatedList *curr_node = vmm_pop_allocated_mem(&g_vm_allocated_head, (uint64_t)ptr);
    if (curr_node == NULL)
    {
        SPIN_UNLOCK_IRQRESTORE(&vmm_lock, rflags);
        Task *curr = get_curr_task();
        if (curr != NULL && curr->pid > 0)
        {
            vmm_unmap_user(curr, ptr);
            return;
        }
        kprint("Trying to free a not allocated memory\n");
        return;
    }

    size_t aligned_size = get_aligned_size(curr_node->size);
    uint64_t npages = aligned_size / PAGE_SIZE;
    uint64_t virt_start_addr = (uint64_t)ptr;

    for (uint64_t i = 0; i < npages; i++)
    {
        uint64_t virt_addr = virt_start_addr + i * PAGE_SIZE;
        uint64_t phys_addr = vmm_virt2phys(kern_pml4, virt_addr);
        if (phys_addr != 0)
        {
            vmm_unmap_page(kern_pml4, virt_addr);
            if (!(curr_node->flags & VMM_FLAG_SHM))
            {
                pmm_free_frame(phys_addr);
            }
        }
    }

    vmm_add_free_region(&g_vm_free_head, virt_start_addr, aligned_size);
    kfree(curr_node);

    SPIN_UNLOCK_IRQRESTORE(&vmm_lock, rflags);
}

int vmm_unmap_user(Task *tsk, void *ptr)
{
    if (tsk == NULL || ptr == NULL)
    {
        return -1;
    }

    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&vmm_lock, rflags);

    VmAllocatedList *curr_node = vmm_pop_allocated_mem(&tsk->vm_alloc_head, (uint64_t)ptr);
    if (curr_node == NULL)
    {
        SPIN_UNLOCK_IRQRESTORE(&vmm_lock, rflags);
        return -1;
    }

    size_t aligned_size = get_aligned_size(curr_node->size);
    uint64_t npages = aligned_size / PAGE_SIZE;
    uint64_t *pml4 = vmm_phys_to_hhdm(tsk->pml4);
    uint64_t virt_start_addr = (uint64_t)ptr;

    for (uint64_t i = 0; i < npages; i++)
    {
        uint64_t virt_addr = virt_start_addr + i * PAGE_SIZE;
        uint64_t phys_addr = vmm_virt2phys(pml4, virt_addr);
        if (phys_addr != 0)
        {
            vmm_unmap_page(pml4, virt_addr);
            if (!(curr_node->flags & VMM_FLAG_SHM))
            {
                pmm_free_frame(phys_addr);
            }
        }
    }

    vmm_add_free_region(&tsk->vm_free_head, virt_start_addr, aligned_size);
    kfree(curr_node);

    SPIN_UNLOCK_IRQRESTORE(&vmm_lock, rflags);
    return 0;
}

void *vmm_realloc(void *ptr, size_t new_size)
{
    if (ptr == NULL || new_size == 0)
    {
        return NULL;
    }

    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&vmm_lock, rflags);
    VmAllocatedList *vm_node = vmm_find_allocated_mem(&g_vm_allocated_head, (uint64_t)ptr);
    if (vm_node == NULL)
    {
        SPIN_UNLOCK_IRQRESTORE(&vmm_lock, rflags);
        kprint("VMM_REALLOC failed: memory not allocated before\n");
        return NULL;
    }
    size_t curr_size = vm_node->size;
    SPIN_UNLOCK_IRQRESTORE(&vmm_lock, rflags);

    void *new_ptr = vmm_alloc_global(new_size);
    if (new_ptr == NULL)
    {
        kprint("VMM_REALLOC failed: out of memory!\n");
        return NULL;
    }
    memcpy(new_ptr, ptr, (curr_size < new_size) ? curr_size : new_size);
    vmm_free(ptr);
    return new_ptr;
}

void vmm_init(void)
{
    uint64_t pml4_phys = read_cr3();
    kern_pml4 = vmm_phys_to_hhdm(pml4_phys);

    // Pre-allocate all 256 higher-half PDPTs in kern_pml4 (indices 256..511)
    // so every future task's PML4 automatically shares all kernel heap mappings!
    for (int i = 256; i < 512; i++)
    {
        if (!(kern_pml4[i] & VMM_FLAG_PRESENT))
        {
            uint64_t pdpt_phys = pmm_alloc_frame();
            if (pdpt_phys != 0)
            {
                memset(vmm_phys_to_hhdm(pdpt_phys), 0, PAGE_SIZE);
                kern_pml4[i] = pte_set_addr(0, pdpt_phys) | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE;
            }
        }
    }

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
    if (head_ref == NULL)
    {
        return NULL;
    }
    VmAllocatedList *curr_node = *head_ref;
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
    uint64_t child_tbl_phys = pmm_alloc_frame();
    if (child_tbl_phys == 0)
    {
        return 0;
    }

    uint64_t *child_tbl_virt = (uint64_t *)vmm_phys_to_hhdm(child_tbl_phys);
    memset(child_tbl_virt, 0, PAGE_SIZE);

    int limit = (level == 4) ? 256 : 512;

    if (level > 1)
    {
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
            if (child_phys == 0)
            {
                vmm_free_table(child_tbl_virt, level);
                return 0;
            }
            child_tbl_virt[i] = child_phys | pte_get_flags(entry);
        }

        if (level == 4)
        {
            for (int16_t i = 256; i < 512; i++)
            {
                child_tbl_virt[i] = kern_pml4[i];
            }
        }
    }
    else
    {
        for (int16_t i = 0; i < 512; i++)
        {
            uint64_t entry = parent_tbl_virt[i];
            if ((entry & VMM_FLAG_PRESENT) == 0)
            {
                continue;
            }

            uint64_t parent_phys = pte_get_addr(entry);

            if ((entry & PTE_FLAG_SHARED) == 0)
            {
                parent_tbl_virt[i] &= ~VMM_FLAG_WRITABLE;
            }
            child_tbl_virt[i] = parent_tbl_virt[i];
            if ((entry & PTE_FLAG_SHARED) == 0)
            {
                pmm_inc_ref(parent_phys);
            }
        }
    }

    write_cr3(read_cr3());
    return child_tbl_phys;
}

VmAllocatedList *vmm_copy_alloc_list(VmAllocatedList *node)
{
    if (node == NULL)
    {
        return NULL;
    }

    VmAllocatedList *new_node = (VmAllocatedList *)kmalloc(sizeof(VmAllocatedList));
    if (new_node == NULL)
    {
        return NULL;
    }
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
    if (new_node == NULL)
    {
        return NULL;
    }
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

int vmm_handle_cow(uint64_t fault_addr)
{
    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(&vmm_lock, rflags);

    uint64_t *pml4 = vmm_phys_to_hhdm(pte_get_addr(read_cr3()));
    uint64_t *pte = vmm_walk_to_pte(pml4, fault_addr, 0);

    if (pte == NULL || !(*pte & VMM_FLAG_PRESENT) || !(*pte & VMM_FLAG_USER))
    {
        SPIN_UNLOCK_IRQRESTORE(&vmm_lock, rflags);
        return -1;
    }

    uint64_t old_phys = pte_get_addr(*pte);
    uint64_t *virt_addr = vmm_phys_to_hhdm(old_phys);
    uint32_t ref_count = pmm_get_ref_count(old_phys);

    if (ref_count <= 1)
    {
        *pte |= VMM_FLAG_WRITABLE;
    }
    else
    {
        uint64_t phys_addr = pmm_alloc_frame();
        if (phys_addr == 0)
        {
            SPIN_UNLOCK_IRQRESTORE(&vmm_lock, rflags);
            return -1;
        }

        void *hhdm_addr = (void *)vmm_phys_to_hhdm(phys_addr);
        memcpy(hhdm_addr, (void *)virt_addr, PAGE_SIZE);
        uint64_t flags = pte_get_flags(*pte) | VMM_FLAG_WRITABLE;
        *pte = phys_addr | (flags & 0xFFF);
        pmm_dec_ref(old_phys);
    }

    asm volatile("invlpg %0" : : "m"(*(char *)fault_addr) : "memory");
    SPIN_UNLOCK_IRQRESTORE(&vmm_lock, rflags);
    return 0;
}