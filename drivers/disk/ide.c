#include <kdisk/ide.h>
#include <asm/io.h>
#include <libk/string.h>
#include <idt.h>
#include <ioapic.h>
#include <kernel/types.h>
#include <isr.h>
#include <kernel/driver.h>
#include <apic.h>

static int ide_probe(driver_t *drv) {
    (void)drv;
    pci_device_t* pci_ide = pci_find_class(0x01, 0x01);
    if (pci_ide) return 0;
    return 1;
}

driver_t g_ide_driver = {
    .name = "ide",
    .desc = "IDE Disk driver",
    .critical_level = DRIVER_CRITICAL_3,
    .probe = ide_probe,
    .init = NULL,
    .remove = NULL
};

static volatile ide_disk_t* primary_irq_disk = NULL;
static volatile ide_disk_t* secondary_irq_disk = NULL;

/*
 * Low latency: four ALTSTATUS reads (~100ns)
 */
static inline void io_delay(u16 ctrl_port)
{
    (void)inb(ctrl_port + IDE_ALTSTATUS);
    (void)inb(ctrl_port + IDE_ALTSTATUS);
    (void)inb(ctrl_port + IDE_ALTSTATUS);
    (void)inb(ctrl_port + IDE_ALTSTATUS);
}

void ide_disable_interrupts(u16 ctrl_port) {
    outb(ctrl_port + IDE_CONTROL, 0x02); // nIEN = 1 (No Interrupt)
}

void ide_enable_interrupts(u16 ctrl_port) {
    outb(ctrl_port + IDE_CONTROL, 0x00); // nIEN = 0 (Interrupt Enabled)
}

/*
 * Waiting for BSY=0
 */
static int wait_bsy_clear(u16 base_port, u16 ctrl_port, u32 timeout)
{
    for (u32 i = 0; i < timeout; ++i)
    {
        u8 s = inb(base_port + IDE_STATUS);
        if (!(s & IDE_STATUS_BSY))
            return IDE_OK;
        if ((i & 0xFF) == 0)
            io_delay(ctrl_port);
    }
    return IDE_ERR_TIMEOUT;
}

/*
 * We are waiting for DRQ=1 and BSY=0; if ERR - return an error
 */
static int wait_drq_or_err(u16 base_port, u16 ctrl_port, u32 timeout)
{
    for (u32 i = 0; i < timeout; ++i)
    {
        u8 s = inb(base_port + IDE_STATUS);
        if (s & IDE_STATUS_ERR)
            return IDE_ERR_DEVICE;
        if (!(s & IDE_STATUS_BSY) && (s & IDE_STATUS_DRQ))
            return IDE_OK;
        if ((i & 0xFF) == 0)
            io_delay(ctrl_port);
    }
    return IDE_ERR_TIMEOUT;
}

/*
 * Check ERR and read error register
 */
static int check_err_and_clear(u16 base_port)
{
    u8 s = inb(base_port + IDE_STATUS);
    if (s & IDE_STATUS_ERR)
    {
        (void)inb(base_port + IDE_ERROR);
        return IDE_ERR_DEVICE;
    }
    return IDE_OK;
}

/*
 * Select device (LBA/CHS) and delay
 */
static void select_device_and_delay(u16 base, u16 ctrl, u8 drive, int lba_flag, u8 head_high4)
{
    u8 value = (lba_flag ? 0xE0 : 0xA0) | ((drive & 1) << 4) | (head_high4 & 0x0F);
    outb(base + IDE_SELECT, value);
    io_delay(ctrl);
}

/*
 * Collect 4 words of identifier in uint64_t (small word order)
 */
static u64 ident_words_to_u64(const u16 ident[256], int w)
{
    u64 v = 0;
    v |= (u64)ident[w + 0];
    v |= (u64)ident[w + 1] << 16;
    v |= (u64)ident[w + 2] << 32;
    v |= (u64)ident[w + 3] << 48;
    return v;
}

/*
 * Read one sector (256 words) in aligned_words
 */
static void read_sector_words_to(u16 base, u16 aligned_words[256])
{
    for (int i = 0; i < 256; ++i)
        aligned_words[i] = inw(base + IDE_DATA);
}

/*
 * Write one sector (256 words) from aligned_words
 */
