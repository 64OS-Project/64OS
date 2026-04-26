#ifndef PCI_H
#define PCI_H

#include <kernel/types.h>
#include <kernel/terminal.h>

//==================== GENERAL CONSTANTS ====================
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_INVALID_VENDOR 0xFFFF

#define PCI_CAP_ID_MSI     0x05
#define PCI_CAP_ID_MSIX    0x11

#define PCI_MSI_FLAGS_ENABLE        (1 << 16)
#define PCI_MSI_FLAGS_64BIT         (1 << 15)
#define PCI_MSI_FLAGS_MASKABLE      (1 << 14)
#define PCI_MSI_FLAGS_VECTOR_MASK   0xFFFF

// MSI address
#define PCI_MSI_ADDRESS_APIC_BASE   0xFEE00000

//==================== STRUCTURES ====================

//MSI capability structure
typedef struct {
    u8 cap_id;           // 0x05
    u8 next_ptr;
    u16 msg_control;     // control register
    u32 msg_addr;        // message address
    u32 msg_addr_hi;     // message upper address (64-bit)
    u16 msg_data;        // message data
    u32 mask_bits;       //mask bits (if available)
    u32 pending_bits;    // pending bits
} __attribute__((packed)) pci_msi_t;

//MSI-X capability structure
typedef struct {
    u8 cap_id;           // 0x11
    u8 next_ptr;
    u16 msg_control;     // control register
    u32 table_offset;    // BAR indicator + offset
    u32 pba_offset;      // PBA BAR indicator + offset
} __attribute__((packed)) pci_msix_t;

//Unified structure for PCI/PCIe devices
typedef struct pci_device {
    //Identification
    u8 bus;
    u8 slot;
    u8 function;
    
    //Standard PCI registers
    u16 vendor_id;
    u16 device_id;
    u16 command;
    u16 status;
    u8 revision_id;
    u8 prog_if;
    u8 subclass;
    u8 class_code;
    u8 cache_line;
    u8 latency_timer;
    u8 header_type;
    u8 bist;
    
    //BAR registers
    u64 bars[6];
    u64 bar_sizes[6];
    u8 bar_types[6];  // 0=32-bit, 1=64-bit, 2=I/O
    
    //Interrupts
    u8 interrupt_line;
    u8 interrupt_pin;
    
    //========== PCIe SPECIFIC FIELDS ==========
    bool is_pcie;
    
    // PCIe Capability
    u8 pcie_cap_offset;
    u8 pcie_type;
    u8 link_speed;
    u8 max_link_speed;
    u16 link_width;
    u16 max_link_width;
    u8 pcie_version;
    
    // MSI/MSI-X
    u8 msi_cap_offset;
    u8 msix_cap_offset;
    bool msi_enabled;
    bool msix_enabled;
    
    // For a linked list
    struct pci_device* next;
} pci_device_t;

//==================== UNIFIED FUNCTIONS ====================

//Initialization (works for both PCI and PCIe)
void pci_init(void);

//Read/write configuration
u32 pci_read(pci_device_t* dev, u8 offset);
void pci_write(pci_device_t* dev, u8 offset, u32 value);

//Device discovery
int pci_scan(void);
pci_device_t* pci_find(u16 vendor_id, u16 device_id);
pci_device_t* pci_find_class(u8 class_code, u8 subclass);

//Device management
void pci_enable(pci_device_t* dev);
void pci_enable_busmaster(pci_device_t* dev);
int pci_enable_msi(pci_device_t* dev, u8 vector, u32 apic_id);
int pci_enable_msix(pci_device_t* dev, int num_vectors, u8 vector_base, u32 apic_id);

//Information
void pci_print(pci_device_t* dev);
const char* pci_vendor_name(u16 vendor_id);
const char* pci_class_name(u8 class_code, u8 subclass);

//PCIe specific functions (only work if is_pcie == true)
bool pcie_is_active(pci_device_t* dev);
const char* pcie_type_name(pci_device_t* dev);
u8 pcie_get_speed(pci_device_t* dev);
u16 pcie_get_width(pci_device_t* dev);

pci_device_t* pci_find_class_safe(u8 class_code, u8 subclass);
u8 pci_find_cap(pci_device_t* dev, u8 cap_id);

pci_device_t* pci_get_first(void);
pci_device_t* pci_get_next(pci_device_t *dev);
u32 pci_get_count(void);


#endif // PCI_H
