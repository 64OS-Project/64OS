#ifndef LIBC_VSNPRINTF_H
#define LIBC_VSNPRINTF_H

#include <kernel/types.h>
#include <stdarg.h>

int vsnprintf(char *str, sz size, const char *format, va_list args);
int snprintf(char *str, sz size, const char *format, ...);
int sprintf(char *str, const char *format, ...);

#endif
