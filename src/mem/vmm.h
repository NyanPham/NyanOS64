#ifndef VMM_H
#define VMM_H

#include <stdint.h>

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

void vmm_map_page(uint64_t* pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
void vmm_unmap_page(uint64_t* pml4, uint64_t virt_addr);
void vmm_init();

#endif