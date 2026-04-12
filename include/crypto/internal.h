#ifndef CRYPTO_INTERNAL_H
#define CRYPTO_INTERNAL_H

#include <crypto/api.h>
#include <asm/cpu.h>

/*
 * =============================================================================== Internal macros (safe) ================================================================================
 */

/*
 * ROTate Left/Right
 */
#define CRYPTO_ROTL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define CRYPTO_ROTR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CRYPTO_ROTL64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))
#define CRYPTO_ROTR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

/*
 * Little/Big Endian conversion
 */
static inline u32 crypto_cpu_to_be32(u32 x) {
    #ifdef __x86_64__
    __asm__("bswap %0" : "+r"(x));
    #endif
    return x;
}

static inline u32 crypto_cpu_to_le32(u32 x) {
    return x;  /*
 * x86 — little-endian
 */
}

static inline u32 crypto_be32_to_cpu(u32 x) {
    #ifdef __x86_64__
    __asm__("bswap %0" : "+r"(x));
    #endif
    return x;
}

static inline u64 crypto_cpu_to_be64(u64 x) {
    #ifdef __x86_64__
    __asm__("bswap %0" : "+r"(x));
    #endif
    return x;
}

/*
 * Rotate for SHA
 */
#define SHA_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA_PARITY(x, y, z) ((x) ^ (y) ^ (z))

/*
 * ============================================================================== Internal functions for RNG ======================================================================================
 */

/*
 * Backup generator (ChaCha20-based)
 */
void crypto_rng_reseed(void);

#endif /*
 * CRYPTO_INTERNAL_H
 */
