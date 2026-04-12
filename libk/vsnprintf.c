#include <kernel/types.h>
#include <stdarg.h>
#include <libk/vsnprintf.h>
#include <libk/string.h>

/*
 * ============================================================================= Mini printf implementation (inspired by glibc) Supports: %d, %i, %u, %x, %X, %s, %c, %p, %% %ld, %lld, %lu, %llu, %lx, %llx width, zero-padding, left-align ============================================================================
 */

typedef struct {
    char *str;
    sz size;
    sz written;
} printf_state_t;

static inline void out_char(printf_state_t *state, char c) {
    if (state->written < state->size) {
        state->str[state->written] = c;
    }
    state->written++;
}

static void out_string(printf_state_t *state, const char *s, sz len) {
    if (len == (sz)-1) {
        while (*s) {
            out_char(state, *s++);
        }
    } else {
        for (sz i = 0; i < len; i++) {
            out_char(state, s[i]);
        }
    }
}

static void out_reverse(char *start, char *end) {
    while (start < end) {
        char tmp = *start;
        *start++ = *end;
        *end-- = tmp;
    }
}

static char *itoa_base(unsigned long long num, char *buf, int base, int upper) {
    const char *digits_lower = "0123456789abcdef";
    const char *digits_upper = "0123456789ABCDEF";
    const char *digits = upper ? digits_upper : digits_lower;
    char *p = buf;
    
    if (num == 0) {
        *p++ = '0';
    } else {
        while (num > 0) {
            *p++ = digits[num % base];
            num /= base;
        }
    }
    
    *p = '\0';
    
    // Reverse the string
    char *start = buf;
    char *end = p - 1;
    while (start < end) {
        char tmp = *start;
        *start++ = *end;
        *end-- = tmp;
    }
    
    return buf;
}

static void out_num(printf_state_t *state, unsigned long long num, int base,
                    int width, int zero_pad, int upper, int negative) {
    char buf[64];
    char *num_str = itoa_base(num, buf, base, upper);
    int num_len = strlen(num_str);
    int sign_len = negative ? 1 : 0;
    int total_len = num_len + sign_len;
    int pad = (width > total_len) ? width - total_len : 0;
    
    // Left align
    if (negative) {
        out_char(state, '-');
    }
    
    if (!zero_pad) {
        for (int i = 0; i < pad; i++) {
            out_char(state, ' ');
        }
        out_string(state, num_str, num_len);
    } else {
        for (int i = 0; i < pad; i++) {
            out_char(state, '0');
        }
        out_string(state, num_str, num_len);
    }
}