static void write_sector_words_from(u16 base, const u16 aligned_words[256])
{
    for (int i = 0; i < 256; ++i)
        outw(base + IDE_DATA, aligned_words[i]);
}

/*
 * IDENTIFY: fills ident_buffer and disk fields
 */
int ide_identify(ide_disk_t *disk, u16 ident_buffer[256])
{
    if (!disk || !ident_buffer)
        return IDE_ERR_INVALID;

    disk->type = IDE_TYPE_NONE;
    disk->supports_lba48 = 0;
    disk->sector_size = 512;
    disk->total_sectors = 0;
    disk->irq_pending = 0;
    disk->irq_count = 0;

    /*
 * Device Selection (CHS)
 */
    select_device_and_delay(disk->base_port, disk->ctrl_port, disk->drive, 0, 0);

    /*
 * Sending IDENTIFY
 */
    outb(disk->base_port + IDE_COMMAND, IDE_CMD_IDENTIFY);
    io_delay(disk->ctrl_port);

    u8 status = inb(disk->base_port + IDE_STATUS);
    if (status == 0)
        return IDE_ERR_DEVICE;

    /*
 * If ERR - possible ATAPI
 */
    if (status & IDE_STATUS_ERR)
    {
        u8 cl = inb(disk->base_port + IDE_LCYL);
        u8 ch = inb(disk->base_port + IDE_HCYL);
        if ((cl == 0x14 && ch == 0xEB) || (cl == 0x69 && ch == 0x96))
        {
            outb(disk->base_port + IDE_COMMAND, IDE_CMD_IDENTIFY_PACKET);
            if (wait_bsy_clear(disk->base_port, disk->ctrl_port, IDE_TIMEOUT_LOOPS) != IDE_OK)
                return IDE_ERR_TIMEOUT;
            if (check_err_and_clear(disk->base_port) != IDE_OK)
                return IDE_ERR_DEVICE;
            if (wait_drq_or_err(disk->base_port, disk->ctrl_port, IDE_TIMEOUT_LOOPS) != IDE_OK)
                return IDE_ERR_TIMEOUT;
            for (int i = 0; i < 256; ++i)
                ident_buffer[i] = inw(disk->base_port + IDE_DATA);
            disk->type = IDE_TYPE_ATAPI;
            disk->sector_size = 2048;
            disk->total_sectors = 0;
            return IDE_OK;
        }
        else
        {
            return IDE_ERR_DEVICE;
        }
    }

    if (wait_bsy_clear(disk->base_port, disk->ctrl_port, IDE_TIMEOUT_LOOPS) != IDE_OK)
        return IDE_ERR_TIMEOUT;
    int rc = wait_drq_or_err(disk->base_port, disk->ctrl_port, IDE_TIMEOUT_LOOPS);
    if (rc != IDE_OK)
        return rc;

    for (int i = 0; i < 256; ++i)
        ident_buffer[i] = inw(disk->base_port + IDE_DATA);

    if (ident_buffer[0] == 0)
        return IDE_ERR_DEVICE;

    disk->type = IDE_TYPE_ATA;

    /*
 * LBA28 (words 60-61)
 */
    u32 lba28 = ((u32)ident_buffer[61] << 16) | ident_buffer[60];
    disk->total_sectors = lba28;

    /*
 * Checking LBA48 (word 83 bit 10)
 */
    if (ident_buffer[83] & (1u << 10))
    {
        disk->supports_lba48 = 1;
        u64 lba48 = ident_words_to_u64(ident_buffer, 100);
        if (lba48 != 0)
            disk->total_sectors = lba48;
    }
    else
    {
        disk->supports_lba48 = 0;
    }

    disk->sector_size = 512;
    return IDE_OK;
}

/*
 * Initializing the structure and calling IDENTIFY
 */
