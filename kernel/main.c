#include <kernel/types.h>
#include <asm/cpu.h>
#include <kernel/panic.h>
#include <stdarg.h>
#include <mbstruct.h>
#include <mb.h>
#include <asm/io.h>
#include <fb.h>
#include <apic.h>
#include <ioapic.h>
#include <acpi.h>
#include <idt.h>
#include <ktime/clock.h>
#include <kernel/timer.h>
#include <ps2kbd.h>
#include <asm/mmio.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <kernel/paging.h>
#include <libk/string.h>
#include <kernel/driver.h>
#include <kernel/terminal.h>
#include <kdisk/ahci.h>
#include <kdisk/ide.h>
#include <pci.h>
#include <kernel/blockdev.h>
#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/exfat.h>
#include <fs/fat32.h>
#include <kernel/sched.h>
#include <kernel/findroot.h>
#include <kdisk/cdrom.h>
#include <kernel/biosdev.h>
#include <kernel/partition.h>
#include <kernel/fileio.h>
#include <fs/procfs.h>
#include <kernel/smp.h>
#include <kernel/syscall.h>
#include <crypto/api.h>
#include <net/net.h>
#include <net/dhcp.h>

multiboot2_info_t mbinfo;
pmm_t pmm;
static u64 current_acpi_tbl = 0;
acpi_t *acpi;
ide_disk_t disks[4];
vfs_inode_t *fs_root = NULL;
u8 bios_number = 0;
static dhcp_client_t g_dhcp_client;

extern driver_t g_fb_driver;
extern driver_t g_acpi_driver;
extern driver_t g_lapic_driver;
extern driver_t g_ioapic_driver;
extern driver_t g_ps2_kbd_driver;
extern driver_t g_pci_driver;
extern driver_t g_cdrom_driver;
extern driver_t g_ide_driver;
extern driver_t g_ahci_driver;
extern driver_t g_rtl8139_driver;
extern char _heap_start, _heap_end;

static void dhcp_global_callback(dhcp_client_t *client, bool success) {
    if (success) {
        char ip_str[16];
        ipv4_ntop((ipv4_addr_t){ .addr = client->yiaddr }, ip_str, 16);
        terminal_success_printf("[NET] %s configured: %s\n", client->dev->name, ip_str);
    } else {
        terminal_error_printf("[NET] %s DHCP failed\n", client->dev->name);
    }
}

static void crypto_init(void) {
    crypto_rng_init();

    if (crypto_self_test() == CRYPTO_OK) {
        terminal_success_printf("Crypto: All tests passed\n");
    } else {
        terminal_error_printf("Crypto: Self-test FAILED!\n");
        //We can continue, but it is better not to trust cryptography
    }
}

static void drivers_register(void) {
    driver_subsystem_init();

    driver_register(&g_fb_driver);
    driver_register(&g_acpi_driver);
    driver_register(&g_lapic_driver);
    driver_register(&g_ioapic_driver);
    driver_register(&g_ps2_kbd_driver);
    driver_register(&g_pci_driver);
    driver_register(&g_ide_driver);
    driver_register(&g_ahci_driver);
    driver_register(&g_cdrom_driver);
    driver_register(&g_rtl8139_driver);
}

static void memory_init(u64 mb2_addr) {
    pmm_init(&pmm, mb2_addr);
    paging_init(multiboot2_get_total_memory(&mbinfo));
    u64 heap_start = (u64)&_heap_start;
    u64 totalmem = multiboot2_get_total_memory(&mbinfo);
    u64 reserved = 1024 * 1024;  //1MB reserve
    u64 heap_end = totalmem - reserved;
    sz heap_size = (sz)(heap_end - heap_start);
    if (heap_size > 0 && heap_size < 512 * 1024 * 1024) {
    	malloc_init((void*)heap_start, heap_size);
    } else {
    	heap_size = (sz)((u64)&_heap_end - (u64)&_heap_start);
    	malloc_init((void*)&_heap_start, heap_size);
    }
}

static void fs_init(void) {
    vfs_init();
    exfat_init();
    fat32_init();
    devfs_init();
    procfs_init();
}

