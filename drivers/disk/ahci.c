#include <kdisk/ahci.h>
#include <pci.h>
#include <kernel/terminal.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <libk/string.h>
#include <kernel/timer.h>
#include <kernel/driver.h>

// ==================== GLOBALS ====================
static hba_mem_t* g_ahci_hba = NULL;
static uptr g_ahci_phys_base = 0;
static uptr g_ahci_virt_base = 0;
static pci_device_t* g_ahci_pci_dev = NULL;
static ahci_port_t* g_ahci_ports[32];

extern pmm_t pmm;

static int ahci_probe(driver_t *drv) {
    (void)drv;
    
    //Looking for an AHCI controller
    pci_device_t* pci_dev = pci_find_class(0x01, 0x06);
    if (!pci_dev) return 1;
    
    //We check that this is AHCI (prog_if = 0x01)
    if (pci_dev->prog_if != 0x01) return 1;
    
    return 0;
}

static int ahci_init_driver(driver_t *drv) {
    (void)drv;
    return ahci_init();
}

static void ahci_remove_driver(driver_t *drv) {
    (void)drv;
    //Resource cleanup (if needed)
}

driver_t g_ahci_driver = {
    .name = "ahci",
    .desc = "AHCI SATA Disk Driver",
    .critical_level = DRIVER_CRITICAL_3,
    .probe = ahci_probe,
    .init = ahci_init_driver,
    .remove = ahci_remove_driver
};

// ==================== HELPER FUNCTIONS ====================

static inline u64 ahci_get_ticks_ms(void) {
    return timer_apic_ticks();
}

static inline void ahci_mdelay(u32 ms) {
    timer_mdelay(ms);
}

static inline void ahci_udelay(u32 us) {
    timer_udelay(us);
}

static void ahci_stop_cmd(hba_port_t* port) {
    port->cmd &= ~HBA_PxCMD_ST;
    
    u64 timeout = ahci_get_ticks_ms() + 100;
    while ((port->cmd & HBA_PxCMD_CR) && ahci_get_ticks_ms() < timeout) {
        asm volatile("pause");
    }
    
    port->cmd &= ~HBA_PxCMD_FRE;
}

