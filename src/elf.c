#include "elf.h"
#include "fs/tar.h"
#include "fs/vfs.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "mem/kmalloc.h"
#include "drivers/serial.h"
#include "./string.h"
#include "cpu.h"
#include "kern_defs.h"

#include <stddef.h>

extern uint64_t *kern_pml4;

uint64_t elf_load(const char *fname)
{
    Elf64_Ehdr elf_hdr;
    file_handle_t *file = vfs_open(fname, 0);
    if (file == NULL)
    {
        kprint("ELF: File not found: ");
        kprint(fname);
        kprint("\n");
        return 0;
    }

    vfs_read(file, sizeof(Elf64_Ehdr), (uint8_t *)&elf_hdr);

    if (elf_hdr.e_ident[0] != ELF_MAGIC0 ||
        elf_hdr.e_ident[1] != ELF_MAGIC1 ||
        elf_hdr.e_ident[2] != ELF_MAGIC2 ||
        elf_hdr.e_ident[3] != ELF_MAGIC3)
    {
        kprint("Error: Not a valid ELF file\n");
        vfs_close(file);
        return 0;
    }

    uint64_t curr_pml4_phys = read_cr3();
    uint64_t *curr_pml4_virt = vmm_phys_to_hhdm(curr_pml4_phys);

    uint16_t phdrs_size = elf_hdr.e_phnum * elf_hdr.e_phentsize;
    Elf64_Phdr *phdr = (Elf64_Phdr *)kmalloc(phdrs_size);
    if (phdr == NULL)
    {
        kprint("ELF: Out of memory for phdrs!\n");
        vfs_close(file);
        return 0;
    }

    vfs_seek(file, elf_hdr.e_phoff);
    vfs_read(file, phdrs_size, (uint8_t *)phdr);

    for (uint16_t i = 0; i < elf_hdr.e_phnum; i++)
    {
        if (phdr[i].p_type == PT_LOAD)
        {
            uint64_t mem_size = phdr[i].p_memsz;
            uint64_t file_size = phdr[i].p_filesz;
            uint64_t vaddr = phdr[i].p_vaddr;

            if (mem_size == 0)
            {
                continue;
            }

            uint64_t start_page = vaddr & ~(uint64_t)(PAGE_SIZE - 1);
            uint64_t end_page = (vaddr + mem_size + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);

            // 1. Allocate and map all pages in [start_page, end_page) if not already mapped
            for (uint64_t page_addr = start_page; page_addr < end_page; page_addr += PAGE_SIZE)
            {
                uint64_t existing_phys = vmm_virt2phys(curr_pml4_virt, page_addr);
                if (existing_phys == 0)
                {
                    uint64_t phys_addr = pmm_alloc_frame();
                    if (phys_addr == 0)
                    {
                        kprint("ELF: Out of memory!\n");
                        kfree(phdr);
                        vfs_close(file);
                        return 0;
                    }

                    void *loc_virt_addr = vmm_phys_to_hhdm(phys_addr);
                    memset(loc_virt_addr, 0, PAGE_SIZE);

                    vmm_map_page(
                        curr_pml4_virt,
                        page_addr,
                        phys_addr,
                        VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
                }
            }

            // 2. Copy file_size bytes to the exact virtual address (including page offset)
            uint64_t copied = 0;
            vfs_seek(file, phdr[i].p_offset);
            while (copied < file_size)
            {
                uint64_t curr_vaddr = vaddr + copied;
                uint64_t page_addr = curr_vaddr & ~(uint64_t)(PAGE_SIZE - 1);
                uint64_t page_off = curr_vaddr & (PAGE_SIZE - 1);

                uint64_t phys_addr = vmm_virt2phys(curr_pml4_virt, page_addr);
                uint8_t *dst_ptr = (uint8_t *)vmm_phys_to_hhdm(phys_addr) + page_off;

                uint64_t chunk = PAGE_SIZE - page_off;
                if (chunk > (file_size - copied))
                {
                    chunk = file_size - copied;
                }

                vfs_read(file, chunk, dst_ptr);
                copied += chunk;
            }
        }
    }

    kfree(phdr);
    vfs_close(file);
    kprint("ELF: Loaded successfully. Entry: ");
    kprint_hex_64(elf_hdr.e_entry);
    kprint("\n");

    return elf_hdr.e_entry;
}