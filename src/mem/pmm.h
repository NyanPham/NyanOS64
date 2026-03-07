#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct limine_memmap_response;
struct limine_hhdm_response;

void pmm_init(struct limine_memmap_response *memmap_resp, struct limine_hhdm_response *hhdm_resp);
uint64_t pmm_alloc_frame(void);
void pmm_free_frame(uint64_t phys_addr);
void pmm_inc_ref(uint64_t frame_addr);
void pmm_dec_ref(uint64_t frame_addr);
uint32_t pmm_get_ref_count(uint64_t frame_addr);

#endif
