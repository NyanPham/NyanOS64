#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>

#define VMM_FLAG_PRESENT 1
#define VMM_FLAG_WRITABLE (1 << 1)
#define VMM_FLAG_USER (1 << 2)

#define ENTRIES_NUM (4096 / sizeof(uint64_t))

#define PML4_INDEX 0x27
#define PDPT_INDEX 0x1e
#define PD_INDEX 0x15
#define PT_INDEX 0xc

typedef struct VmFreeRegion
{
    uint64_t addr;
    size_t size;
    struct VmFreeRegion* next;
} VmFreeRegion;

/**
 * @brief Create a new pml4 page for a Task
 * It copies the kernel's entries 256-512
 */
uint64_t vmm_new_pml4(void);

/**
 * @brief Return the allocated page back to kernel
 */
void vmm_ret_pml4(uint64_t pml4_phys);

void vmm_free_table(uint64_t* table, int level);

/**
 * @brief Set the physical address into the page table entry.
 * It preserves the last 12 bits.
 */
uint64_t pte_set_addr(uint64_t page_tab_entry, uint64_t phys_addr);

/**
 * @brief Get the physical address from the page table entry.
 * It drops the last 12 bits.
 */
uint64_t pte_get_addr(uint64_t page_tab_entry);

/**
 * @brief Get the physical address from the
 */
uint64_t vmm_virt2phys(uint64_t* pml4, uint64_t virt_addr);

/**
 * @brief Maps a physical page to a virtual page.
 * This is a high-level function that uses vmm_walk_to_pte to find the correct entry and sets it.
 */
void vmm_map_page(uint64_t* pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

/**
 * @brief Unmaps a virtual page.
 * This function clears the page table entry for a given virtual address, making it
 * no longer accessible. It also invalidates the TLB entry for that address to ensure
 * the change takes effect immediately.
 */
void vmm_unmap_page(uint64_t* pml4, uint64_t virt_addr);

/**
 * @brief Finds a virtual address
 * Traverses the linked list to find a node that is large enough for requested size,
 * then return the virtual address of the node.
 */
uint64_t find_free_addr(size_t size);

/**
 * @brief Allocates space in VM
 */
void* vmm_alloc(size_t size);

/**
 * @brief Free space in VM
 */
void vmm_free(void* ptr, size_t size);

/**
 * @brief Initializes the Virtual Memory Manager.
 * It gets the current page table from CR3 and runs a test to verify that
 * mapping and unmapping functionality is working correctly.
 */
void vmm_init();

#endif