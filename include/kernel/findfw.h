#ifndef FIND_FIRMWARE_H
#define FIND_FIRMWARE_H

#include <kernel/types.h>

typedef enum {
    FW_UNKNOWN = 0,
    FW_BIOS,        // Legacy BIOS
    FW_UEFI,        // UEFI
    FW_UEFI_CSM,    // UEFI with CSM
} firmware_type_t;

typedef struct {
    firmware_type_t type;
    char vendor[64];      // AMI, Phoenix, Intel, etc
    char version[32];     // Version string
    char date[16];        // MM/DD/YYYY
} firmware_info_t;

// Основные функции
int find_firmware_init(void);
firmware_info_t* find_firmware_get_info(void);
const char* find_firmware_type_string(firmware_type_t type);

// Простые проверки
bool is_uefi_boot(void);
bool is_bios_boot(void);

// Вывод информации
void find_firmware_print_info(void);

#endif