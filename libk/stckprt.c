#include <kernel/types.h>
#include <asm/cpu.h>
#include <kernel/panic.h>

/*
 * Global guard that GCC reads
 */
uptr __stack_chk_guard = 0xBAAAD00Du;

/*
 * Simple output + kernel stop
 */
static void __attribute__((noreturn)) kstack_panic(void)
{
    intd();
    panic("STACK CANARY");
    for (;;) { halt(); }
    __builtin_unreachable();
}

/*
 * Called by GCC when canaries do not match
 */
void __attribute__((noreturn)) __stack_chk_fail(void)
{
    kstack_panic();
}

/*
 * Local version, on i386/ELF it is often called this way
 */
void __attribute__((noreturn)) __stack_chk_fail_local(void)
{
    kstack_panic();
}
