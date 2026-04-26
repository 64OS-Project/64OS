#include <kernel/types.h>
#include <libk/string.h>
#include <stdarg.h>
#include <libk/vsnprintf.h>
#include <mm/heap.h>

/*
 * =================== MEM ===================
 */
void *memcpy(void *dst_, const void *src_, sz n)
{
    unsigned char *dst = (unsigned char *)dst_;
    const unsigned char *src = (const unsigned char *)src_;
    void *ret = dst_;

    if (n == 0 || dst == src)
        return ret;

#if defined(__x86_64__) || defined(__i386__)
    /*
 * On x86/x86_64 rep movsb is very fast
 */
    asm volatile(
        "rep movsb"
        : "+D"(dst), "+S"(src), "+c"(n)
        :
        : "memory");
    return ret;
#else
    const sz W = sizeof(unsigned long);
    uptr dst_addr = (uptr)dst;
    uptr src_addr = (uptr)src;

    /*
 * Align dst to word boundary byte byte
 */
    while (n > 0 && (dst_addr & (W - 1)))
    {
        *dst++ = *src++;
        --n;
        dst_addr = (uptr)dst;
        src_addr = (uptr)src;
    }

    /*
 * If src alignment is equal to dst alignment, you can copy in words
 */
    if ((src_addr & (W - 1)) == (dst_addr & (W - 1)))
    {
        unsigned long *dw = (unsigned long *)dst;
        const unsigned long *sw = (const unsigned long *)src;
        sz words = n / W;

        /*
 * Unfolding a cycle of 4 words
 */
        while (words >= 4)
        {
            dw[0] = sw[0];
            dw[1] = sw[1];
            dw[2] = sw[2];
            dw[3] = sw[3];
            dw += 4;
            sw += 4;
            words -= 4;
        }
        while (words--)
        {
            *dw++ = *sw++;
        }

        /*
 * Update pointers and remaining bytes
 */
        dst = (unsigned char *)dw;
        src = (const unsigned char *)sw;
        n = n & (W - 1);
    }
    else
    {
        /*
 * If the alignments do not match, we leave everything byte-by-byte.
           This is safe on architectures that require alignment.
 */
    }

    /*
 * Tail bytes
 */
    while (n--)
    {
        *dst++ = *src++;
    }

    return ret;
#endif
}

void *memset(void *s, int c, sz n)
{
    unsigned char *p = (unsigned char *)s;
    unsigned char val = (unsigned char)c;
    void *ret = s;

    const sz W = sizeof(unsigned long);
    uptr addr = (uptr)p;

    /*
 * Align byte-by-byte to word boundaries
 */
    while (n > 0 && (addr & (W - 1)))
    {
        *p++ = val;
        --n;
        addr = (uptr)p;
    }

    /*
 * Fill in whole words
 */
    if (n >= W)
    {
        unsigned long word = val;
        word |= word << 8;
        word |= word << 16;
#ifdef __LP64__
        word |= word << 32;
#endif
        unsigned long *pw = (unsigned long *)p;
        while (n >= W)
        {
            *pw++ = word;
            n -= W;
        }
        p = (unsigned char *)pw;
    }

    /*
 * Remaining bytes
 */
    while (n--)
        *p++ = val;

    return ret;
}

int memcmp(const void *ptr1, const void *ptr2, sz num)
{
    const unsigned char *a = (const unsigned char *)ptr1;
    const unsigned char *b = (const unsigned char *)ptr2;

    for (sz i = 0; i < num; i++)
    {
        if (a[i] != b[i])
            return (int)a[i] - (int)b[i];
    }
    return 0;
}

void *memmove(void *dst0, const void *src0, sz n)
{
    if (n == 0 || dst0 == src0)
        return dst0;

    unsigned char *dst = (unsigned char *)dst0;
    const unsigned char *src = (const unsigned char *)src0;

    if (dst < src) /*
 * copy forward
 */
    {
        sz word = sizeof(uptr);
        while (n && ((uptr)dst & (word - 1)))
        {
            *dst++ = *src++;
            --n;
        }

        uptr *dw = (uptr *)dst;
        const uptr *sw = (const uptr *)src;
        while (n >= word)
        {
            *dw++ = *sw++;
            n -= word;
        }

        dst = (unsigned char *)dw;
        src = (const unsigned char *)sw;
        while (n--)
            *dst++ = *src++;
    }
    else /*
 * dst > src - copy back
 */
    {
        dst += n;
        src += n;

        sz word = sizeof(uptr);
        while (n && ((uptr)dst & (word - 1)))
        {
            *--dst = *--src;
            --n;
        }

        uptr *dw = (uptr *)dst;
        const uptr *sw = (const uptr *)src;
        while (n >= word)
        {
            *--dw = *--sw;
            n -= word;
        }

        dst = (unsigned char *)dw;
        src = (const unsigned char *)sw;
        while (n--)
            *--dst = *--src;
    }

    return dst0;
}