int ide_init(ide_disk_t *disk, ide_channel_t channel, u8 drive)
{
    if (!disk || drive > 1)
        return IDE_ERR_INVALID;

    if (channel == IDE_CHANNEL_PRIMARY)
    {
        disk->base_port = IDE_BASE_PRIMARY;
        disk->ctrl_port = IDE_CTRL_PRIMARY;
        primary_irq_disk = disk;
    }
    else
    {
        disk->base_port = IDE_BASE_SECONDARY;
        disk->ctrl_port = IDE_CTRL_SECONDARY;
	secondary_irq_disk = disk;
    }

    ide_enable_interrupts(disk->ctrl_port);

    disk->drive = drive & 1;
    disk->channel = channel;
    disk->type = IDE_TYPE_NONE;
    disk->sector_size = 512;
    disk->supports_lba48 = 0;
    disk->total_sectors = 0;
    disk->irq_pending = 0;
    disk->irq_count = 0;

    if (channel == IDE_CHANNEL_PRIMARY) {
        disk->irq = 14;
    } else {
        disk->irq = 15;
    }

    pci_device_t* pci_ide = pci_find_class(0x01, 0x01);
    if (pci_ide) {
        disk->irq = pci_ide->interrupt_line;
        disk->pci_dev = pci_ide;
        
        //Enable Bus Mastering for DMA
        pci_enable_busmaster(pci_ide);
    }

    u32 gsi;
    u32 flags;

    if (!ioapic_get_override(disk->irq, &gsi, &flags)) {
        //If there is no override, use standard values
        gsi = disk->irq;
        flags = IOAPIC_FLAG_EDGE_TRIGGERED | IOAPIC_FLAG_ACTIVE_HIGH;
    }

    //Interrupt vector = 32 + IRQ number (as in IDT)
    u8 vector = 32 + disk->irq;

    //Setting up a redirect in IOAPIC
    if (!ioapic_redirect_irq(gsi, vector, apic_get_id(), flags)) {
        terminal_error_printf("[IDE] Failed to redirect IRQ %d (GSI %d)\n", disk->irq, gsi);
    } else {
        //Let's unmask the interrupt
        ioapic_unmask_irq(gsi);
        terminal_success_printf("[IDE] IRQ %d (GSI %d) -> vector %d, unmasked\n", 
                            disk->irq, gsi, vector);
    }   

    if (channel == IDE_CHANNEL_PRIMARY) {
        idt_set_gate(disk->irq, ide_pri_irq, KERNEL_CODE_SEL, IDT_GATE_INT, 0);
    } else {
        idt_set_gate(disk->irq, ide_sec_irq, KERNEL_CODE_SEL, IDT_GATE_INT, 0);
    }

    

    u16 ident[256];
    int rc = ide_identify(disk, ident);
    if (rc != IDE_OK)
        return rc;

    return IDE_OK;
}

/*
 * Setting up registers for LBA28
 */
static void setup_lba28_regs(u16 base, u16 ctrl, u32 lba, u8 count, u8 drive)
{
    select_device_and_delay(base, ctrl, drive, 1, (u8)((lba >> 24) & 0x0F));
    outb(base + IDE_NSECT, count);
    outb(base + IDE_SECTOR, (u8)(lba & 0xFF));
    outb(base + IDE_LCYL, (u8)((lba >> 8) & 0xFF));
    outb(base + IDE_HCYL, (u8)((lba >> 16) & 0xFF));
}

/*
 * Setting up registers for LBA48
 */
static void setup_lba48_regs(u16 base, u16 ctrl, u64 lba, u16 count, u8 drive)
{
    select_device_and_delay(base, ctrl, drive, 1, (u8)((lba >> 24) & 0x0F));
    io_delay(ctrl);

    outb(base + IDE_NSECT, (u8)((count >> 8) & 0xFF)); /*
 * SECCOUNT1
 */
    outb(base + IDE_SECTOR, (u8)((lba >> 24) & 0xFF)); /*
 * LBA3
 */
    outb(base + IDE_LCYL, (u8)((lba >> 32) & 0xFF));   /*
 * LBA4
 */
    outb(base + IDE_HCYL, (u8)((lba >> 40) & 0xFF));   /*
 * LBA5
 */

    outb(base + IDE_NSECT, (u8)(count & 0xFF));      /*
 * SECCOUNT0
 */
    outb(base + IDE_SECTOR, (u8)(lba & 0xFF));       /*
 * LBA0
 */
    outb(base + IDE_LCYL, (u8)((lba >> 8) & 0xFF));  /*
 * LBA1
 */
    outb(base + IDE_HCYL, (u8)((lba >> 16) & 0xFF)); /*
 * LBA2
 */
}

