#include "ata.h"
#include "../io.h"
#include "./serial.h"
#include <stdint.h>

#define ATA_PRIMARY_IO 0x1F0
#define ATA_PRIMARY_CTRL 0x3F6

#define ATA_REG_DATA 0x00
#define ATA_REG_SECCOUNT 0x02
#define ATA_REG_LBA_LO 0x03
#define ATA_REG_LBA_MID 0x04
#define ATA_REG_LBA_HI 0x05
#define ATA_REG_DRIVE 0x06
#define ATA_REG_COMMAND 0x07
#define ATA_REG_STATUS 0x07

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_SR_BSY 0x80 // busy?
#define ATA_SR_DRQ 0x08 // data request ready
#define ATA_SR_ERR 0x01 // error

void ata_wait_bsy()
{
    while (inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_BSY)
    {
        ;
    }
}

void ata_wait_drq()
{
    while (!(inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_DRQ))
    {
        ;
    }
}

void ata_identify()
{
    // select the Master drive
    // by sending 0xE0 to port 0x1F6
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xE0);

    // set other ports to 0
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI, 0);

    // send IDENTIFY (0xEC) to the Command port (0x1F7)
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t stat = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (stat == 0)
    {
        kprint("ATA: No drive found.\n");
        return;
    }

    // wait 
    ata_wait_bsy();
    ata_wait_drq();

    // finally, read the 256 words (512 bytes)
    uint16_t dst[256];
    for (int i = 0; i < 256; i++)
    {
        dst[i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    }

    kprint("ATA: Drive Identified Successfully!\n");

    char model[41];
    ata_string_swap(model, dst + 27, 20);

    kprint("Model: ");
    kprint(model);
    kprint("\n");
}

void ata_string_swap(char *dst, uint16_t *src, int len)
{
    for (int i = 0; i < len; i++)
    {
        uint16_t tmp = src[i];
        *dst++ = (tmp >> 8) & 0xFF;
        *dst++ = tmp & 0xFF;
    }
    *dst = '\0';
}

void ata_read_sectors(uint16_t *dst, uint32_t lba, uint8_t sec_count)
{
    // split the lba (28-bit) to the 4 ports
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO, lba & 0xFF);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, ((lba >> 24) & 0x0F) | 0xE0);

    // send the sec_count to the sector count port
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, sec_count);

    // send the 0x20 command to read the sec count
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, 0x20);
    
    // because each sector is done being read
    // it sets the drq, and clears the bsy to continue
    // reading the next sector, we need to wait 
    // in a loop
    for (int i = 0; i < sec_count; i++)
    {
        ata_wait_bsy();
        ata_wait_drq();
        
        for (int j = 0; j < 256; j++)
        {
            dst[i * 256 + j] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
        }
    }
}

void ata_write_sectors(uint16_t *src, uint32_t lba, uint8_t sec_count)
{
    // split the lba (28-bit) to the 4 ports
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO, lba & 0xFF);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, ((lba >> 24) & 0x0F) | 0xE0);

    // send the sec_count to the sector count port
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, sec_count);

    // send the 0x30 command to write the sec count
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, 0x30);
    
    // because each sector is done being written to
    // it sets the drq, and clears the bsy to continue
    // writing to the next sector, we need to wait 
    // in a loop
    for (int i = 0; i < sec_count; i++)
    {
        ata_wait_bsy();
        ata_wait_drq();
        
        for (int j = 0; j < 256; j++)
        {
            outw(ATA_PRIMARY_IO + ATA_REG_DATA, src[i * 256 + j]);
        }
    }
}

/*
| Port  | Read Mode      | Write Mode        | Description                                          |
|-------|----------------|-------------------|------------------------------------------------------|
| 0x1F0 | Data Register  | Data Register     | a port to read from or write to                      |
| 0x1F1 | Error Register | Features Register | to read error or to                                  |
| 0x1F2 | Sector Count   | Sector Count      | number of sectors to read or write                   |
| 0x1F3 | LBA Low        | LBA Low           | sector address low                                   |
| 0x1F4 | LBA Mid        | LBA Mid           | sector address mid                                   |
| 0x1F5 | LBA High       | LBA High          | sector address high                                  |
| 0x1F6 | Drive/Head     | Drive/Head        | select master or slave, and LBA mode                 |
| 0x1F7 | Status         | Command           | read status (busy or ready) or send command          |

| Bit 7    | Bit 6                       | Bit 5    | Bit 4 (DRV)           | Bits 3-0                                  |
|----------|-----------------------------|----------|-----------------------|-------------------------------------------|
| Always 1 | LBA Mode (1 = LBA, 0 = CHS) | Always 1 | 0 = Master, 1 = Slave | Highest 4 bits of LBA address (usually 0) |
*/