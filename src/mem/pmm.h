#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct limine_memmap_response;
struct limine_hhdm_response;

void pmm_init(struct limine_memmap_response* memmap_resp, struct limine_hhdm_response* hhdm_resp);
void* pmm_alloc_frame();
void pmm_free_frame(void* frame_address);

#endif