/*
 * Checking 2-byte alignment
 */
static inline int is_aligned_2(const void *ptr)
{
    return (((uptr)ptr) & 1u) == 0;
}

/*
 * Reading sectors
 */
int ide_read_sectors(ide_disk_t *disk, u64 lba, u32 count, void *buffer)
{
    if (!disk || !buffer || count == 0)
        return IDE_ERR_INVALID;

    if (disk->total_sectors && lba > disk->total_sectors - (u64)count)
        return IDE_ERR_INVALID;

    if (buffer == NULL) {
        return IDE_ERR_INVALID;
    }

    const u32 max_per_op = 256;
    u8 *user_buf = (u8 *)buffer;
    u32 remaining = count;

    u16 tmp_sector_words[256];

    while (remaining)
    {
        u32 chunk = remaining > max_per_op ? max_per_op : remaining;

        for (u32 s = 0; s < chunk; ++s)
        {
            u64 cur_lba = lba + (count - remaining) + s;

            if (disk->supports_lba48 && (cur_lba > 0x0FFFFFFF))
            {
                setup_lba48_regs(disk->base_port, disk->ctrl_port, cur_lba, 1, disk->drive);
                outb(disk->base_port + IDE_COMMAND, IDE_CMD_READ_SECTORS_EXT);
            }
            else
            {
                u32 cur_lba32 = (u32)cur_lba;
                setup_lba28_regs(disk->base_port, disk->ctrl_port, cur_lba32, 1, disk->drive);
                outb(disk->base_port + IDE_COMMAND, IDE_CMD_READ_SECTORS);
            }

            int rc = wait_bsy_clear(disk->base_port, disk->ctrl_port, IDE_TIMEOUT_LOOPS);
            if (rc != IDE_OK)
                return rc;

            rc = wait_drq_or_err(disk->base_port, disk->ctrl_port, IDE_TIMEOUT_LOOPS);
            if (rc != IDE_OK)
                return rc;

            int bytes_per_sector = disk->sector_size;
            int words_per_sector = bytes_per_sector / 2;

            u8 *dest = user_buf + ((count - remaining) + s) * (sz)bytes_per_sector;
            if (is_aligned_2(dest) && bytes_per_sector == 512)
            {
                u16 *wptr = (u16 *)dest;
                for (int i = 0; i < words_per_sector; ++i)
                    wptr[i] = inw(disk->base_port + IDE_DATA);
            }
            else
            {
                if (bytes_per_sector == 512)
                {
                    read_sector_words_to(disk->base_port, tmp_sector_words);
                    memcpy(dest, tmp_sector_words, 512);
                }
                else
                {
                    for (int i = 0; i < words_per_sector; ++i)
                    {
                        u16 w = inw(disk->base_port + IDE_DATA);
                        dest[2 * i + 0] = (u8)(w & 0xFF);
                        dest[2 * i + 1] = (u8)((w >> 8) & 0xFF);
                    }
                }
            }
        }

        user_buf += (sz)chunk * disk->sector_size;
        remaining -= chunk;
        lba += chunk;
    }

    return IDE_OK;
}

/*
 * Recording sectors
 */
