#ifndef MATH_H
#define MATH_H

#include <kernel/types.h>

//Absolute value for integers
static inline int abs(int n) {
    return (n < 0) ? -n : n;
}

static inline long labs(long n) {
    return (n < 0) ? -n : n;
}

static inline long long llabs(long long n) {
    return (n < 0) ? -n : n;
}

//Absolute value for floating point numbers
static inline float fabsf(float x) {
    union {
        float f;
        u32 i;
    } u = {x};
    u.i &= 0x7fffffff;
    return u.f;
}

static inline double fabs(double x) {
    union {
        double d;
        u64 i;
    } u = {x};
    u.i &= 0x7fffffffffffffffULL;
    return u.d;
}

//Maximum and minimum
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

//Limit value
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

//Number sign
static inline int sign(int x) {
    return (x > 0) - (x < 0);
}

static inline int signf(float x) {
    return (x > 0.0f) - (x < 0.0f);
}

//=============================================================================
//Square root (sqrt)
//=============================================================================

//Fast inverse square root (for float)
static inline float inv_sqrtf(float x) {
    float xhalf = 0.5f * x;
    int i = *(int*)&x;
    i = 0x5f3759df - (i >> 1);
    x = *(float*)&i;
    x = x * (1.5f - xhalf * x * x);
    return x;
}

//Square root for float (Newton's method)
static inline float sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    
    float guess = x;
    float better = (guess + x / guess) * 0.5f;
    
    //Iterations until convergence
    for (int i = 0; i < 5; i++) {
        better = (guess + x / guess) * 0.5f;
        if (better > guess - 0.0001f && better < guess + 0.0001f) break;
        guess = better;
    }
    
    return better;
}

//Square root for double
static inline double sqrt(double x) {
    if (x <= 0.0) return 0.0;
    
    double guess = x;
    double better;
    
    for (int i = 0; i < 8; i++) {
        better = (guess + x / guess) * 0.5;
        if (better > guess - 0.0000001 && better < guess + 0.0000001) break;
        guess = better;
    }
    
    return better;
}

//Square root for integers (integer)
static inline u32 isqrt(u32 x) {
    if (x <= 1) return x;
    
    u32 guess = x / 2;
    u32 better;
    
    for (int i = 0; i < 10; i++) {
        better = (guess + x / guess) / 2;
        if (better >= guess) break;
        guess = better;
    }
    
    return guess;
}

//=============================================================================
//Power functions
//=============================================================================

//Fast exponentiation (integer)
static inline int ipow(int base, int exp) {
    int result = 1;
    while (exp > 0) {
        if (exp & 1) result *= base;
        base *= base;
        exp >>= 1;
    }
    return result;
}

//Degree for float
static inline float powf(float base, int exp) {
    float result = 1.0f;
    int negative = (exp < 0);
    
    if (negative) exp = -exp;
    
    while (exp > 0) {
        if (exp & 1) result *= base;
        base *= base;
        exp >>= 1;
    }
    
    return negative ? 1.0f / result : result;
}

//=============================================================================
//Trigonometric functions (Taylor series approximation)
//=============================================================================

static inline float sinf(float x) {
    //Reducing the angle to [-π, π]
    const float pi = 3.14159265f;
    const float two_pi = 2.0f * pi;
    
    while (x > pi) x -= two_pi;
    while (x < -pi) x += two_pi;
    
    //Taylor series: sin(x) = x - x^3/6 + x^5/120 - x^7/5040
    float x2 = x * x;
    float x3 = x2 * x;
    float x5 = x3 * x2;
    float x7 = x5 * x2;
    
    return x - x3 / 6.0f + x5 / 120.0f - x7 / 5040.0f;
}

static inline float cosf(float x) {
    //Reducing the angle to [-π, π]
    const float pi = 3.14159265f;
    const float two_pi = 2.0f * pi;
    
    while (x > pi) x -= two_pi;
    while (x < -pi) x += two_pi;
    
    //Taylor series: cos(x) = 1 - x^2/2 + x^4/24 - x^6/720
    float x2 = x * x;
    float x4 = x2 * x2;
    float x6 = x4 * x2;
    
    return 1.0f - x2 / 2.0f + x4 / 24.0f - x6 / 720.0f;
}

static inline float tanf(float x) {
    return sinf(x) / cosf(x);
}

//=============================================================================
//Exponent and logarithms
//=============================================================================

static inline float expf(float x) {
    float result = 1.0f;
    float term = 1.0f;
    
    //Taylor series: e^x = 1 + x + x^2/2! + x^3/3! +...
    for (int i = 1; i < 10; i++) {
        term *= x / i;
        result += term;
    }
    
    return result;
}

static inline float logf(float x) {
    if (x <= 0.0f) return -1.0f;
    
    //Natural logarithm via approximation
    u32 bits = *(u32*)&x;
    int exponent = ((bits >> 23) & 0xFF) - 127;
    float mantissa = 1.0f + ((bits & 0x7FFFFF) / (float)(1 << 23));
    
    // log(x) = log(1.m * 2^e) = log(1.m) + e * log(2)
    const float log2 = 0.69314718f;
    
    //Approximation log(1.m)
    float m1 = mantissa - 1.0f;
    float log_m = m1 - m1*m1/2 + m1*m1*m1/3 - m1*m1*m1*m1/4;
    
    return log_m + exponent * log2;
}

#endif // MATH_H