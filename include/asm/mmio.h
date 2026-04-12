#ifndef MMIO_H
#define MMIO_H

static inline u8 mmio_read8(volatile void* addr) {
    return *(volatile u8*)addr;
}

static inline u16 mmio_read16(volatile void* addr) {
    return *(volatile u16*)addr;
}

static inline u32 mmio_read32(volatile void* addr) {
    return *(volatile u32*)addr;
}

static inline u64 mmio_read64(volatile void* addr) {
    return *(volatile u64*)addr;
}

static inline void mmio_write8(volatile void* addr, u8 value) {
    *(volatile u8*)addr = value;
}

static inline void mmio_write16(volatile void* addr, u16 value) {
    *(volatile u16*)addr = value;
}

static inline void mmio_write32(volatile void* addr, u32 value) {
    *(volatile u32*)addr = value;
}

static inline void mmio_write64(volatile void* addr, u64 value) {
    *(volatile u64*)addr = value;
}

#define mmio_read(addr) mmio_read32(addr)
#define mmio_write(addr, value) mmio_write32(addr, value)

#endif