int ide_write_sectors(ide_disk_t *disk, u64 lba, u32 count, const void *buffer)
{
    if (!disk || !buffer || count == 0)
        return IDE_ERR_INVALID;

    if (disk->total_sectors && lba > disk->total_sectors - (u64)count)
        return IDE_ERR_INVALID;

    const u32 max_per_op = 256;
    const u8 *user_buf = (const u8 *)buffer;
    u32 remaining = count;
    int used_lba48 = 0;

    u16 tmp_sector_words[256];

    while (remaining)
    {
        u32 chunk = remaining > max_per_op ? max_per_op : remaining;

        for (u32 s = 0; s < chunk; ++s)
        {
            u64 cur_lba = lba + (count - remaining) + s;

            int use_lba48 = disk->supports_lba48 && (cur_lba > 0x0FFFFFFF);

            if (use_lba48)
            {
                setup_lba48_regs(disk->base_port, disk->ctrl_port, cur_lba, 1, disk->drive);
                outb(disk->base_port + IDE_COMMAND, IDE_CMD_WRITE_SECTORS_EXT);
                used_lba48 = 1;
            }
            else
            {
                u32 cur_lba32 = (u32)cur_lba;
                setup_lba28_regs(disk->base_port, disk->ctrl_port, cur_lba32, 1, disk->drive);
                outb(disk->base_port + IDE_COMMAND, IDE_CMD_WRITE_SECTORS);
            }

            int rc = wait_bsy_clear(disk->base_port, disk->ctrl_port, IDE_TIMEOUT_LOOPS);
            if (rc != IDE_OK)
                return rc;

            rc = wait_drq_or_err(disk->base_port, disk->ctrl_port, IDE_TIMEOUT_LOOPS);
            if (rc != IDE_OK)
                return rc;

            int bytes_per_sector = disk->sector_size;
            int words_per_sector = bytes_per_sector / 2;
            const u8 *src = user_buf + ((count - remaining) + s) * (sz)bytes_per_sector;

            if (is_aligned_2(src) && bytes_per_sector == 512)
            {
                const u16 *wptr = (const u16 *)src;
                for (int i = 0; i < words_per_sector; ++i)
                    outw(disk->base_port + IDE_DATA, wptr[i]);
            }
            else
            {
                if (bytes_per_sector == 512)
                {
                    memcpy(tmp_sector_words, src, 512);
                    for (int i = 0; i < words_per_sector; ++i)
                        outw(disk->base_port + IDE_DATA, tmp_sector_words[i]);
                }
                else
                {
                    for (int i = 0; i < words_per_sector; ++i)
                    {
                        u16 w = (u16)src[2 * i] | ((u16)src[2 * i + 1] << 8);
                        outw(disk->base_port + IDE_DATA, w);
                    }
                }
            }

            if (wait_bsy_clear(disk->base_port, disk->ctrl_port, IDE_TIMEOUT_LOOPS) != IDE_OK)
                return IDE_ERR_TIMEOUT;

            if (check_err_and_clear(disk->base_port) != IDE_OK)
                return IDE_ERR_DEVICE;
        }

        user_buf += (sz)chunk * disk->sector_size;
        remaining -= chunk;
        lba += chunk;
    }

    /*
 * Flush cache
 */
    if (used_lba48)
        outb(disk->base_port + IDE_COMMAND, IDE_CMD_CACHE_FLUSH_EXT);
    else
        outb(disk->base_port + IDE_COMMAND, IDE_CMD_CACHE_FLUSH);

    if (wait_bsy_clear(disk->base_port, disk->ctrl_port, IDE_TIMEOUT_LOOPS) != IDE_OK)
        return IDE_ERR_TIMEOUT;
    if (check_err_and_clear(disk->base_port) != IDE_OK)
        return IDE_ERR_DEVICE;

    return IDE_OK;
}

void ide_primary_irq_handler(void) {
    //1. Be sure to read the status register to clear the interrupt
    if (primary_irq_disk) {
        u8 status = inb(IDE_BASE_PRIMARY + IDE_STATUS);
        (void)status; //Use a variable to avoid warning
        
        primary_irq_disk->irq_pending = 1;
        primary_irq_disk->irq_count++;
    }
    
    //2. Send EOI
    ioapic_eoi(primary_irq_disk->irq);
}

void ide_secondary_irq_handler(void) {
    if (secondary_irq_disk) {
        u8 status = inb(IDE_BASE_SECONDARY + IDE_STATUS);
        (void)status;
        
        secondary_irq_disk->irq_pending = 1;
        secondary_irq_disk->irq_count++;
    }
    
    ioapic_eoi(secondary_irq_disk->irq);
}

int ide_wait_irq(ide_disk_t *disk, u32 timeout_ms) {
    if (!disk) return IDE_ERR_INVALID;
    
    disk->irq_pending = 0;
    
    for (u32 i = 0; i < timeout_ms * 1000; i++) {
        if (disk->irq_pending) {
            disk->irq_pending = 0;
            return IDE_OK;
        }
        for (volatile int j = 0; j < 1000; j++);
    }
    
    return IDE_ERR_TIMEOUT;
}

u8 ide_get_irq_count(ide_disk_t *disk) {
    return disk ? disk->irq_count : 0;
}
