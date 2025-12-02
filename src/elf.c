#include "elf.h"
#include "fs/tar.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "mem/kmalloc.h"
#include "drivers/serial.h"
#include "./string.h"

#include <stddef.h>

extern uint64_t hhdm_offset;
extern uint64_t* kern_pml4;
extern void *memset(void *s, int c, size_t n);
void *memcpy(void *restrict dest, const void *restrict src, size_t n);

uint64_t elf_load(const char* fname)
{
    char* file_addr = tar_read_file(fname);
    if (file_addr == NULL)
    {
        kprint("ELF: File not found: ");
        kprint(fname);
        kprint("\n");
        return 0;
    }

    Elf64_Ehdr* elf_hdr = (Elf64_Ehdr*)(file_addr);
    if (elf_hdr->e_ident[0] != ELF_MAGIC0 || 
        elf_hdr->e_ident[1] != ELF_MAGIC1 ||
        elf_hdr->e_ident[2] != ELF_MAGIC2 ||
        elf_hdr->e_ident[3] != ELF_MAGIC3)
    {
        kprint("Error: Not a valid ELF file\n");
        return 0;
    }

    Elf64_Phdr* phdr = (Elf64_Phdr*)((uint8_t*)file_addr + elf_hdr->e_phoff);
    
    for (uint16_t i = 0; i < elf_hdr->e_phnum; i++)
    {
        if (phdr[i].p_type == PT_LOAD)
        {
            uint64_t mem_size = phdr[i].p_memsz;
            uint64_t file_size = phdr[i].p_filesz;
            uint64_t vaddr = phdr[i].p_vaddr;
            uint64_t offset = phdr[i].p_offset;

            uint64_t npages = (mem_size + PAGE_SIZE - 1) / PAGE_SIZE;
            for (size_t j = 0; j < npages; j++)
            {
                void* loc_virt_addr = pmm_alloc_frame();
                if (loc_virt_addr == NULL)
                {
                    kprint("ELF: Out of memory!\n");
                    return 0;
                }

                memset(loc_virt_addr, 0, PAGE_SIZE);

                uint64_t phys_addr = (uint64_t)loc_virt_addr - hhdm_offset;
                uint64_t targt_addr = vaddr + (j * PAGE_SIZE);

                vmm_map_page(
                    kern_pml4,
                    targt_addr,
                    phys_addr,
                    VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER
                );

                uint64_t page_offset = j * PAGE_SIZE;

                if (page_offset < file_size)
                {
                    uint64_t remaining_bytes = file_size - page_offset;
                    uint64_t bytes_to_cpy = remaining_bytes > PAGE_SIZE ? PAGE_SIZE : remaining_bytes;

                    if (bytes_to_cpy > 0)
                    {
                        void* src = (void*)((uint8_t*)file_addr + offset + page_offset);
                        memcpy(loc_virt_addr, src, bytes_to_cpy);
                    }
                    
                    if (bytes_to_cpy < PAGE_SIZE)
                    {
                        memset((uint8_t*)loc_virt_addr + bytes_to_cpy, 0, PAGE_SIZE - bytes_to_cpy);
                    }
                }

                // kprint("Loaded segment at ");
                // kprint_hex_64(vaddr);
                // kprint("\n");
            }
        }
    }

    kprint("ELF: Loaded successfully. Entry: ");
    kprint_hex_64(elf_hdr->e_entry);
    kprint("\n");

    return elf_hdr->e_entry;
}