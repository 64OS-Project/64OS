#ifndef IDE_H
#define IDE_H

#include <kernel/types.h>
#include <libk/string.h>
#include <pci.h>

/*
 * Basic ports (compatibility mode)
 */
#define IDE_BASE_PRIMARY 0x1F0
#define IDE_CTRL_PRIMARY 0x3F6
#define IDE_BASE_SECONDARY 0x170
#define IDE_CTRL_SECONDARY 0x376

/*
 * Register offsets relative to base/ctrl
 */
#define IDE_DATA 0x00    /*
 * word I/O
 */
#define IDE_ERROR 0x01   /*
 * reading
 */
#define IDE_FEATURE 0x01 /*
 * record
 */
#define IDE_NSECT 0x02
#define IDE_SECTOR 0x03  /*
 * LBA0
 */
#define IDE_LCYL 0x04    /*
 * LBA1
 */
#define IDE_HCYL 0x05    /*
 * LBA2
 */
#define IDE_SELECT 0x06  /*
 * Drive/Head
 */
#define IDE_STATUS 0x07  /*
 * reading
 */
#define IDE_COMMAND 0x07 /*
 * record
 */

/*
 * ctrl port
 */
#define IDE_ALTSTATUS 0x0 /*
 * reading
 */
#define IDE_CONTROL 0x0   /*
 * record
 */

/*
 * Status bits
 */
#define IDE_STATUS_BSY 0x80
#define IDE_STATUS_DRDY 0x40
#define IDE_STATUS_DRQ 0x08
#define IDE_STATUS_ERR 0x01
#define IDE_STATUS_DF 0x20

/*
 * Teams
 */
#define IDE_CMD_READ_SECTORS 0x20
#define IDE_CMD_WRITE_SECTORS 0x30
#define IDE_CMD_IDENTIFY 0xEC
#define IDE_CMD_IDENTIFY_PACKET 0xA1
#define IDE_CMD_READ_SECTORS_EXT 0x24
#define IDE_CMD_WRITE_SECTORS_EXT 0x34
#define IDE_CMD_CACHE_FLUSH 0xE7
#define IDE_CMD_CACHE_FLUSH_EXT 0xEA

/*
 * Timeout (poll iterations)
 */
#define IDE_TIMEOUT_LOOPS 100000U

/*
 * Return codes
 */
#define IDE_OK 0
#define IDE_ERR_TIMEOUT -1
#define IDE_ERR_DEVICE -2
#define IDE_ERR_INVALID -3

typedef enum
{
    IDE_CHANNEL_PRIMARY = 0,
    IDE_CHANNEL_SECONDARY = 1
} ide_channel_t;

typedef enum
{
    IDE_TYPE_NONE = 0,
    IDE_TYPE_ATA,
    IDE_TYPE_ATAPI
} ide_dev_type_t;

typedef struct
{
    u16 base_port; /*
 * 0x1F0 / 0x170
 */
    u16 ctrl_port; /*
 * 0x3F6 / 0x376
 */
    u8 drive;      /*
 * 0 = master, 1 = slave
 */
    ide_channel_t channel;
    ide_dev_type_t type;    /*
 * ATA / ATAPI / NONE
 */
    u64 total_sectors; /*
 * number of sectors (512B)
 */
    u16 sector_size;   /*
 * sector size (usually 512)
 */
    int supports_lba48;     /*
 * support LBA48 (bool)
 */
    volatile u8 irq_pending;
    volatile u8 irq_count;
    u8 irq;  // real IRQ from PCI
    pci_device_t* pci_dev;  //pointer to PCI device
} ide_disk_t;

int ide_init(ide_disk_t *disk, ide_channel_t channel, u8 drive);
int ide_identify(ide_disk_t *disk, u16 ident_buffer[256]);
int ide_read_sectors(ide_disk_t *disk, u64 lba, u32 count, void *buffer);
int ide_write_sectors(ide_disk_t *disk, u64 lba, u32 count, const void *buffer);

void ide_primary_irq_handler(void);
void ide_secondary_irq_handler(void);
void ide_enable_interrupts(u16 ctrl_port);
void ide_disable_interrupts(u16 ctrl_port);
int ide_wait_irq(ide_disk_t *disk, u32 timeout_ms);
u8 ide_get_irq_count(ide_disk_t *disk);
#endif
