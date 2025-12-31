#ifndef TAR_H
#define TAR_H

#include "stdbool.h"
#include "stdint.h"
 
typedef struct tar_header
{
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} __attribute__((packed)) tar_header;

void tar_list(char* list, uint64_t max_len);
char* tar_read_file(const char* fname);
void tar_init(void* tar_addr);

#endif