#ifndef VMM_H
#define VMM_H

#include <stdint.h>

#define VMM_FLAG_PRESENT 1
#define VMM_FLAG_WRITABLE (1 << 1)
#define VMM_FLAG_USER (1 << 2)

#define ENTRIES_NUM (4096 / sizeof(uint64_t))

#define PML4_INDEX 0x27
#define PDPT_INDEX 0x1e
#define PD_INDEX 0x15
#define PT_INDEX 0xc

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
 * @brief Initializes the Virtual Memory Manager.
 * It gets the current page table from CR3 and runs a test to verify that
 * mapping and unmapping functionality is working correctly.
 */
void vmm_init();

#endif