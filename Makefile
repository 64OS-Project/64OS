CC      := gcc
LD      := ld
AS      := nasm
QEMU    := qemu-system-x86_64

# Флаги компилятора
BASE_CFLAGS := -m64 -g -fno-builtin -Iinclude -O2 -ffreestanding -fno-stack-protector -mno-red-zone -mno-mmx -mno-avx -mno-80387 -mno-fp-ret-in-387 -mcmodel=kernel -fno-pic -fno-pie -fstack-protector-strong -fno-allow-store-data-races -fno-delete-null-pointer-checks -fno-optimize-sibling-calls -fno-common -fno-strict-aliasing -Wno-address-of-packed-member -Wno-unused-parameter -fno-asynchronous-unwind-tables -freorder-blocks-algorithm=stc -fcf-protection=full -mindirect-branch=thunk-extern -fpartial-inlining 
DEBUG_CFLAGS := -m64 -g -O0 -DDEBUG

# Флаги линковки
LDFLAGS  := -m elf_x86_64 -T link.ld

# Флаги ассемблера
ASMFLAGS       := -f elf64
ASMFLAGS_DEBUG := -f elf64 -g -F dwarf

# Источники
SRCS_AS := boot/boot32.asm boot/boot64.asm kernel/interrupts/isr_stubs.asm kernel/time/timer_isr.asm drivers/ps2kbd/kbd_isr.asm drivers/disk/ide_pri_irq.asm drivers/disk/ide_sec_irq.asm kernel/interrupts/except/gpf.asm kernel/interrupts/except/pf.asm kernel/interrupts/except/df.asm kernel/interrupts/except/mc.asm kernel/interrupts/except/ud.asm
SRCS_C  := kernel/main.c boot/mb2p/mb2p_fnc.c boot/mb2p/mb2p_parse.c libk/string.c libk/stckprt.c drivers/fb/fbbase.c drivers/fb/fbprim.c drivers/fb/fbtextrender.c kernel/driver.c kernel/panic.c drivers/acpi/acpifnc.c drivers/acpi/acpipwr.c drivers/apic/lapic.c drivers/apic/ioapic.c kernel/time/bcd2utime.c kernel/time/clock.c kernel/time/delay.c kernel/time/rdcmos.c kernel/time/rtc.c kernel/time/timer.c kernel/interrupts/idt.c drivers/ps2kbd/ps2kbd.c kernel/mm/pmm.c kernel/mm/heap.c kernel/paging.c kernel/terminal.c kernel/cmd.c drivers/pci/pci.c drivers/disk/ide.c drivers/disk/blockdev.c drivers/disk/ahci.c libk/vsnprintf.c fs/vfs.c fs/fat32.c fs/exfat.c fs/devfs/devfs.c fs/devfs/std.c fs/devfs/blk.c kernel/tss.c kernel/sched.c kernel/findroot.c drivers/pci/hda.c kernel/biosdev.c drivers/disk/cdrom.c kernel/partition.c kernel/fileio.c kernel/cmd/builtin.c kernel/cmd/disk.c kernel/cmd/filesystem.c kernel/cmd/part.c kernel/cmd/root.c fs/procfs.c kernel/smp.c kernel/findcpus.c kernel/syscall.c kernel/exp/pageexp.c kernel/exp/df.c kernel/exp/mc.c kernel/exp/ud.c crypto/random.c crypto/hash/md5.c crypto/hash/sha1.c crypto/hash/sha256.c crypto/cipher/chacha20.c crypto/selftest.c kernel/cmd/process.c kernel/net/arp.c kernel/net/ethernet.c kernel/net/icmp.c kernel/net/ipv4.c kernel/net/ipv6.c kernel/net/net.c kernel/net/tcp.c kernel/net/udp.c kernel/net/dhcp.c drivers/net/rtl8139.c
SRCS_S  := libk/retpoline.S
# Объекты
ASM_OBJS := $(patsubst %.asm,build/%.asm.o,$(SRCS_AS))
C_OBJS   := $(patsubst %.c,build/%.c.o,$(SRCS_C))
S_OBJS   := $(patsubst %.S,build/%.S.o,$(SRCS_S))
OBJECTS  := $(ASM_OBJS) $(C_OBJS) $(S_OBJS)

BUILD_KERNEL := build/kernel.elf
QEMU_OPTS ?=

.PHONY: all clean builddir run debug

all: builddir $(BUILD_KERNEL)

builddir:
	@mkdir -p build

# Правила для asm
build/%.asm.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASMFLAGS) $< -o $@

# Правила для c
build/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(BASE_CFLAGS) $(EXTRA_CFLAGS) -c $< -o $@

# Правила для .S (GNU as)
build/%.S.o: %.S
	@mkdir -p $(dir $@)
	gcc -c -x assembler-with-cpp $< -o $@

$(BUILD_KERNEL): $(OBJECTS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

# debug-сборка: подменяем флаги
debug: EXTRA_CFLAGS=$(DEBUG_CFLAGS)
debug: ASMFLAGS=$(ASMFLAGS_DEBUG)
debug: all
	$(QEMU) -kernel $(BUILD_KERNEL) -serial stdio $(QEMU_OPTS)

run: all
	$(QEMU) -kernel $(BUILD_KERNEL) $(QEMU_OPTS)

clean:
	rm -rf build