void *memmem(const void *haystack, sz haystack_len, const void *needle, sz needle_len) {
    const unsigned char *h = (const unsigned char *)haystack;
    const unsigned char *n = (const unsigned char *)needle;
    
    if (needle_len == 0) return (void *)haystack;
    if (haystack_len < needle_len) return NULL;
    
    for (sz i = 0; i <= haystack_len - needle_len; i++) {
        sz j;
        for (j = 0; j < needle_len; j++) {
            if (h[i + j] != n[j]) break;
        }
        if (j == needle_len) return (void *)(h + i);
    }
    return NULL;
}

/*
 * =================== STR ===================
 */
sz strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return p - s;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++))
        ;
    return dst;
}

char *strncpy(char *dst, const char *src, sz n)
{
    sz i = 0;
    for (; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d)
        d++;
    while ((*d++ = *src++))
        ;
    return dst;
}

int strcmp(const char *a, const char *b)
{
    while (*a == *b && *a != '\0')
    {
        a++;
        b++;
    }
    return *(unsigned char *)a - *(unsigned char *)b;
}

int strncmp(const char *a, const char *b, sz n)
{
    for (sz i = 0; i < n; i++)
    {
        if (a[i] != b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0')
            return 0;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    while (*s)
    {
        if (*s == (char)c)
            return (char *)s;
        s++;
    }
    if ((char)c == '\0')
        return (char *)s;
    return NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    while (*s)
    {
        if (*s == (char)c)
            last = s;
        s++;
    }
    if ((char)c == '\0')
        return (char *)s;
    return (char *)last;
}

char *strncat(char *dest, const char *src, sz n)
{
    char *d = dest;
    while (*d)
        ++d; /*
 * let's find the end dest
 */
    sz i = 0;
    while (i < n && src[i] != '\0')
    {
        d[i] = src[i];
        ++i;
    }
    d[i] = '\0';
    return dest;
}

char *strtok_r(char *str, const char *delim, char **saveptr)
{
    char *token;

    if (str)
        *saveptr = str;
    if (*saveptr == NULL)
        return NULL;

    //Skip leading delimiter characters
    char *start = *saveptr;
    while (*start && strchr(delim, *start))
        start++;
    if (*start == '\0')
    {
        *saveptr = NULL;
        return NULL;
    }

    //Find the end of the token
    token = start;
    char *p = start;
    while (*p && !strchr(delim, *p))
        p++;

    if (*p)
    {
        *p = '\0';
        *saveptr = p + 1;
    }
    else
    {
        *saveptr = NULL;
    }

    return token;
}

int nameeq(const char *a, const char *b, sz n)
{
    for (sz i = 0; i < n; ++i)
    {
        char ca = a[i], cb = b[i];
        if (!ca && !cb)
            return 1;
        if (ca != cb)
            return 0;
    }
    return 1;
}

static char* itoa(int value, char* str, int base) {
    char* ptr = str;
    char* ptr1 = str;
    char tmp_char;
    int tmp_value;
    
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    
    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[tmp_value - value * base];
    } while (value);
    
    *ptr-- = '\0';
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    return str;
}

char* utoa(unsigned int value, char* str, int base) {
    char* ptr = str;
    char* ptr1 = str;
    char tmp_char;
    unsigned int tmp_value;
    
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    
    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[tmp_value - value * base];
    } while (value);
    
    *ptr-- = '\0';
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    return str;
}

char *strstr(const char *haystack, const char *needle)
{
    if (!haystack || !needle) {
        return NULL;
    }
    
    //If needle is an empty string, return haystack
    if (*needle == '\0') {
        return (char *)haystack;
    }
    
    //Quick check in case needle is longer than haystack
    sz needle_len = strlen(needle);
    sz haystack_len = strlen(haystack);
    
    if (needle_len > haystack_len) {
        return NULL;
    }
    
    //Optimized algorithm with pre-check of the first character
    char first = needle[0];
    
    for (sz i = 0; i <= haystack_len - needle_len; i++) {
        //Quick first character check
        if (haystack[i] != first) {
            continue;
        }
        
        //Checking other characters
        sz j;
        for (j = 1; j < needle_len; j++) {
            if (haystack[i + j] != needle[j]) {
                break;
            }
        }
        
        //If all characters match, return a pointer
        if (j == needle_len) {
            return (char *)(haystack + i);
        }
    }
    
    return NULL;
}

int atoi(const char* str) {
    int result = 0;
    int sign = 1;
    
    if (!str) return 0;
    
    //Skip spaces
    while (*str == ' ' || *str == '\t') str++;
    
    //Sign Processing
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    //Converting numbers
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return result * sign;
}

//Converting a string to a long integer
long atol(const char* str) {
    long result = 0;
    int sign = 1;
    
    if (!str) return 0;
    
    //Skip spaces
    while (*str == ' ' || *str == '\t') str++;
    
    //Sign Processing
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    //Converting numbers
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return result * sign;
}

char *strdup(const char *s) {
    if (!s) return NULL;
    
    sz len = strlen(s) + 1;
    char *dup = (char *)malloc(len);
    if (!dup) return NULL;
    
    memcpy(dup, s, len);
    return dup;
}