void kmain(u64 mb2_addr) {
    outb(0xA1, 0xFF);
    outb(0x21, 0xFF);

    
    idt_install();

    multiboot2_init_info(&mbinfo);
    if (multiboot2_parse((void*)(uptr)mb2_addr, &mbinfo) != 0) {
        for (;;) { halt(); }
    }

    drivers_register();

    driver_t *fb_drv = driver_find_by_name("framebuffer");
    driver_t *acpi_drv = driver_find_by_name("acpi");
    driver_t *lapic_drv = driver_find_by_name("lapic");
    driver_t *ioapic_drv = driver_find_by_name("ioapic");
    driver_t *ps2_kbd_drv = driver_find_by_name("ps2_kbd");
    driver_t *pci_drv = driver_find_by_name("pci");
    driver_t *ide_drv = driver_find_by_name("ide");
    driver_t *ahci_drv = driver_find_by_name("ahci");
    driver_t *rtl8139_drv = driver_find_by_name("rtl8139");

    memory_init(mb2_addr);

    if (fb_drv && fb_drv->probe && fb_drv->probe(fb_drv) == 0) {
        if (mbinfo.framebuffer.addr && mbinfo.framebuffer.width && 
            mbinfo.framebuffer.height && mbinfo.framebuffer.pitch && 
            mbinfo.framebuffer.bpp) {
            
            framebuffer_init(
                mbinfo.framebuffer.addr,
                mbinfo.framebuffer.width,
                mbinfo.framebuffer.height,
                mbinfo.framebuffer.pitch,
                mbinfo.framebuffer.bpp
            );

            fb_drv->initialized = true;
        } else {
            for (;;) { halt(); }
        }
    } else {
        for (;;) { halt(); }
    }

    fb_clear(FB_BLACK);
    terminal_init();

    terminal_printf("Loading kernel...\n");
    if (mbinfo.boot_loader_name) {
        terminal_printf("Bootloader: %s\n", mbinfo.boot_loader_name);
    }
    terminal_printf("Load address: 0x%llx\n", mbinfo.load_base_addr);
    terminal_printf("BIOSDEV: 0x%x\n", mbinfo.bootdev.biosdev);
    u8 bios_number = (u8)(mbinfo.bootdev.biosdev & 0xFF);
    if (mbinfo.bootdev.partition == 4294967295) {
        terminal_printf("Partition: No\n");
    } else {
        terminal_printf("Partition: %u\n", mbinfo.bootdev.partition);
    }
    if (mbinfo.bootdev.sub_partition == 4294967295) {
        terminal_printf("Sub-partition: No\n");
    } else {
        terminal_printf("Sub-partition: %u\n", mbinfo.bootdev.sub_partition);
    }
    char *cmdline = get_cmdline();
    if (cmdline) {
        terminal_printf("Kernel cmdline: %s\n", cmdline);
    } else {
        terminal_printf("No cmdline found\n");
    }
    if (acpi_drv && acpi_drv->probe && acpi_drv->probe(acpi_drv) == 0) {
        if (mbinfo.acpi.rsdpv2_addr) {
            current_acpi_tbl = mbinfo.acpi.rsdpv2_addr;
        } else if (mbinfo.acpi.rsdpv1_addr) {
            current_acpi_tbl = mbinfo.acpi.rsdpv1_addr;
        } else {
            panic("NO_ACPI_TABLE");
        }

        if (acpi_init(current_acpi_tbl)) {
            terminal_success_printf("ACPI OK\n");
            acpi_drv->initialized = true;
            acpi = acpi_get_table();
        } else {
            panic("ACPI_INIT_FAILED");
        }
    } else {
        panic("NO_ACPI_DRIVER");
    }

    if (lapic_drv && lapic_drv->probe && lapic_drv->probe(lapic_drv) == 0) {
        if (lapic_drv->init && lapic_drv->init(lapic_drv) == 0) {
            lapic_drv->initialized = true;
            terminal_success_printf("LAPIC OK\n");
        } else {
            panic("LAPIC_INIT_FAILED");
        }
    } else {
        panic("NO_LAPIC_DRIVER");
    }

    if (ioapic_drv && ioapic_drv->probe && ioapic_drv->probe(ioapic_drv) == 0) {
        if (ioapic_drv->init && ioapic_drv->init(ioapic_drv) == 0) {
            ioapic_drv->initialized = true;
            terminal_success_printf("I/O APIC OK\n");
            if (ioapic_process_overrides()) {
                terminal_info_printf("IRQ overrides processed\n");
            } else {
                terminal_warn_printf("IRQ overrides process failed\n");
            }
            
            ioapic_mask_all();
        } else {
            panic("IOAPIC_INIT_FAILED");
        }
    } else {
        panic("NO_IOAPIC_FOUND");
    }

    smp_init();

    if (ps2_kbd_drv && ps2_kbd_drv->probe && ps2_kbd_drv->probe(ps2_kbd_drv) == 0) {
        ps2_kbd_drv->initialized = true;
            
        u32 gsi, flags;
        if (!ioapic_get_override(1, &gsi, &flags)) {
            gsi = 1;
            flags = IOAPIC_FLAG_EDGE_TRIGGERED | IOAPIC_FLAG_ACTIVE_HIGH;
        }

        ioapic_redirect_irq(gsi, 33, apic_get_id(), flags);
        ioapic_unmask_irq(gsi);
    } else {
        terminal_warn_printf("No PS/2 keyboard found\n");
    }

    terminal_info_printf("Calibrating APIC timer  ");
    init_system_clock();
    timer_apic_init(1000);
    terminal_success_printf("Done\n");

    scheduler_init();

    syscall_init();

    if (pci_drv && pci_drv->probe && pci_drv->probe(pci_drv) == 0) {
        pci_init();
        pci_drv->initialized = true;
        terminal_success_printf("PCI OK");
    } else {
        panic("NO PCI");
    }

    if (ide_drv && ide_drv->probe && ide_drv->probe(ide_drv) == 0) {
        for (int ch = 0; ch < 2; ch++) {
            for (int dr = 0; dr < 2; dr++) {
                int idx = ch * 2 + dr;

                terminal_printf("Initializing IDE %s/%s...\n",
                        ch == 0 ? "Primary" : "Secondary",
                        dr == 0 ? "Master" : "Slave");

                int result = ide_init(&disks[idx], (ide_channel_t)ch, dr);

                if (result == IDE_OK) {
                    terminal_success_printf("OK (%s)\n",
                            disks[idx].type == IDE_TYPE_ATA ? "ATA" :
                            disks[idx].type == IDE_TYPE_ATAPI ? "ATAPI" : "NONE");  
                    ide_drv->initialized = true;
                } else {
                    terminal_warn_printf("Failed (%d)\n", result);
                }
            }
        }
    } else {
        driver_unregister(&g_ide_driver);
        terminal_error_printf("No IDE found\n");
    }

    blockdev_init();

    blockdev_scan_all_disks(1);

    if (ahci_drv && ahci_drv->probe && ahci_drv->probe(ahci_drv) == 0) {
        if (ahci_init() == 0) {
	        blockdev_scan_all_disks(2);
            terminal_success_printf("AHCI OK");
        } else {
	        terminal_warn_printf("AHCI failed.\n");
        }
    } else {
        driver_unregister(&g_ahci_driver);
        terminal_error_printf("No AHCI found\n");
    }

    if (rtl8139_drv && rtl8139_drv->probe && rtl8139_drv->probe(rtl8139_drv) == 0) {
        if (rtl8139_drv->init && rtl8139_drv->init(rtl8139_drv) == 0) {
            terminal_printf("[RTL8139] OK");
        } else {
            terminal_error_printf("[RTL8139] Init failed");
        }
    } else {
        driver_unregister(&g_rtl8139_driver);
    }

    blockdev_dump_all();

    partition_init();
    partition_scan_all();

    net_init();

    dhcp_start_on_all_interfaces(dhcp_global_callback);

    fs_init();

    crypto_init();

    findroot_init();

    inte();

    vfs_inode_t *root_fs = findroot_scan_and_mount();

    if (root_fs) {
        //Found the root file system
        fs_root = root_fs;
        current_dir = fs_root;
        terminal_success_printf("Root filesystem mounted\n");
        create_dir("/", "etc");
    } else {
        //There is no root file system, we show all mounted ones
        terminal_warn_printf("No root filesystem. Use 'setroot <fs> <disk>' or 'chooseroot <index>'\n");
        findroot_print_candidates();
    }

    terminal_printf("BIOS device number: 0x%02x\n", bios_number);
    boot_device_info_t boot_info;
    biosdev_parse(bios_number, &boot_info);
    biosdev_print_info(&boot_info);
    /*
 * if (boot_info.type == BOOT_DEVICE_CDROM) {
        terminal_printf("\n[TEST] Booted from CD-ROM, trying to read...\n");
    
        cdrom_t cdrom = cdrom_find_and_init();
        if (cdrom) {
            cdrom_print_info(cdrom);
    
            //Trying to read the ISO9660 root directory
            u8 sector[2048];
            if (cdrom_read_sector(cdrom, 16, sector) == 0) {
                terminal_success_printf("[TEST] Successfully read ISO9660 PVD\n");
            }
        }
    } 
    else if (boot_info.type == BOOT_DEVICE_HDD) {
        terminal_printf("\n[TEST] Booted from Hard Disk\n");
        terminal_printf("[TEST] This is a normal boot from installed system\n");
    }
    else {
        terminal_printf("\n[TEST] Booted from unknown or removable device\n");
    }
 */

    terminal_enable_prompt(true);

    for (;;) { halt(); }
}
