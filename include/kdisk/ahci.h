#ifndef AHCI_H
#define AHCI_H

#include <kernel/types.h>

// ==================== FIS TYPES ====================
#define FIS_TYPE_REG_H2D    0x27
#define FIS_TYPE_REG_D2H    0x34
#define FIS_TYPE_DMA_ACT    0x39
#define FIS_TYPE_DMA_SETUP  0x41
#define FIS_TYPE_DATA       0x46
#define FIS_TYPE_BIST       0x58
#define FIS_TYPE_PIO_SETUP  0x5F
#define FIS_TYPE_DEV_BITS   0xA1

// ==================== SATA SIGNATURES ====================
#define SATA_SIG_SATA       0x00000101
#define SATA_SIG_ATAPI      0xEB140101
#define SATA_SIG_SEMB       0xC33C0101
#define SATA_SIG_PM         0x96690101

// ==================== GLOBAL HBA REGISTERS ====================
#define AHCI_GHC_HR         (1 << 0)
#define AHCI_GHC_IE         (1 << 1)
#define AHCI_GHC_ENABLE     (1 << 31)

#define AHCI_CAP_S64A       (1 << 31)
#define AHCI_CAP_NCQ        (1 << 30)
#define AHCI_CAP_SSS        (1 << 27)
#define AHCI_CAP_SALP       (1 << 26)
#define AHCI_CAP_FBSS       (1 << 16)
#define AHCI_CAP_SSC        (1 << 14)
#define AHCI_CAP_PSC        (1 << 13)

#define AHCI_CAP2_NVMHCI    (1 << 1)
#define AHCI_CAP2_BOHC      (1 << 0)

#define AHCI_BOHC_BIOS_BUSY     (1 << 4)
#define AHCI_BOHC_OS_OWNERSHIP  (1 << 3)

// ==================== PORT REGISTERS ====================
#define HBA_PxCMD_ST        0x0001
#define HBA_PxCMD_SUD       0x0002
#define HBA_PxCMD_POD       0x0004
#define HBA_PxCMD_FRE       0x0010
#define HBA_PxCMD_FR        0x4000
#define HBA_PxCMD_CR        0x8000
#define HBA_PxCMD_ASP       0x4000000
#define HBA_PxCMD_ICC       0xF0000000
#define HBA_PxCMD_ICC_ACTIVE (1 << 28)

#define HBA_PxIS_TFES       (1 << 30)

#define HBA_PxSSTS_DET      0x0F
#define HBA_PxSSTS_DET_INIT     1
#define HBA_PxSSTS_DET_PRESENT  3

#define HBA_PORT_IPM_ACTIVE 1

// ==================== SCTL ====================
#define SCTL_PORT_DET_INIT      0x1
#define SCTL_PORT_IPM_NOPART    0x100
#define SCTL_PORT_IPM_NOSLUM    0x200
#define SCTL_PORT_IPM_NODSLP    0x400

// ==================== ATA COMMANDS ====================
#define ATA_CMD_READ_DMA_EX     0x25
#define ATA_CMD_WRITE_DMA_EX    0x35
#define ATA_CMD_IDENTIFY        0xEC

#define ATA_DEV_BUSY            0x80
#define ATA_DEV_DRQ             0x08

// ==================== FIS STRUCTURES ====================

typedef struct {
    u8  fis_type;
    u8  pmport : 4;
    u8  rsv0   : 3;
    u8  c      : 1;
    u8  command;
    u8  featurel;
    u8  lba0;
    u8  lba1;
    u8  lba2;
    u8  device;
    u8  lba3;
    u8  lba4;
    u8  lba5;
    u8  featureh;
    u8  countl;
    u8  counth;
    u8  icc;
    u8  control;
    u8  rsv1[4];
} __attribute__((packed)) fis_reg_h2d_t;

typedef struct {
    u8  fis_type;
    u8  pmport : 4;
    u8  rsv0   : 2;
    u8  i      : 1;
    u8  rsv1   : 1;
    u8  status;
    u8  error;
    u8  lba0;
    u8  lba1;
    u8  lba2;
    u8  device;
    u8  lba3;
    u8  lba4;
    u8  lba5;
    u8  rsv2;
    u8  countl;
    u8  counth;
    u8  rsv3[2];
    u8  rsv4[4];
} __attribute__((packed)) fis_reg_d2h_t;

typedef struct {
    u8  fis_type;
    u8  pmport : 4;
    u8  rsv0   : 4;
    u8  rsv1[2];
    u32 data[1];
} __attribute__((packed)) fis_data_t;

