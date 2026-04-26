#ifndef CRYPTO_API_H
#define CRYPTO_API_H

#include <kernel/types.h>

/*
 * ============================================================================== CONSTANTS - MUST BE FIRST! ================================================================================
 */

/*
 * Hash sizes
 */
#define CRYPTO_MD5_DIGEST_SIZE      16
#define CRYPTO_SHA1_DIGEST_SIZE     20
#define CRYPTO_SHA256_DIGEST_SIZE   32
#define CRYPTO_SHA512_DIGEST_SIZE   64

/*
 * Block sizes
 */
#define CRYPTO_MD5_BLOCK_SIZE       64
#define CRYPTO_SHA1_BLOCK_SIZE      64
#define CRYPTO_SHA256_BLOCK_SIZE    64
#define CRYPTO_SHA512_BLOCK_SIZE    128

/*
 * AES Key Sizes
 */
#define CRYPTO_AES_KEY_SIZE_128     16
#define CRYPTO_AES_KEY_SIZE_192     24
#define CRYPTO_AES_KEY_SIZE_256     32
#define CRYPTO_AES_BLOCK_SIZE       16

/*
 * DES Key Sizes
 */
#define CRYPTO_DES_KEY_SIZE         8
#define CRYPTO_DES_BLOCK_SIZE       8

/*
 * ChaCha20
 */
#define CRYPTO_CHACHA20_KEY_SIZE    32
#define CRYPTO_CHACHA20_IV_SIZE     12
#define CRYPTO_CHACHA20_BLOCK_SIZE  64

/*
 * ================================================================================ Errors =======================================================================================
 */

#define CRYPTO_OK                   0
#define CRYPTO_ERR_INVALID_PARAM    -1
#define CRYPTO_ERR_INVALID_KEY_SIZE -2
#define CRYPTO_ERR_NOT_IMPLEMENTED  -3
#define CRYPTO_ERR_HARDWARE_FAIL    -4

/*
 * ============================================================================= MD5 ============================================================================
 */

typedef struct {
    u32 state[4];
    u64 count;
    u8 buffer[64];
} crypto_md5_ctx_t;

void crypto_md5_init(crypto_md5_ctx_t *ctx);
void crypto_md5_update(crypto_md5_ctx_t *ctx, const u8 *data, u32 len);
void crypto_md5_final(crypto_md5_ctx_t *ctx, u8 out[CRYPTO_MD5_DIGEST_SIZE]);
void crypto_md5(const u8 *data, u32 len, u8 out[CRYPTO_MD5_DIGEST_SIZE]);

/*
 * ============================================================================= SHA-1 ============================================================================
 */

typedef struct {
    u32 state[5];
    u64 count;
    u8 buffer[64];
} crypto_sha1_ctx_t;

void crypto_sha1_init(crypto_sha1_ctx_t *ctx);
void crypto_sha1_update(crypto_sha1_ctx_t *ctx, const u8 *data, u32 len);
void crypto_sha1_final(crypto_sha1_ctx_t *ctx, u8 out[CRYPTO_SHA1_DIGEST_SIZE]);
void crypto_sha1(const u8 *data, u32 len, u8 out[CRYPTO_SHA1_DIGEST_SIZE]);

/*
 * ============================================================================= SHA-256 ============================================================================
 */

typedef struct {
    u32 state[8];
    u64 count;
    u8 buffer[64];
} crypto_sha256_ctx_t;

void crypto_sha256_init(crypto_sha256_ctx_t *ctx);
void crypto_sha256_update(crypto_sha256_ctx_t *ctx, const u8 *data, u32 len);
void crypto_sha256_final(crypto_sha256_ctx_t *ctx, u8 out[CRYPTO_SHA256_DIGEST_SIZE]);
void crypto_sha256(const u8 *data, u32 len, u8 out[CRYPTO_SHA256_DIGEST_SIZE]);

/*
 * ============================================================================= ChaCha20 ============================================================================
 */

typedef struct {
    u32 state[16];
} crypto_chacha20_ctx_t;

void crypto_chacha20_init(crypto_chacha20_ctx_t *ctx, const u8 key[32], const u8 iv[12]);
void crypto_chacha20_encrypt(crypto_chacha20_ctx_t *ctx, const u8 *src, u8 *dst, u32 len);
void crypto_chacha20_decrypt(crypto_chacha20_ctx_t *ctx, const u8 *src, u8 *dst, u32 len);
void crypto_chacha20_xor(crypto_chacha20_ctx_t *ctx, u8 *data, u32 len);

/*
 * ============================================================================= RNG ============================================================================
 */

void crypto_rng_init(void);
int crypto_get_random_bytes(u8 *buf, u32 len);

/*
 * ============================================================================= Self-test ============================================================================
 */

int crypto_self_test(void);

#endif /*
 * CRYPTO_API_H
 */
