#include <kernel/pageexp.h>
#include <kernel/sched.h>
#include <kernel/panic.h>
#include <kernel/types.h>

void handle_page_fault(u64 cr2, u64 error_code,
                       u64 rip, u64 cs)
{
    (void)cr2; /*
 * used when necessary for diagnostics
 */
    (void)error_code;
    (void)rip;

    if (cs & 3)
    {
        task_exit(EXIT_SIGSEGV);
    }
    else
    {
        panic("KERNEL_PAGE_FAULT");
    }
}

void handle_gpf(u64 error_code, u64 rip, u64 cs)
{
    (void)error_code;
    (void)rip;

    if (cs & 3)
    {
        task_exit(EXIT_SIGSEGV);
    }
    else
    {
        panic("KERNEL_GENERAL_PROTECTION_FAULT");
    }
}
