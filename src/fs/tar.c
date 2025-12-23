#include "tar.h"
#include "../string.h"
#include "drivers/serial.h"
#include "drivers/video.h"
#include "mem/kmalloc.h"
#include "kern_defs.h"

#include <stddef.h>

static void *g_tar_addr = NULL;

uint64_t oct2bin(const char *str, int size)
{
    uint64_t n = 0;
    for (int i = 0; i < size; i++)
    {
        if (str[i] < '0' || str[i] > '7')
        {
            break;
        }

        n = n * 8 + (str[i] - '0');
    }

    return n;
}

bool tar_validate(tar_header *header)
{
    if (header->magic[0] != 'u' ||
        header->magic[1] != 's' ||
        header->magic[2] != 't' ||
        header->magic[3] != 'a' ||
        header->magic[4] != 'r')
    {
        return false;
    }
    return true;
}

char* tar_read_file(const char* fname)
{
    if (g_tar_addr == NULL) 
    {
        return NULL;
    }

    tar_header* hdr = (tar_header*)g_tar_addr;

    const char* search_name = fname;
    if (*search_name == '/')
    {
        search_name++;
    }

    while (1)
    {
        if (!tar_validate(hdr))
        {
            break;
        }

        if (strcmp(hdr->name, search_name) == 0)
        {
            return (char*)((uint64_t)hdr + 512);
        }

        uint64_t size = oct2bin(hdr->size, 11);
        uint64_t size_aligned = (size + 511) / 512 * 512;
        uint64_t next_hdr_addr = (uint64_t)hdr + 512 + size_aligned;
        hdr = (tar_header*)next_hdr_addr;
    }

    return NULL;
}

void tar_list()
{
    if (g_tar_addr == NULL)
    {
        video_write("Error: No File System found!\n", 0xFF0000);
        return;
    }

    tar_header *hdr = (tar_header *)g_tar_addr;

    while (1)
    {
        if (!tar_validate(hdr))
        {
            kprint("End of TAR or Invalid Header.\n");
            break;
        }

        uint64_t size = oct2bin(hdr->size, 11);

        video_write("- ", 0x00FF00);
        video_write(hdr->name, White);
        video_write("\n", 0);

        uint64_t size_aligned = (size + 511) / 512 * 512;
        uint64_t next_hdr_addr = (uint64_t)hdr + 512 + size_aligned;
        hdr = (tar_header *)next_hdr_addr;
    }
}

void tar_init(void *tar_addr)
{
    g_tar_addr = tar_addr;
    kprint("TAR FS initialized at: ");
    kprint_hex_64((uint64_t)g_tar_addr);
    kprint("\n");
}