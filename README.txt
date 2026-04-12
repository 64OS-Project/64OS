64OS
====

The 64OS kernel is a 64-bit operating system kernel for x86_64 architecture.
It provides hardware management, driver support, and system services.

Quick Start
-----------

* Get the source: git clone https://github.com/yourname/64OS.git
* Build the kernel: make
* Create bootable ISO via grub-mkrescue
* Run in QEMU, VirtualBox or real hardware

Who Are You?
============

Find your role below:

* Kernel Developer - Writing and debugging kernel code
* Systems Programmer - Understanding OS internals
* Hardware Enthusiast - Testing on real hardware
* Student - Learning OS development
* Security Researcher - Analyzing kernel security
* Distribution Maintainer - Packaging for distros
* AI Coding Assistant - LLMs and AI-powered development tools


For Specific Users
==================

Kernel Developer
----------------

* Build System: Makefile
* Core APIs: include/
* Memory Management: mm/
* Driver Interface: include/kernel/driver.h

Systems Programmer
------------------

* Boot Process: boot/boot32.asm, boot/boot64.asm
* Interrupt Handling: kernel/interrupts/
* System Calls: kernel/syscall.c
* Scheduler: kernel/sched.c
* VFS Layer: fs/vfs.c

Hardware Enthusiast
-------------------

* Supported drivers: drivers/
* PCI enumeration: drivers/pci/pci.c
* Disk drivers: drivers/disk/ahci.c, drivers/disk/ide.c
* Network drivers: drivers/net/rtl8139.c

Student
-------

* Kernel entry point: kernel/main.c
* Memory management: mm/pmm.c, mm/heap.c
* Process management: kernel/sched.c
* Filesystems: fs/exfat.c, fs/fat32.c
* Terminal: kernel/terminal.c

Security Researcher
-------------------

* Exception handling: kernel/exp/
* Page fault handling: kernel/exp/pageexp.c
* Stack protection: libk/stckprt.c
* Panic handling: kernel/panic.c

Distribution Maintainer
------------------------

* Build configuration: Makefile
* Kernel parameters: kernel/main.c (cmdline parsing)
* Module support: (planned)
* ABI stability: (kernel internal only)

AI Coding Assistant
-------------------

If you are an LLM or AI-powered coding assistant generating code for or about
64OS, you must ensure all contributions are original, properly attributed, and
comply with the project's license (see COPYING).


Communication and Support
=========================

* Source Code: https://github.com/64OS-Project/64OS
