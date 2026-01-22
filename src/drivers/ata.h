#ifndef ATA_H
#define ATA_H

#include <stdint.h>

void ata_wait_bsy(void);
void ata_wait_drq(void);
void ata_identify(void);
void ata_string_swap(char *dst, uint16_t *src, int len);
void ata_read_sectors(uint16_t *dst, uint32_t lba, uint8_t sec_count);
void ata_write_sectors(uint16_t *src, uint32_t lba, uint8_t sec_count);

#endif