static void ahci_start_cmd(hba_port_t* port) {
    port->cmd &= ~HBA_PxCMD_ST;
    
    u64 timeout = ahci_get_ticks_ms() + 100;
    while ((port->cmd & HBA_PxCMD_CR) && ahci_get_ticks_ms() < timeout) {
        asm volatile("pause");
    }
    
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

static int ahci_find_cmd_slot(hba_port_t* port) {
    u32 slots = port->sact | port->ci;
    for (int i = 0; i < 8; i++) {
        if ((slots & 1) == 0)
            return i;
        slots >>= 1;
    }
    return -1;
}

static int ahci_acquire_buffer(ahci_port_t* port) {
    while (__sync_lock_test_and_set(&port->buffer_semaphore, 1)) {
        asm volatile("pause");
    }
    
    for (int i = 0; i < 8; i++) {
        if (!__sync_lock_test_and_set(&port->buffer_locks[i], 1)) {
            __sync_lock_release(&port->buffer_semaphore);
            return i;
        }
    }
    
    __sync_lock_release(&port->buffer_semaphore);
    return -1;
}

static void ahci_release_buffer(ahci_port_t* port, int index) {
    if (index < 0 || index >= 8) return;
    __sync_lock_release(&port->buffer_locks[index]);
    __sync_lock_release(&port->buffer_semaphore);
}

static void ahci_port_lock(ahci_port_t* port) {
    while (__sync_lock_test_and_set(&port->port_lock, 1)) {
        asm volatile("pause");
    }
}

static void ahci_port_unlock(ahci_port_t* port) {
    __sync_synchronize();
    __sync_lock_release(&port->port_lock);
}

static void* ahci_alloc_dma_page(void) {
    void* phys = pmm_alloc_page(&pmm);
    if (!phys) return NULL;
    memset((void*)(uptr)phys, 0, PAGE_SIZE);
    return phys;
}

// ==================== PORT ACCESS ====================

static int ahci_port_access(ahci_port_t* port, u64 lba, u32 count, 
                            uptr phys_buffer, int write) {
    ahci_port_lock(port);
    
    hba_port_t* regs = port->regs;
    regs->ie = 0xFFFFFFFF;
    regs->is = 0;
    
    int slot = ahci_find_cmd_slot(regs);
    if (slot == -1) {
        terminal_error_printf("[AHCI] No command slot\n");
        ahci_port_unlock(port);
        return -1;
    }
    
    regs->serr = 0;
    regs->tfd = 0;
    
    hba_cmd_header_t* cmd_header = &port->cmd_list[slot];
    
    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(u32);
    cmd_header->a = 0;
    cmd_header->w = write;
    cmd_header->c = 0;
    cmd_header->p = 0;
    cmd_header->prdbc = 0;
    cmd_header->pmp = 0;
    
    hba_cmd_tbl_t* cmd_tbl = port->cmd_tables[slot];
    memset(cmd_tbl, 0, sizeof(hba_cmd_tbl_t));
    
    cmd_tbl->prdt_entry[0].dba = (u32)(phys_buffer & 0xFFFFFFFF);
    cmd_tbl->prdt_entry[0].dbau = (u32)((phys_buffer >> 32) & 0xFFFFFFFF);
    cmd_tbl->prdt_entry[0].dbc = port->sector_size * count - 1;
    cmd_tbl->prdt_entry[0].i = 1;
    
    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)cmd_tbl->cfis;
    memset(cmd_tbl->cfis, 0, sizeof(fis_reg_h2d_t));
    
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->pmport = 0;
    
    if (write) {
        cmdfis->command = ATA_CMD_WRITE_DMA_EX;
    } else {
        cmdfis->command = ATA_CMD_READ_DMA_EX;
    }
    
    cmdfis->lba0 = (u8)(lba & 0xFF);
    cmdfis->lba1 = (u8)((lba >> 8) & 0xFF);
    cmdfis->lba2 = (u8)((lba >> 16) & 0xFF);
    cmdfis->device = 1 << 6;
    
    cmdfis->lba3 = (u8)((lba >> 24) & 0xFF);
    cmdfis->lba4 = (u8)((lba >> 32) & 0xFF);
    cmdfis->lba5 = (u8)((lba >> 40) & 0xFF);
    
    cmdfis->countl = (u8)(count & 0xFF);
    cmdfis->counth = (u8)(count >> 8);
    
    cmdfis->control = 0x8;
    
    // Wait for device ready
    u64 timeout = ahci_get_ticks_ms() + 100;
    while ((regs->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && ahci_get_ticks_ms() < timeout) {
        asm volatile("pause");
    }
    
    if (ahci_get_ticks_ms() >= timeout) {
        terminal_error_printf("[AHCI] Port timeout before command\n");
        ahci_port_unlock(port);
        return -1;
    }
    
    regs->ie = 0xFFFFFFFF;
    regs->is = 0xFFFFFFFF;
    
    ahci_start_cmd(regs);
    regs->ci |= 1 << slot;
    
    // Wait for command completion
    timeout = ahci_get_ticks_ms() + 200;
    while ((regs->ci & (1 << slot)) && ahci_get_ticks_ms() < timeout) {
        if (regs->is & HBA_PxIS_TFES) {
            terminal_error_printf("[AHCI] Task file error, SERR: 0x%x\n", regs->serr);
            ahci_stop_cmd(regs);
            ahci_port_unlock(port);
            return -1;
        }
        asm volatile("pause");
    }
    
    if (ahci_get_ticks_ms() >= timeout) {
        terminal_error_printf("[AHCI] Command timeout\n");
        ahci_stop_cmd(regs);
        ahci_port_unlock(port);
        return -1;
    }
    
    timeout = ahci_get_ticks_ms() + 100;
    while ((regs->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && ahci_get_ticks_ms() < timeout) {
        asm volatile("pause");
    }
    
    ahci_stop_cmd(regs);
    
    if (regs->is & HBA_PxIS_TFES) {
        terminal_error_printf("[AHCI] Task file error after command\n");
        ahci_port_unlock(port);
        return -1;
    }
    
    ahci_port_unlock(port);
    return 0;
}

// ==================== IDENTIFY ====================

static void ahci_port_identify(ahci_port_t* port) {
    hba_port_t* regs = port->regs;
    
    regs->ie = 0xFFFFFFFF;
    regs->is = 0;
    
    int slot = ahci_find_cmd_slot(regs);
    if (slot == -1) {
        terminal_error_printf("[AHCI] No command slot for IDENTIFY\n");
        return;
    }
    
    regs->tfd = 0;
    
    hba_cmd_header_t* cmd_header = &port->cmd_list[slot];
    
    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(u32);
    cmd_header->a = 0;
    cmd_header->w = 0;
    cmd_header->c = 0;
    cmd_header->p = 0;
    cmd_header->prdbc = 0;
    cmd_header->pmp = 0;
    
    hba_cmd_tbl_t* cmd_tbl = port->cmd_tables[slot];
    memset(cmd_tbl, 0, sizeof(hba_cmd_tbl_t));
    
    uptr phys_buf = port->phys_buffers[0];
    
    cmd_tbl->prdt_entry[0].dba = (u32)(phys_buf & 0xFFFFFFFF);
    cmd_tbl->prdt_entry[0].dbau = (u32)((phys_buf >> 32) & 0xFFFFFFFF);
    cmd_tbl->prdt_entry[0].dbc = 512 - 1;
    cmd_tbl->prdt_entry[0].i = 1;
    
    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)cmd_tbl->cfis;
    memset(cmd_tbl->cfis, 0, sizeof(fis_reg_h2d_t));
    
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->pmport = 0;
    cmdfis->command = ATA_CMD_IDENTIFY;
    
    cmdfis->lba0 = 0;
    cmdfis->lba1 = 0;
    cmdfis->lba2 = 0;
    cmdfis->device = 0;
    cmdfis->lba3 = 0;
    cmdfis->lba4 = 0;
    cmdfis->lba5 = 0;
    cmdfis->countl = 0;
    cmdfis->counth = 0;
    cmdfis->control = 0;
    
    // Wait for device ready
    u64 timeout = ahci_get_ticks_ms() + 100;
    while ((regs->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && ahci_get_ticks_ms() < timeout) {
        asm volatile("pause");
    }
    
    regs->ie = 0xFFFFFFFF;
    regs->is = 0xFFFFFFFF;
    
    ahci_start_cmd(regs);
    regs->ci |= 1 << slot;
    
    timeout = ahci_get_ticks_ms() + 200;
    while ((regs->ci & (1 << slot)) && ahci_get_ticks_ms() < timeout) {
        if (regs->is & HBA_PxIS_TFES) {
            terminal_error_printf("[AHCI] IDENTIFY task file error\n");
            ahci_stop_cmd(regs);
            return;
        }
        asm volatile("pause");
    }
    
    ahci_stop_cmd(regs);
    
    if (regs->is & HBA_PxIS_TFES) {
        terminal_error_printf("[AHCI] IDENTIFY failed\n");
        return;
    }
    
    // Parse IDENTIFY data
    u16* identify = (u16*)port->virt_buffers[0];
    
    // Get sector size
    if (identify[106] & (1 << 12)) {
        port->sector_size = (identify[106] & (1 << 12)) ? 512 : 4096;
    } else {
        port->sector_size = 512;
    }
    
    // Get total sectors
    if (identify[83] & (1 << 10)) {
        port->supports_lba48 = 1;
        port->total_sectors = *(u64*)&identify[100];
    } else {
        port->supports_lba48 = 0;
        port->total_sectors = *(u32*)&identify[60];
    }
    
    terminal_printf("[AHCI] Port %d: %lu sectors, %u bytes/sector, LBA48: %s\n",
               port->port_num, (unsigned long)port->total_sectors, port->sector_size,
               port->supports_lba48 ? "yes" : "no");
}

// ==================== PORT INITIALIZATION ====================

static ahci_port_t* ahci_port_init(int port_num, hba_port_t* port_regs, hba_mem_t* hba) {
    
    //Step 1: select the port structure
    ahci_port_t* port = (ahci_port_t*)malloc(sizeof(ahci_port_t));
    if (!port) {
        terminal_error_printf("[AHCI] Port %d: malloc failed for port structure\n", port_num);
        return NULL;
    }
    
    memset(port, 0, sizeof(ahci_port_t));
    port->regs = port_regs;
    port->port_num = port_num;
    port->status = AHCI_PORT_UNINITIALIZED;
    port->sector_size = 512;
    
    //Step 2: Stop the command engine
    port_regs->cmd &= ~HBA_PxCMD_ST;
    port_regs->cmd &= ~HBA_PxCMD_FRE;
    ahci_stop_cmd(port_regs);
    //Step 3: select command list
    void* cmd_list_phys = ahci_alloc_dma_page();
    if (!cmd_list_phys) {
        terminal_error_printf("[AHCI] Port %d: FAILED to allocate command list\n", port_num);
        free(port);
        return NULL;
    }
    
    port_regs->clb = (u32)((uptr)cmd_list_phys & 0xFFFFFFFF);
    port_regs->clbu = (u32)((uptr)cmd_list_phys >> 32);
    port->cmd_list = (hba_cmd_header_t*)cmd_list_phys;
    
    //Step 4: Select FIS
    void* fis_phys = ahci_alloc_dma_page();
    if (!fis_phys) {
        free(port);
        return NULL;
    }
    
    port_regs->fb = (u32)((uptr)fis_phys & 0xFFFFFFFF);
    port_regs->fbu = (u32)((uptr)fis_phys >> 32);
    port->fis = (hba_fis_t*)fis_phys;
    
    //Step 5: initialize FIS types
    port->fis->dsfis.fis_type = FIS_TYPE_DMA_SETUP;
    port->fis->psfis.fis_type = FIS_TYPE_PIO_SETUP;
    port->fis->rfis.fis_type = FIS_TYPE_REG_D2H;
    port->fis->sdbfis[0] = FIS_TYPE_DEV_BITS;
    
    //Step 6: allocate command tables for 8 slots
    for (int i = 0; i < 8; i++) {
        void* tbl_phys = ahci_alloc_dma_page();
        if (!tbl_phys) {
            free(port);
            return NULL;
        }
        
        port->cmd_list[i].prdtl = 1;
        port->cmd_list[i].ctba = (u32)((uptr)tbl_phys & 0xFFFFFFFF);
        port->cmd_list[i].ctbau = (u32)((uptr)tbl_phys >> 32);
        port->cmd_tables[i] = (hba_cmd_tbl_t*)tbl_phys;
    }
    
    //Step 7: allocate DMA buffers
    for (int i = 0; i < 8; i++) {
        void* buf_phys = ahci_alloc_dma_page();
        if (!buf_phys) {
            free(port);
            return NULL;
        }
        
        port->phys_buffers[i] = (uptr)buf_phys;
        port->virt_buffers[i] = (void*)(uptr)buf_phys;
        port->buffer_locks[i] = 0;
    }
    
    port->buffer_semaphore = 0;
    port->port_lock = 0;
    
    //Step 8: configure the port port_num);
    port_regs->sctl |= (SCTL_PORT_IPM_NOPART | SCTL_PORT_IPM_NOSLUM | SCTL_PORT_IPM_NODSLP);
    
    if (hba->cap & AHCI_CAP_SALP) {
        port_regs->cmd &= ~HBA_PxCMD_ASP;
    }
    
    port_regs->is = 0;
    port_regs->ie = 1;
    port_regs->cmd |= HBA_PxCMD_POD;
    port_regs->cmd |= HBA_PxCMD_SUD;
    
    ahci_mdelay(10);
    
    //Step 9: check the presence of the device
    int spin = 100;
    int det_value = 0;
    while (spin-- > 0) {
        det_value = port_regs->ssts & HBA_PxSSTS_DET;
        if (det_value == HBA_PxSSTS_DET_PRESENT) {
            break;
        }
        if (spin % 20 == 0) {
        }
        ahci_mdelay(1);
    }
    
    if (det_value != HBA_PxSSTS_DET_PRESENT) {
        terminal_error_printf("[AHCI] Port %d: device NOT present (DET=0x%x)\n", port_num, det_value);
        port->status = AHCI_PORT_ERROR;
        terminal_printf("[AHCI] Port %d: returning port with ERROR status\n", port_num);
        return port;
    }
    
    //Step 10: Activate the Interface
    port_regs->cmd = (port_regs->cmd & ~HBA_PxCMD_ICC) | HBA_PxCMD_ICC_ACTIVE;
    
    //Step 11: Wait for the device to be ready
    spin = 1000;
    int tfd_value = 0;
    while (spin-- > 0) {
        tfd_value = port_regs->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ);
        if (!tfd_value) {
            break;
        }
        if (spin % 100 == 0) {
        ahci_mdelay(1);
        }
    }
    
    if (spin <= 0) {
        terminal_error_printf("[AHCI] Port %d: device HUNG (tfd=0x%x)\n", port_num, port_regs->tfd);
        port->status = AHCI_PORT_ERROR;
        return port;
    }
    
    port->status = AHCI_PORT_ACTIVE;

    ahci_port_identify(port);
    return port;
}

// ==================== PUBLIC FUNCTIONS ====================

int ahci_init(void) {
    g_ahci_pci_dev = pci_find_class(0x01, 0x06);
    if (!g_ahci_pci_dev) {
        terminal_error_printf("[AHCI] No controller found\n");
        return -1;
    }
    if (g_ahci_pci_dev->prog_if != 0x01) {
        return -1;
    }
    pci_enable(g_ahci_pci_dev);
    pci_enable_busmaster(g_ahci_pci_dev);
    g_ahci_phys_base = g_ahci_pci_dev->bars[5] & ~0xF;
    if (!g_ahci_phys_base) {
        terminal_error_printf("[AHCI] No BAR5\n");
        return -1;
    }
    
    g_ahci_virt_base = g_ahci_phys_base;
    g_ahci_hba = (hba_mem_t*)g_ahci_virt_base;
    u32 timeout = ahci_get_ticks_ms() + 100;
    while (!(g_ahci_hba->ghc & AHCI_GHC_ENABLE) && ahci_get_ticks_ms() < timeout) {
        g_ahci_hba->ghc |= AHCI_GHC_ENABLE;
        ahci_mdelay(1);
    }
    
    g_ahci_hba->ghc &= ~AHCI_GHC_IE;
    
    g_ahci_hba->is = 0xFFFFFFFF;
    
    u32 pi = g_ahci_hba->pi;
    terminal_printf("[AHCI] Implemented ports: 0x%x\n", pi);
    
    for (int i = 0; i < 32; i++) {
        if ((pi >> i) & 1) {
            terminal_printf("[AHCI] Checking port %d...\n", i);
            terminal_printf("[AHCI]   ssts=0x%x, sig=0x%x\n", 
                           g_ahci_hba->ports[i].ssts, g_ahci_hba->ports[i].sig);
            
            u32 ipm = (g_ahci_hba->ports[i].ssts >> 8) & 0x0F;
            u32 det = g_ahci_hba->ports[i].ssts & HBA_PxSSTS_DET;
            
            terminal_printf("[AHCI]   IPM=%u, DET=%u\n", ipm, det);
            
            if (ipm != HBA_PORT_IPM_ACTIVE) {
                terminal_printf("[AHCI]   IPM not active (%u)\n", ipm);
                continue;
            }
            if (det != HBA_PxSSTS_DET_PRESENT) {
                terminal_printf("[AHCI]   DET not present (%u)\n", det);
                continue;
            }
            
            u32 sig = g_ahci_hba->ports[i].sig;
            if (sig == SATA_SIG_ATAPI) {
                continue;
            }
            if (sig == SATA_SIG_PM) {
                continue;
            }
            if (sig == SATA_SIG_SEMB) {
                continue;
            }
            
            terminal_printf("[AHCI] Found SATA drive on port %d\n", i);
            ahci_port_t* port = ahci_port_init(i, &g_ahci_hba->ports[i], g_ahci_hba);
            
            if (port) {
                terminal_printf("[AHCI] port_init returned, status=%d\n", port->status);
                if (port->status == AHCI_PORT_ACTIVE) {
                    g_ahci_ports[i] = port;
                    terminal_printf("[AHCI] Port %d added to g_ahci_ports\n", i);
                } else {
                    terminal_error_printf("[AHCI] Port %d status = %d (not active)\n", i, port->status);
                }
            } else {
                terminal_error_printf("[AHCI] ahci_port_init returned NULL for port %d\n", i);
            }
        }
    }
    
    terminal_printf("[AHCI] INIT COMPLETE. Active ports: ");
    int active_count = 0;
    for (int i = 0; i < 32; i++) {
        if (g_ahci_ports[i] && g_ahci_ports[i]->status == AHCI_PORT_ACTIVE) {
            terminal_printf("%d ", i);
            active_count++;
        }
    }
    terminal_printf("(%d active)\n", active_count);
    
    return 0;
}

// ==================== DISK OPERATIONS ====================

int ahci_port_read(ahci_port_t* port, u64 lba, u32 count, void* buffer) {
    if (!port || port->status != AHCI_PORT_ACTIVE) return -1;
    
    u32 sector_size = port->sector_size;
    u8* buf_ptr = (u8*)buffer;
    u64 remaining_sectors = (count + sector_size - 1) / sector_size;
    
    int buf_idx = ahci_acquire_buffer(port);
    if (buf_idx < 0) return -1;
    
    uptr phys_buf = port->phys_buffers[buf_idx];
    void* virt_buf = port->virt_buffers[buf_idx];
    
    while (remaining_sectors > 0) {
        u32 sectors_this = (remaining_sectors > 8) ? 8 : (u32)remaining_sectors;
        u32 bytes_this = sectors_this * sector_size;
        
        int ret = ahci_port_access(port, lba, sectors_this, phys_buf, 0);
        if (ret != 0) {
            ahci_release_buffer(port, buf_idx);
            return -1;
        }
        
        memcpy(buf_ptr, virt_buf, bytes_this);
        
        buf_ptr += bytes_this;
        lba += sectors_this;
        remaining_sectors -= sectors_this;
    }
    
    ahci_release_buffer(port, buf_idx);
    return 0;
}

int ahci_port_write(ahci_port_t* port, u64 lba, u32 count, const void* buffer) {
    if (!port || port->status != AHCI_PORT_ACTIVE) return -1;
    
    u32 sector_size = port->sector_size;
    const u8* buf_ptr = (const u8*)buffer;
    u64 remaining_sectors = (count + sector_size - 1) / sector_size;
    
    int buf_idx = ahci_acquire_buffer(port);
    if (buf_idx < 0) return -1;
    
    uptr phys_buf = port->phys_buffers[buf_idx];
    void* virt_buf = port->virt_buffers[buf_idx];
    
    while (remaining_sectors > 0) {
        u32 sectors_this = (remaining_sectors > 8) ? 8 : (u32)remaining_sectors;
        u32 bytes_this = sectors_this * sector_size;
        
        memcpy(virt_buf, buf_ptr, bytes_this);
        
        int ret = ahci_port_access(port, lba, sectors_this, phys_buf, 1);
        if (ret != 0) {
            ahci_release_buffer(port, buf_idx);
            return -1;
        }
        
        buf_ptr += bytes_this;
        lba += sectors_this;
        remaining_sectors -= sectors_this;
    }
    
    ahci_release_buffer(port, buf_idx);
    return 0;
}

ahci_port_t* ahci_get_port(int port_num) {
    if (port_num < 0 || port_num >= 32) return NULL;
    return g_ahci_ports[port_num];
}
