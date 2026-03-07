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
        return 0;
    }

    uint64_t curr_pml4_phys = read_cr3();
    uint64_t *curr_pml4_virt = vmm_phys_to_hhdm(curr_pml4_phys);

    uint16_t phdrs_size = elf_hdr.e_phnum * elf_hdr.e_phentsize;
    Elf64_Phdr *phdr = (Elf64_Phdr *)vmm_alloc(phdrs_size);
    vfs_seek(file, elf_hdr.e_phoff);
    vfs_read(file, phdrs_size, (uint8_t *)phdr);

    for (uint16_t i = 0; i < elf_hdr.e_phnum; i++)
    {
        if (phdr[i].p_type == PT_LOAD)
        {
            uint64_t mem_size = phdr[i].p_memsz;
            uint64_t file_size = phdr[i].p_filesz;
            uint64_t vaddr = phdr[i].p_vaddr;
            // uint64_t offset = phdr[i].p_offset;

            uint64_t npages = (mem_size + PAGE_SIZE - 1) / PAGE_SIZE;
            for (size_t j = 0; j < npages; j++)
            {
                uint64_t phys_addr = pmm_alloc_frame();
                if (phys_addr == 0)
                {
                    kprint("ELF: Out of memory!\n");
                    return 0;
                }

                void *loc_virt_addr = (void *)vmm_phys_to_hhdm(phys_addr);
                memset(loc_virt_addr, 0, PAGE_SIZE);

                uint64_t targt_addr = vaddr + (j * PAGE_SIZE);

                vmm_map_page(
                    curr_pml4_virt,
                    targt_addr,
                    phys_addr,
                    VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);

                uint64_t page_offset = j * PAGE_SIZE;

                if (page_offset < file_size)
                {
                    uint64_t remaining_bytes = file_size - page_offset;
                    uint64_t bytes_to_cpy = remaining_bytes > PAGE_SIZE ? PAGE_SIZE : remaining_bytes;

                    if (bytes_to_cpy > 0)
                    {
                        vfs_seek(file, phdr[i].p_offset + page_offset);
                        uint8_t *tmp_buf = (uint8_t *)vmm_alloc(bytes_to_cpy);
                        vfs_read(file, bytes_to_cpy, tmp_buf);
                        memcpy(loc_virt_addr, tmp_buf, bytes_to_cpy);
                        vmm_free(tmp_buf);
                    }

                    if (bytes_to_cpy < PAGE_SIZE)
                    {
                        memset((uint8_t *)loc_virt_addr + bytes_to_cpy, 0, PAGE_SIZE - bytes_to_cpy);
                    }
                }

                // kprint("Loaded segment at ");
                // kprint_hex_64(vaddr);
                // kprint("\n");
            }
        }
    }

    vmm_free(phdr);
    vfs_close(file);
    kprint("ELF: Loaded successfully. Entry: ");
    kprint_hex_64(elf_hdr.e_entry);
    kprint("\n");

    return elf_hdr.e_entry;
}