#ifndef ATA_H
#define ATA_H

#include "fs/vfs.h"

#include <stdint.h>

typedef struct 
{   
    uint8_t boot_indicator; // 0x80 = bootable, 0x00 = nope
    uint8_t starting_chs[3]; // legacy, ignore
    uint8_t partition_type; // e.g. 0x83 is Linux
    uint8_t ending_chs[3]; // legacy, ignore
    uint32_t lba_start;
    uint32_t total_sectors;
} __attribute__((packed)) PartitionEntry;

void ata_wait_bsy(void);
void ata_wait_drq(void);
void ata_identify(void);
void ata_string_swap(char *dst, uint16_t *src, int len);
void ata_read_sectors(uint16_t *dst, uint32_t lba, uint8_t sec_count);
void ata_write_sectors(uint16_t *src, uint32_t lba, uint8_t sec_count);
void ata_fs_init(void);
void ata_probe_partitions(void);

extern vfs_fs_ops_t ata_ops;

#endif