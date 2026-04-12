#ifndef STRING_H
#define STRING_H

#include <kernel/types.h>
#include <stdarg.h>

void *memcpy(void *dst, const void *src, sz n);
void *memset(void *s, int c, sz n);
int memcmp(const void *ptr1, const void *ptr2, sz num);
void *memmove(void *dst0, const void *src0, sz n);

sz strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, sz n);
char *strcat(char *dst, const char *src);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, sz n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strncat(char *dest, const char *src, sz n);
char *strtok_r(char *str, const char *delim, char **saveptr);
int nameeq(const char *a, const char *b, sz n);
int sprintf(char* str, const char* format, ...);
int snprintf(char* str, sz size, const char* format, ...);
int vsprintf(char* str, const char* format, va_list args);
int vsnprintf(char* str, sz size, const char* format, va_list args);
char *strstr(const char *haystack, const char *needle);

int atoi(const char* str);
long atol(const char* str);

char* utoa(unsigned int value, char* str, int base);

#endif
