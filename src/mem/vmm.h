#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>

#define VMM_FLAG_PRESENT 1
#define VMM_FLAG_WRITABLE (1 << 1)
#define VMM_FLAG_USER (1 << 2)
#define VMM_FLAG_SHM (1 << 3)

#define ENTRIES_NUM (4096 / sizeof(uint64_t))

#define PML4_INDEX 0x27
#define PDPT_INDEX 0x1e
#define PD_INDEX 0x15
#define PT_INDEX 0xc

#define PTE_FLAG_SHARED (1 << 9)

struct Task;
extern uint64_t hhdm_offset;

typedef struct VmAllocatedList
{
    uint64_t addr;
    size_t size;
    uint32_t flags;
    struct VmAllocatedList *next;
} VmAllocatedList;

typedef struct VmFreeRegion
{
    uint64_t addr;
    size_t size;
    struct VmFreeRegion *next;
} VmFreeRegion;

uint64_t vmm_new_pml4(void);
void vmm_ret_pml4(uint64_t pml4_phys);
void vmm_free_table(uint64_t *table, int level);

uint64_t pte_set_addr(uint64_t page_tab_entry, uint64_t phys_addr);
uint64_t pte_get_addr(uint64_t page_tab_entry);
uint64_t pte_get_flags(uint64_t page_tab_entry);

uint64_t vmm_virt2phys(uint64_t *pml4, uint64_t virt_addr);
void vmm_map_page(uint64_t *pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
void vmm_unmap_page(uint64_t *pml4, uint64_t virt_addr);

uint64_t find_free_addr(VmFreeRegion **head_ref, size_t size);
void vmm_add_free_region(VmFreeRegion **head_ref, uint64_t addr, size_t size);
int8_t vmm_add_allocated_mem(VmAllocatedList **head_ref, uint64_t addr, size_t size, uint32_t flags);
VmAllocatedList *vmm_pop_allocated_mem(VmAllocatedList **head_ref, uint64_t addr);
VmAllocatedList *vmm_find_allocated_mem(VmAllocatedList **head_ref, uint64_t addr);

void *vmm_alloc(size_t size);
void vmm_free(void *ptr);
void *vmm_realloc(void *ptr, size_t new_size);
void *vmm_alloc_global(size_t size);
int vmm_unmap_user(struct Task *tsk, void *ptr);

uint64_t vmm_copy_hierarchy(uint64_t *parent_tbl_virt, int level);
void vmm_init(void);

VmAllocatedList *vmm_copy_alloc_list(VmAllocatedList *node);
VmFreeRegion *vmm_copy_free_list(VmFreeRegion *node);
void vmm_cleanup_task(struct Task *tsk);
int vmm_handle_cow(uint64_t fault_addr);

static inline void *vmm_phys_to_hhdm(uint64_t phys_addr)
{
    return (void *)(phys_addr | hhdm_offset);
}

static inline uint64_t vmm_hhdm_to_phys(void *virt_addr)
{
    return (uint64_t)virt_addr & (~hhdm_offset);
}

#endif