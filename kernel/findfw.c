#include <kernel/findfw.h>
#include <kernel/terminal.h>
#include <libk/string.h>
#include <mb.h>

static firmware_info_t g_firmware = {0};
static bool g_initialized = false;

int find_firmware_init(void) {
    if (g_initialized) return 0;
    
    memset(&g_firmware, 0, sizeof(firmware_info_t));
    
    extern multiboot2_info_t mbinfo;
    
    // Проверка UEFI через Multiboot2
    if (mbinfo.efi.system_table64 != 0) {
        g_firmware.type = FW_UEFI;
        strcpy(g_firmware.vendor, "UEFI");
        strcpy(g_firmware.version, "2.x");
        
        // Проверка CSM (если есть)
        if (mbinfo.efi.system_table32 != 0) {
            g_firmware.type = FW_UEFI_CSM;
        }
    } 
    // Иначе Legacy BIOS
    else {
        g_firmware.type = FW_BIOS;
        
        // Пытаемся найти производителя BIOS в памяти
        const char *bios_signatures[] = {
            "AMERICAN MEGATRENDS", "AMI",
            "PHOENIX", "AWARD", "INSYDE", "Intel"
        };
        
        u8 *bios_ptr = (u8*)0xF0000;
        for (int i = 0; i < sizeof(bios_signatures)/sizeof(bios_signatures[0]); i++) {
            if (memmem(bios_ptr, 0x10000, bios_signatures[i], strlen(bios_signatures[i]))) {
                strncpy(g_firmware.vendor, bios_signatures[i], sizeof(g_firmware.vendor) - 1);
                break;
            }
        }
        
        // Дата BIOS (обычно по адресу 0xFFFF5)
        u8 *date_ptr = (u8*)0xFFFF5;
        if (*date_ptr) {
            memcpy(g_firmware.date, date_ptr, 8);
            g_firmware.date[8] = '\0';
        }
        
        // Версия (поищем в ROM)
        if (!g_firmware.version[0]) {
            strcpy(g_firmware.version, "Legacy");
        }
    }
    
    g_initialized = true;
    return 0;
}

firmware_info_t* find_firmware_get_info(void) {
    if (!g_initialized) find_firmware_init();
    return &g_firmware;
}

const char* find_firmware_type_string(firmware_type_t type) {
    switch (type) {
        case FW_BIOS:      return "Legacy BIOS";
        case FW_UEFI:      return "UEFI";
        case FW_UEFI_CSM:  return "UEFI (CSM mode)";
        default:           return "Unknown";
    }
}

bool is_uefi_boot(void) {
    if (!g_initialized) find_firmware_init();
    return (g_firmware.type == FW_UEFI || g_firmware.type == FW_UEFI_CSM);
}

bool is_bios_boot(void) {
    if (!g_initialized) find_firmware_init();
    return (g_firmware.type == FW_BIOS);
}

void find_firmware_print_info(void) {
    if (!g_initialized) find_firmware_init();
    terminal_printf("Type:    %s\n", find_firmware_type_string(g_firmware.type));
    
    if (g_firmware.vendor[0]) {
        terminal_printf("Vendor:  %s\n", g_firmware.vendor);
    }
    if (g_firmware.date[0]) {
        terminal_printf("Date:    %s\n", g_firmware.date);
    }
    if (g_firmware.version[0]) {
        terminal_printf("Version: %s\n", g_firmware.version);
    }
    
    terminal_printf("Boot:    %s\n", is_uefi_boot() ? "UEFI" : "Legacy BIOS");
}