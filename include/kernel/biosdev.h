#ifndef BIOSDEV_H
#define BIOSDEV_H

#include <kernel/types.h>

//Boot device types
typedef enum {
    BOOT_DEVICE_UNKNOWN = 0,
    BOOT_DEVICE_FLOPPY,
    BOOT_DEVICE_HDD,
    BOOT_DEVICE_CDROM,
    BOOT_DEVICE_NETWORK,
    BOOT_DEVICE_REMOVABLE
} boot_device_type_t;

//Boot device information
typedef struct {
    u8 bios_number;              //Original number (0x80, 0xE0, etc.)
    boot_device_type_t type;     //Device type
    u8 device_number;            //Device number (0, 1, 2...)
    bool is_removable;           //Removable?
    bool is_valid;               //Is the information valid?
    char name[32];               //Human readable name
} boot_device_info_t;

//Functions
void biosdev_parse(u8 bios_number, boot_device_info_t *info);
const char* biosdev_type_to_string(boot_device_type_t type);
void biosdev_print_info(boot_device_info_t *info);

#endif