typedef struct {
    u8  fis_type;
    u8  pmport : 4;
    u8  rsv0   : 1;
    u8  d      : 1;
    u8  i      : 1;
    u8  rsv1   : 1;
    u8  status;
    u8  error;
    u8  lba0;
    u8  lba1;
    u8  lba2;
    u8  device;
    u8  lba3;
    u8  lba4;
    u8  lba5;
    u8  rsv2;
    u8  countl;
    u8  counth;
    u8  rsv3;
    u8  e_status;
    u16 tc;
    u8  rsv4[2];
} __attribute__((packed)) fis_pio_setup_t;

typedef struct {
    u8  fis_type;
    u8  pmport : 4;
    u8  rsv0   : 1;
    u8  d      : 1;
    u8  i      : 1;
    u8  a      : 1;
    u8  rsved[2];
    u64 dma_buffer_id;
    u32 rsvd;
    u32 dma_buf_offset;
    u32 transfer_count;
    u32 resvd;
} __attribute__((packed)) fis_dma_setup_t;

// ==================== HBA STRUCTURES ====================

typedef volatile struct {
    u32 clb;
    u32 clbu;
    u32 fb;
    u32 fbu;
    u32 is;
    u32 ie;
    u32 cmd;
    u32 rsv0;
    u32 tfd;
    u32 sig;
    u32 ssts;
    u32 sctl;
    u32 serr;
    u32 sact;
    u32 ci;
    u32 sntf;
    u32 fbs;
    u32 rsv1[11];
    u32 vendor[4];
} __attribute__((packed)) hba_port_t;

typedef volatile struct {
    u32 cap;
    u32 ghc;
    u32 is;
    u32 pi;
    u32 vs;
    u32 ccc_ctl;
    u32 ccc_pts;
    u32 em_loc;
    u32 em_ctl;
    u32 cap2;
    u32 bohc;
    u8  rsv[0xA0 - 0x2C];
    u8  vendor[0x100 - 0xA0];
    hba_port_t ports[32];
} __attribute__((packed)) hba_mem_t;

typedef volatile struct {
    fis_dma_setup_t  dsfis;
    u8               pad0[4];
    fis_pio_setup_t  psfis;
    u8               pad1[12];
    fis_reg_d2h_t    rfis;
    u8               pad2[4];
    u8               sdbfis[8];
    u8               ufis[64];
    u8               rsv[0x100 - 0xA0];
} __attribute__((packed)) hba_fis_t;

typedef struct {
    u8  cfl   : 5;
    u8  a     : 1;
    u8  w     : 1;
    u8  p     : 1;
    u8  r     : 1;
    u8  b     : 1;
    u8  c     : 1;
    u8  rsv0  : 1;
    u8  pmp   : 4;
    u16 prdtl;
    u32 prdbc;
    u32 ctba;
    u32 ctbau;
    u32 rsv1[4];
} __attribute__((packed)) hba_cmd_header_t;

typedef struct {
    u32 dba;
    u32 dbau;
    u32 rsv0;
    u32 dbc   : 22;
    u32 rsv1  : 9;
    u32 i     : 1;
} __attribute__((packed)) hba_prdt_entry_t;

typedef struct {
    u8  cfis[64];
    u8  acmd[16];
    u8  rsv[48];
    hba_prdt_entry_t prdt_entry[1];
} __attribute__((packed)) hba_cmd_tbl_t;

// ==================== PORT STATUS ====================
typedef enum {
    AHCI_PORT_UNINITIALIZED = 0,
    AHCI_PORT_ERROR = 1,
    AHCI_PORT_ACTIVE = 2,
} ahci_port_status_t;

// ==================== PORT STRUCTURE ====================
typedef struct ahci_port {
    hba_port_t* regs;
    hba_cmd_header_t* cmd_list;
    hba_fis_t* fis;
    hba_cmd_tbl_t* cmd_tables[8];
    
    u64 phys_buffers[8];
    void* virt_buffers[8];
    volatile int buffer_locks[8];
    volatile int buffer_semaphore;
    volatile int port_lock;
    
    ahci_port_status_t status;
    u32 sector_size;
    u64 total_sectors;
    int supports_lba48;
    int port_num;
} ahci_port_t;

// ==================== PUBLIC FUNCTIONS ====================
int ahci_init(void);
int ahci_port_read(ahci_port_t* port, u64 lba, u32 count, void* buffer);
int ahci_port_write(ahci_port_t* port, u64 lba, u32 count, const void* buffer);
ahci_port_t* ahci_get_port(int port_num);

#endif
