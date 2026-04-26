#ifndef KERNEL_PAGE_EXCEPTION_H
#define KERNEL_PAGE_EXCEPTION_H

#include <kernel/types.h>

#define EXIT_SIGSEGV (-11) /*
 * Segmentation Violation - going beyond memory limits
 */
#define EXIT_SIGBUS (-7)   /*
 * Bus Error - incorrect access to the bus/port
 */

void handle_page_fault(u64 cr2, u64 error_code,
                       u64 rip, u64 cs);
void handle_gpf(u64 error_code, u64 rip, u64 cs);

#endif
