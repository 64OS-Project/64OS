#ifndef TYPES_H
#define TYPES_H

#define NULL ((void *)0)

typedef enum {
    false = 0,
    true = 1
} bool;

#define U8_MAX   0xFF
#define U16_MAX  0xFFFF
#define U32_MAX  0xFFFFFFFF
#define U64_MAX  0xFFFFFFFFFFFFFFFFULL

#define I8_MAX   0x7F
#define I16_MAX  0x7FFF
#define I32_MAX  0x7FFFFFFF
#define I64_MAX  0x7FFFFFFFFFFFFFFFLL

#define I8_MIN   (-I8_MAX - 1)
#define I16_MIN  (-I16_MAX - 1)
#define I32_MIN  (-I32_MAX - 1)
#define I64_MIN  (-I64_MAX - 1)

typedef signed char i8;
typedef unsigned char u8;
typedef signed short i16;
typedef unsigned short u16;
typedef signed int i32;
typedef unsigned int u32;
typedef long long i64;
typedef unsigned long long u64;

#if defined(__x86_64__)
    typedef long iptr;
    typedef unsigned long uptr;
#elif defined(__i386__)
    typedef int iptr;
    typedef unsigned int uptr; 
#endif

typedef unsigned long sz;

typedef iptr diff;
typedef int wch;

#define offsetof(TYPE, MEMBER) ((sz)&(((TYPE *)0)->MEMBER))

#endif