int vsnprintf(char *str, sz size, const char *fmt, va_list args) {
    printf_state_t state;
    state.str = str;
    state.size = size;
    state.written = 0;
    
    if (size == 0) {
        return 0;
    }
    
    while (*fmt && state.written < size) {
        if (*fmt != '%') {
            out_char(&state, *fmt++);
            continue;
        }
        
        fmt++; // skip '%'
        
        // Parse flags
        int left_align = 0;
        int zero_pad = 0;
        int width = 0;
        int is_long = 0;
        int is_long_long = 0;
        int is_short = 0;
        int is_char = 0;
        
        // Flag '-'
        if (*fmt == '-') {
            left_align = 1;
            fmt++;
        }
        
        // Flag '0'
        if (*fmt == '0') {
            zero_pad = 1;
            fmt++;
        }
        
        // Width
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        int precision = -1;
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                precision = precision * 10 + (*fmt - '0');
                fmt++;
            }
        }
        
        // Length modifiers
        if (*fmt == 'h') {
            fmt++;
            if (*fmt == 'h') {
                is_char = 1;
                fmt++;
            } else {
                is_short = 1;
            }
        } else if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') {
                is_long_long = 1;
                fmt++;
            } else {
                is_long = 1;
            }
        } else if (*fmt == 'z') {
            is_long = 1;  // size_t is long on x86_64
            fmt++;
        }
        
        // Specifier
        switch (*fmt) {
            case 's': {
                char *s = va_arg(args, char*);
                if (!s) s = "(null)";
                int len = 0;
                while (s[len]) len++;
                
                if (!left_align) {
                    for (int i = 0; i < width - len && state.written < size; i++) {
                        out_char(&state, ' ');
                    }
                }
                out_string(&state, s, -1);
                if (left_align) {
                    for (int i = 0; i < width - len && state.written < size; i++) {
                        out_char(&state, ' ');
                    }
                }
                fmt++;
                break;
            }
            
            case 'c': {
                char c = (char)va_arg(args, int);
                if (!left_align) {
                    for (int i = 0; i < width - 1 && state.written < size; i++) {
                        out_char(&state, ' ');
                    }
                }
                out_char(&state, c);
                if (left_align) {
                    for (int i = 0; i < width - 1 && state.written < size; i++) {
                        out_char(&state, ' ');
                    }
                }
                fmt++;
                break;
            }
            
            case 'd':
            case 'i': {
                long long n;
                if (is_long_long) {
                    n = va_arg(args, long long);
                } else if (is_long) {
                    n = va_arg(args, long);
                } else if (is_short) {
                    n = (short)va_arg(args, int);
                } else if (is_char) {
                    n = (signed char)va_arg(args, int);
                } else {
                    n = va_arg(args, int);
                }
                
                int negative = (n < 0);
                unsigned long long un = negative ? -(unsigned long long)n : (unsigned long long)n;
                
                if (negative) {
                    width--;
                }
                
                out_num(&state, un, 10, width, zero_pad, 0, negative);
                fmt++;
                break;
            }

            case 'f': {
                double n = va_arg(args, double);
                int int_part = (int)n;
                int frac_part;
    
                //Use precision or 2 by default
                int prec = (precision < 0) ? 2 : precision;
    
                //Scale to get the fractional part
                double frac = n - int_part;
                if (frac < 0) frac = -frac;
    
                //Rounding
                double multiplier = 1.0;
                for (int i = 0; i < prec; i++) multiplier *= 10.0;
                frac_part = (int)(frac * multiplier + 0.5);
    
                //Handle rounding up (eg 0.999 -> 1.00)
                if (frac_part >= (int)multiplier) {
                    frac_part = 0;
                    int_part++;
                }
    
                char int_buf[32];
                char *int_str = itoa_base(int_part < 0 ? -int_part : int_part, int_buf, 10, 0);
    
                if (int_part < 0) {
                    out_char(&state, '-');
                }
    
                out_string(&state, int_str, -1);
                out_char(&state, '.');
    
                //Output the fractional part with leading zeros
                char frac_buf[16];
                char *frac_str = itoa_base(frac_part, frac_buf, 10, 0);
                int frac_len = strlen(frac_str);
    
                for (int i = 0; i < prec - frac_len; i++) {
                    out_char(&state, '0');
                }
                out_string(&state, frac_str, -1);
                fmt++;
                break;
            }
            
            case 'u': {
                unsigned long long n;
                if (is_long_long) {
                    n = va_arg(args, unsigned long long);
                } else if (is_long) {
                    n = va_arg(args, unsigned long);
                } else if (is_short) {
                    n = (unsigned short)va_arg(args, unsigned int);
                } else if (is_char) {
                    n = (unsigned char)va_arg(args, unsigned int);
                } else {
                    n = va_arg(args, unsigned int);
                }
                out_num(&state, n, 10, width, zero_pad, 0, 0);
                fmt++;
                break;
            }
            
            case 'x':
            case 'X': {
                unsigned long long n;
                if (is_long_long) {
                    n = va_arg(args, unsigned long long);
                } else if (is_long) {
                    n = va_arg(args, unsigned long);
                } else if (is_short) {
                    n = (unsigned short)va_arg(args, unsigned int);
                } else if (is_char) {
                    n = (unsigned char)va_arg(args, unsigned int);
                } else {
                    n = va_arg(args, unsigned int);
                }
                out_num(&state, n, 16, width, zero_pad, (*fmt == 'X'), 0);
                fmt++;
                break;
            }
            
            case 'p': {
                unsigned long long n = (unsigned long long)va_arg(args, void*);
                out_string(&state, "0x", 2);
                out_num(&state, n, 16, 16, 1, 0, 0);
                fmt++;
                break;
            }
            
            case '%': {
                out_char(&state, '%');
                fmt++;
                break;
            }
            
            default:
                out_char(&state, '%');
                if (*fmt) {
                    out_char(&state, *fmt);
                    fmt++;
                }
                break;
        }
    }
    
    // Null terminate
    if (state.written < size) {
        str[state.written] = '\0';
    } else {
        str[size - 1] = '\0';
    }
    
    return state.written;
}

int snprintf(char *str, sz size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int result = vsnprintf(str, size, fmt, args);
    va_end(args);
    return result;
}

int sprintf(char *str, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int result = vsnprintf(str, 0x7FFFFFFF, fmt, args);
    va_end(args);
    return result;
}
