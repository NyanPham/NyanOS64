#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>

typedef struct
{
    // header
    uint8_t boot_jump[3]; // padding
    char oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fats_num;

    // legacy of FAT12/16
    uint16_t root_entry_count; // for FAT32, it's always 0
    uint16_t total_sectors_16; // for FAT32, it's usually 0
    uint8_t media_type;
    uint16_t fat_size_16; // FAT32 uses fat_size_32
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32; //

    // FAT32 extension
    uint32_t sectors_per_fat32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
} __attribute__((packed)) fat32_bpb;

typedef struct
{
    char name[8]; // non null-terminated
    char ext[3];  // non null-terminated
    uint8_t attributes;
    uint8_t reserved_nt;     // for Windows NT, usually is 0
    uint8_t creation_tenths; // milli from the creation time
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) DirectoryEntry; // 32 bytes in total

typedef struct
{
    const char *name;
    const char *ext;
    DirectoryEntry *out_entry;
} find_control_t;

typedef int (*fat32_entry_cb_t)(DirectoryEntry *, void *);

void fat32_init(uint32_t partition_lba, uint8_t drive_sel);
void fat32_list_root(void);
int fat32_parse_name(const char *fname, char *out_name, char *out_ext);
int fat32_find_file(const char *name, DirectoryEntry *out_entry);
int fat32_iterate_root(fat32_entry_cb_t cb, void *ctx);
uint32_t fat32_read_fat(uint32_t cluster);

#endif