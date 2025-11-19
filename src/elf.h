#ifndef ELF_H
#define ELF_H

#include <stdint.h>

// all elf files start their binaries in 0x7F"ELF"
#define ELF_MAGIC0 0x7F
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'

// Segment type
#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_SHLIB 5
#define PT_PHDR 6

// access flags
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

typedef struct
{
    unsigned char e_ident[0x10];    // the first 16 bytes for magic numbers 
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;   // entry point address
    uint64_t e_phoff;   // offset of Program Header
    uint64_t e_shoff;   // offset of Section Header
    uint32_t e_flags;
    uint16_t e_ehsize;  // Header size
    uint16_t e_phentsize;   // Program Header entry size
    uint16_t e_phnum; // num of PH entries
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf64_Ehdr;

typedef struct
{
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr; 
    uint64_t p_paddr;   // phys addr is not really used in our OS's paging
    uint64_t p_filesz;
    uint64_t p_memsz;    // normally >= p_filesz
    uint64_t p_align; 
} __attribute((packed)) Elf64_Phdr;

#endif