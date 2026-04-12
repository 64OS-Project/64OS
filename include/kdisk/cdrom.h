#ifndef CDROM_H
#define CDROM_H

#include <kernel/types.h>
#include <kernel/blockdev.h>
#include <kdisk/ide.h>
#include <kdisk/ahci.h>

#define CDROM_SECTOR_SIZE 2048

typedef struct {
    blockdev_t *device;
    ide_disk_t *ide_disk;
    ahci_port_t *ahci_port;
    bool is_atapi;
    u32 sector_size;
    u64 total_sectors;
    char volume_label[33];
    bool initialized;
} cdrom_t;

int cdrom_init(blockdev_t *device);
int cdrom_read_sector(cdrom_t *cdrom, u32 lba, void *buffer);
int cdrom_read_sectors(cdrom_t *cdrom, u32 lba, u32 count, void *buffer);
cdrom_t* cdrom_find_and_init(void);
void cdrom_print_info(cdrom_t *cdrom);

#endif
