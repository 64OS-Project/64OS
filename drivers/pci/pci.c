#include <pci.h>
#include <asm/io.h>
#include <mm/heap.h>
#include <kernel/terminal.h>
#include <libk/string.h>
#include <kernel/types.h>
#include <kernel/driver.h>
#include <hda.h>

static int pcidrv_probe(driver_t *drv) {
    (void)drv;
    return 0;
}

driver_t g_pci_driver = {
    .name = "pci",
    .desc = "PCI driver",
    .critical_level = DRIVER_CRITICAL_2,
    .probe = pcidrv_probe,
    .init = NULL,
    .remove = NULL
};

static void pci_scan_bus(u8 bus);
u32 pci_read_addr(u8 bus, u8 slot, u8 func, u8 offset);

//==================== GLOBAL VARIABLES ====================
static pci_device_t* pci_devices = NULL;
static int device_count = 0;

//==================== BASIC OPERATIONS ====================

u32 pci_read_addr(u8 bus, u8 slot, u8 func, u8 offset) {
    //Forming an address
    u32 address = (u32)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    
    //Send address
    outl(0xCF8, address);
    
    //Reading the data
    return inl(0xCFC);
}

//Formation of address
static u32 pci_make_addr(u8 bus, u8 slot, u8 func, u8 offset) {
    return (1u << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
}

//Reading configuration
u32 pci_read(pci_device_t* dev, u8 offset) {
    u32 addr = pci_make_addr(dev->bus, dev->slot, dev->function, offset);
    outl(PCI_CONFIG_ADDRESS, addr);
    return inl(PCI_CONFIG_DATA);
}

//Write configuration
void pci_write(pci_device_t* dev, u8 offset, u32 value) {
    u32 addr = pci_make_addr(dev->bus, dev->slot, dev->function, offset);
    outl(PCI_CONFIG_ADDRESS, addr);
    outl(PCI_CONFIG_DATA, value);
}

//Fast field reading
static u16 pci_read_vendor(u8 bus, u8 slot, u8 func) {
    u32 addr = pci_make_addr(bus, slot, func, 0x00);
    outl(PCI_CONFIG_ADDRESS, addr);
    return inl(PCI_CONFIG_DATA) & 0xFFFF;
}

//==================== PCI/PCIe DETECTION ====================

//Search Capability
u8 pci_find_cap(pci_device_t* dev, u8 cap_id) {
    if (!dev) return 0;
    
    //Read the status to make sure the Capabilities List is supported
    u16 status = pci_read(dev, 0x04) >> 16;
    if (!(status & (1 << 4))) return 0;  //Capabilities List is not supported
    
    u8 cap_ptr = (pci_read(dev, 0x34) >> 8) & 0xFC;  //Low-order 2 bits are reserved
    if (!cap_ptr) return 0;
    
    u8 offset = cap_ptr;
    while (offset >= 0x40 && offset < 0xFF) {
        u8 id = pci_read(dev, offset) & 0xFF;
        if (id == cap_id) return offset;
        offset = (pci_read(dev, offset + 1) >> 8) & 0xFC;
    }
    return 0;
}

//PCIe detection
static void pci_detect_pcie(pci_device_t* dev) {
    dev->pcie_cap_offset = pci_find_cap(dev, 0x10); // PCIe Capability ID
    
    if (!dev->pcie_cap_offset) {
        dev->is_pcie = false;
        return;
    }
    
    dev->is_pcie = true;
    
    //Reading PCIe Capability
    u32 pcie_cap = pci_read(dev, dev->pcie_cap_offset);
    dev->pcie_version = (pcie_cap >> 16) & 0xF;
    dev->pcie_type = (pcie_cap >> 20) & 0xF;
    
    // Link Capabilities
    u32 link_cap = pci_read(dev, dev->pcie_cap_offset + 0x0C);
    dev->max_link_speed = link_cap & 0xF;
    dev->max_link_width = (link_cap >> 4) & 0x3F;
    
    // Link Status
    u16 link_status = pci_read(dev, dev->pcie_cap_offset + 0x12) & 0xFFFF;
    dev->link_speed = link_status & 0xF;
    dev->link_width = (link_status >> 4) & 0x3F;
    
    // MSI/MSI-X
    dev->msi_cap_offset = pci_find_cap(dev, 0x05); // MSI
    dev->msix_cap_offset = pci_find_cap(dev, 0x11); // MSI-X
}

//Detecting BAR registers
static void pci_detect_bars(pci_device_t* dev) {
    //Save the team
    u16 saved_cmd = dev->command;
    pci_write(dev, 0x04, saved_cmd & ~0x07); //Disable I/O and Memory
    
    for (int i = 0; i < 6; i++) {
        u8 offset = 0x10 + i * 4;
        u32 bar = pci_read(dev, offset);
        
        if (bar == 0) {
            dev->bars[i] = 0;
            dev->bar_sizes[i] = 0;
            dev->bar_types[i] = 0;
            continue;
        }
        
        //Determining the BAR type
        if (bar & 0x01) {
            // I/O Space BAR
            dev->bar_types[i] = 2;
            pci_write(dev, offset, 0xFFFFFFFF);
            u32 size = pci_read(dev, offset);
            size = ~(size & 0xFFFFFFFC) + 1;
            pci_write(dev, offset, bar);
            dev->bars[i] = bar & 0xFFFFFFFC;
            dev->bar_sizes[i] = size;
        } else {
            // Memory Space BAR
            u8 type = (bar >> 1) & 0x03;
            
            if (type == 0x00) { // 32-bit
                dev->bar_types[i] = 0;
                pci_write(dev, offset, 0xFFFFFFFF);
                u32 size = pci_read(dev, offset);
                size = ~(size & 0xFFFFFFF0) + 1;
                pci_write(dev, offset, bar);
                dev->bars[i] = bar & 0xFFFFFFF0;
                dev->bar_sizes[i] = size;
            } else if (type == 0x02) { // 64-bit
                dev->bar_types[i] = 1;
                if (i < 5) {
                    //Processing as 64-bit
                    u32 bar_high = pci_read(dev, offset + 4);
                    u64 full_bar = ((u64)bar_high << 32) | (bar & 0xFFFFFFF0);
                    
                    //We write down all units
                    pci_write(dev, offset, 0xFFFFFFFF);
                    pci_write(dev, offset + 4, 0xFFFFFFFF);
                    
                    u32 size_low = pci_read(dev, offset);
                    u32 size_high = pci_read(dev, offset + 4);
                    u64 full_size = ((u64)size_high << 32) | (size_low & 0xFFFFFFF0);
                    full_size = ~full_size + 1;
                    
                    //We restore
                    pci_write(dev, offset, bar);
                    pci_write(dev, offset + 4, bar_high);
                    
                    dev->bars[i] = full_bar;
                    dev->bar_sizes[i] = full_size;
                    i++; //Skip the next BAR
                }
            }
        }
    }
    
    //Rebuilding the team
    pci_write(dev, 0x04, saved_cmd);
}

//Single device discovery
static pci_device_t* pci_probe(u8 bus, u8 slot, u8 func) {
    u16 vendor = pci_read_vendor(bus, slot, func);
    if (vendor == PCI_INVALID_VENDOR) return NULL;
    
    pci_device_t* dev = malloc(sizeof(pci_device_t));
    memset(dev, 0, sizeof(pci_device_t));
    
    dev->bus = bus;
    dev->slot = slot;
    dev->function = func;
    dev->vendor_id = vendor;
    
    //Reading the main registers
    u32 reg00 = pci_read(dev, 0x00);
    dev->device_id = (reg00 >> 16) & 0xFFFF;
    
    u32 reg04 = pci_read(dev, 0x04);
    dev->command = reg04 & 0xFFFF;
    dev->status = (reg04 >> 16) & 0xFFFF;
    
    u32 reg08 = pci_read(dev, 0x08);
    dev->revision_id = reg08 & 0xFF;
    dev->prog_if = (reg08 >> 8) & 0xFF;
    dev->subclass = (reg08 >> 16) & 0xFF;
    dev->class_code = (reg08 >> 24) & 0xFF;
    
    u32 reg0C = pci_read(dev, 0x0C);
    dev->cache_line = reg0C & 0xFF;
    dev->latency_timer = (reg0C >> 8) & 0xFF;
    dev->header_type = (reg0C >> 16) & 0xFF;
    dev->bist = (reg0C >> 24) & 0xFF;
    
    u32 reg3C = pci_read(dev, 0x3C);
    dev->interrupt_line = reg3C & 0xFF;
    dev->interrupt_pin = (reg3C >> 8) & 0xFF;
    
    //Detecting BAR
    pci_detect_bars(dev);
    
    //Detecting PCIe
    pci_detect_pcie(dev);
    
    //Add to the list
    dev->next = pci_devices;
    pci_devices = dev;
    device_count++;
    
    return dev;
}

//Scan function
static void pci_scan_function(u8 bus, u8 slot, u8 func) {
    pci_probe(bus, slot, func);
    
    //If this is a PCI-to-PCI bridge, scan the secondary bus
    pci_device_t* dev = pci_devices; //Newly added device
    if (dev->class_code == 0x06 && dev->subclass == 0x04) {
        u8 secondary_bus = (pci_read(dev, 0x18) >> 8) & 0xFF;
        if (secondary_bus != 0) {
            pci_scan_bus(secondary_bus);
        }
    }
}

//Scan your device
static void pci_scan_device(u8 bus, u8 slot) {
    //Checking function 0
    if (pci_read_vendor(bus, slot, 0) == PCI_INVALID_VENDOR) return;
    
    pci_scan_function(bus, slot, 0);
    
    //Checking multifunction
    pci_device_t* dev = pci_devices; //Just added
    if (dev->header_type & 0x80) {
        for (u8 func = 1; func < 8; func++) {
            if (pci_read_vendor(bus, slot, func) != PCI_INVALID_VENDOR) {
                pci_scan_function(bus, slot, func);
            }
        }
    }
}

//Bus scan
static void pci_scan_bus(u8 bus) {
    for (u8 slot = 0; slot < 32; slot++) {
        pci_scan_device(bus, slot);
    }
}

//==================== PUBLIC FUNCTIONS ====================

//PCI/PCIe Initialization
void pci_init(void) {
    //Checking the multifunction of the host bridge (0:0:0)
    pci_device_t temp_dev = {.bus = 0, .slot = 0, .function = 0};
    u8 header_type = (pci_read(&temp_dev, 0x0C) >> 16) & 0xFF;
    
    if (header_type & 0x80) {
        // Multiple host controllers
        for (u8 func = 0; func < 8; func++) {
            if (pci_read_vendor(0, 0, func) != PCI_INVALID_VENDOR) {
                pci_scan_bus(func);
            }
        }
    } else {
        // Single host controller
        pci_scan_bus(0);
    }

    pci_device_t *hda_dev = pci_find_class(HDA_PCI_CLASS, HDA_PCI_SUBCLASS);
    if (hda_dev) {
        terminal_printf("[PCI] Found HDA controller\n");
        extern int hda_init(pci_device_t*);
        hda_init(hda_dev);
    }
}

//Scan (optional if you need to rescan)
int pci_scan(void) {
    //Clearing the old list
    pci_device_t* dev = pci_devices;
    while (dev) {
        pci_device_t* next = dev->next;
        free(dev);
        dev = next;
    }
    pci_devices = NULL;
    device_count = 0;
    
    //Let's scan again
    pci_init();
    return device_count;
}

//Search for device
pci_device_t* pci_find(u16 vendor_id, u16 device_id) {
    pci_device_t* dev = pci_devices;
    while (dev) {
        if (dev->vendor_id == vendor_id && dev->device_id == device_id) {
            return dev;
        }
        dev = dev->next;
    }
    return NULL;
}

//Search by class
pci_device_t* pci_find_class(u8 class_code, u8 subclass) {
    pci_device_t* dev = pci_devices;
    while (dev) {
        if (dev->class_code == class_code && dev->subclass == subclass) {
            return dev;
        }
        dev = dev->next;
    }
    return NULL;
}

//Turning on the device
void pci_enable(pci_device_t* dev) {
    dev->command |= (1 << 0) | (1 << 1) | (1 << 2); // I/O, Memory, Bus Master
    pci_write(dev, 0x04, dev->command);
}

//Enabling Bus Mastering
void pci_enable_busmaster(pci_device_t* dev) {
    dev->command |= (1 << 2);
    pci_write(dev, 0x04, dev->command);
}

//Enable MSI (if supported)
int pci_enable_msi(pci_device_t* dev, u8 vector, u32 apic_id) {
    if (!dev) return -1;
    
    u8 offset = pci_find_cap(dev, PCI_CAP_ID_MSI);
    if (!offset) {
        terminal_warn_printf("[PCI] No MSI capability for device %04X:%04X\n", 
                           dev->vendor_id, dev->device_id);
        return -1;
    }
    
    dev->msi_cap_offset = offset;
    
    //Reading control register
    u16 msg_control = pci_read(dev, offset + 2) & 0xFFFF;
    bool is_64bit = (msg_control & PCI_MSI_FLAGS_64BIT) != 0;
    bool per_vector_mask = (msg_control & PCI_MSI_FLAGS_MASKABLE) != 0;
    
    //Calculating offsets
    u8 addr_offset = offset + 4;
    u8 data_offset = is_64bit ? offset + 12 : offset + 8;
    u8 mask_offset = per_vector_mask ? (is_64bit ? offset + 16 : offset + 12) : 0;
    
    //Set the MSI address (APIC)
    u32 msi_addr = 0xFEE00000 | (apic_id << 24);  // Physical destination mode
    pci_write(dev, addr_offset, msi_addr);
    
    if (is_64bit) {
        pci_write(dev, addr_offset + 4, 0);  // Upper 32 bits = 0
    }
    
    //Set the data (interrupt vector)
    pci_write(dev, data_offset, vector);
    
    //If there is masking, we will unmask
    if (per_vector_mask && mask_offset) {
        u32 mask_bits = pci_read(dev, mask_offset);
        mask_bits &= ~1;  // Clear mask for vector 0
        pci_write(dev, mask_offset, mask_bits);
    }
    
    //Turn on MSI
    msg_control |= PCI_MSI_FLAGS_ENABLE;
    pci_write(dev, offset + 2, msg_control);
    
    dev->msi_enabled = true;
    terminal_printf("[PCI] MSI enabled for device %04X:%04X, vector=%d, apic_id=%d\n",
                   dev->vendor_id, dev->device_id, vector, apic_id);
    return 0;
}

int pci_enable_msix(pci_device_t* dev, int num_vectors, u8 vector_base, u32 apic_id) {
    u8 offset = pci_find_cap(dev, PCI_CAP_ID_MSIX);
    if (!offset) return -1;
    
    dev->msix_cap_offset = offset;
    
    u16 msg_control = pci_read(dev, offset + 2) & 0xFFFF;
    int table_size = (msg_control & 0x7FF) + 1;
    
    if (num_vectors > table_size) num_vectors = table_size;
    
    //Reading the table and PBA
    u32 table_bir = pci_read(dev, offset + 4);
    u32 pba_bir = pci_read(dev, offset + 8);
    
    //Turn on MSI-X
    msg_control |= (1 << 15);  // Enable MSI-X
    pci_write(dev, offset + 2, msg_control);
    
    dev->msix_enabled = true;
    terminal_printf("[PCI] MSI-X enabled for device %04X:%04X, table_size=%d\n",
                   dev->vendor_id, dev->device_id, table_size);
    return 0;
}

//==================== PCIe SPECIFIC FUNCTIONS ====================

//PCIe Activity Check
bool pcie_is_active(pci_device_t* dev) {
    return dev->is_pcie;
}

//Getting the PCIe Type
const char* pcie_type_name(pci_device_t* dev) {
    if (!dev->is_pcie) return "PCI";
    
    switch (dev->pcie_type) {
        case 0x0: return "Endpoint";
        case 0x1: return "Legacy Endpoint";
        case 0x4: return "Root Port";
        case 0x5: return "Upstream Port";
        case 0x6: return "Downstream Port";
        case 0x7: return "PCIe-to-PCI Bridge";
        case 0x8: return "PCI-to-PCIe Bridge";
        default: return "Unknown";
    }
}

//Getting Speed
u8 pcie_get_speed(pci_device_t* dev) {
    return dev->is_pcie ? dev->link_speed : 0;
}

//Getting the width
u16 pcie_get_width(pci_device_t* dev) {
    return dev->is_pcie ? dev->link_width : 0;
}

//==================== INFORMATION FUNCTIONS ====================

//Vendor name
const char* pci_vendor_name(u16 vendor_id) {
    switch (vendor_id) {
        case 0x8086: return "Intel";
        case 0x1022: return "AMD";
        case 0x10DE: return "NVIDIA";
        case 0x10EC: return "Realtek";
        case 0x1234: return "QEMU";
        case 0x1AF4: return "Red Hat";
        default: return "Unknown";
    }
}

//Class name
const char* pci_class_name(u8 class_code, u8 subclass) {
    switch (class_code) {
        case 0x01:
            switch (subclass) {
                case 0x00: return "SCSI Controller";
                case 0x01: return "IDE Controller";
                case 0x06: return "SATA Controller";
                default: return "Storage Controller";
            }
        case 0x02:
            switch (subclass) {
                case 0x00: return "Ethernet Controller";
                default: return "Network Controller";
            }
        case 0x03:
            switch (subclass) {
                case 0x00: return "VGA Controller";
                case 0x02: return "3D Controller";
                default: return "Display Controller";
            }
        case 0x06:
            switch (subclass) {
                case 0x00: return "Host Bridge";
                case 0x04: return "PCI-to-PCI Bridge";
                default: return "Bridge";
            }
        case 0x0C:
            switch (subclass) {
                case 0x03: return "USB Controller";
                default: return "Serial Controller";
            }
        default: return "Unknown Device";
    }
}

//Displaying device information
void pci_print(pci_device_t* dev) {
    if (!dev) return;
    
    terminal_printf("PCI %02X:%02X.%X: %04X:%04X [%02X:%02X] %s - %s\n",
           dev->bus, dev->slot, dev->function,
           dev->vendor_id, dev->device_id,
           dev->class_code, dev->subclass,
           pci_vendor_name(dev->vendor_id),
           pci_class_name(dev->class_code, dev->subclass));
    
    if (dev->is_pcie) {
        terminal_printf("  PCIe %s, Gen%d x%d\n",
               pcie_type_name(dev),
               dev->link_speed,
               dev->link_width);
    }
}

//Secure search for PCI device
pci_device_t* pci_find_class_safe(u8 class_code, u8 subclass) {
    //We limit the search to only existing tires
    for (u8 bus = 0; bus < 3; bus++) {  //bus 0-2 is usually sufficient
        for (u8 slot = 0; slot < 8; slot++) {  // slot 0-7
            for (u8 func = 0; func < 1; func++) {  //function 0 only
                
                //Checking if the device exists
                u32 vendor = pci_read_addr(bus, slot, func, 0x00);
                if (vendor == 0xFFFFFFFF || vendor == 0x00000000) {
                    continue;  //No device
                }
                
                //Reading the class/subclass
                u32 class_rev = pci_read_addr(bus, slot, func, 0x08);
                u8 found_class = (class_rev >> 24) & 0xFF;
                u8 found_subclass = (class_rev >> 16) & 0xFF;
                
                if (found_class == class_code && found_subclass == subclass) {
                    pci_device_t* dev = (pci_device_t*)malloc(sizeof(pci_device_t));
                    if (!dev) return NULL;
                    
                    memset(dev, 0, sizeof(pci_device_t));
                    dev->bus = bus;
                    dev->slot = slot;
                    dev->function = func;
                    dev->vendor_id = vendor & 0xFFFF;
                    dev->device_id = (vendor >> 16) & 0xFFFF;
                    
                    //Filling out BARs
                    for (int i = 0; i < 6; i++) {
                        dev->bars[i] = pci_read(dev, 0x10 + i * 4);
                    }
                    
                    return dev;
                }
            }
        }
    }
    return NULL;
}

pci_device_t* pci_get_first(void) {
    return pci_devices;
}

pci_device_t* pci_get_next(pci_device_t *dev) {
    if (!dev) return NULL;
    return dev->next;
}

u32 pci_get_count(void) {
    u32 count = 0;
    pci_device_t *dev = pci_devices;
    while (dev) {
        count++;
        dev = dev->next;
    }
    return count;
}
