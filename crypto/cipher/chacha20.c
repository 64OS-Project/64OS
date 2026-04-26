#include <crypto/api.h>
#include <crypto/internal.h>
#include <libk/string.h>

/*
 * ============================================================================= ChaCha20 Quarter Round ============================================================================
 */

#define CHACHA_QR(a, b, c, d) \
    a += b; d ^= a; d = CRYPTO_ROTL32(d, 16); \
    c += d; b ^= c; b = CRYPTO_ROTL32(b, 12); \
    a += b; d ^= a; d = CRYPTO_ROTL32(d, 8);  \
    c += d; b ^= c; b = CRYPTO_ROTL32(b, 7)

/*
 * ================================================================================================ Initialization of state =======================================================================================
 */

void crypto_chacha20_init(crypto_chacha20_ctx_t *ctx,
                          const u8 key[CRYPTO_CHACHA20_KEY_SIZE],
                          const u8 iv[CRYPTO_CHACHA20_IV_SIZE]) {
    const u32 constants[4] = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574
    };
    
    /*
 * Constant
 */
    ctx->state[0] = constants[0];
    ctx->state[1] = constants[1];
    ctx->state[2] = constants[2];
    ctx->state[3] = constants[3];
    
    /*
 * Key (32 bytes)
 */
    for (int i = 0; i < 8; i++) {
        ctx->state[4 + i] = (u32)key[i*4] |
                            (u32)key[i*4+1] << 8 |
                            (u32)key[i*4+2] << 16 |
                            (u32)key[i*4+3] << 24;
    }
    
    /*
 * Block counter (0)
 */
    ctx->state[12] = 0;
    
    /*
 * Nonce (12 bytes)
 */
    ctx->state[13] = (u32)iv[0] |
                     (u32)iv[1] << 8 |
                     (u32)iv[2] << 16 |
                     (u32)iv[3] << 24;
    ctx->state[14] = (u32)iv[4] |
                     (u32)iv[5] << 8 |
                     (u32)iv[6] << 16 |
                     (u32)iv[7] << 24;
    ctx->state[15] = (u32)iv[8] |
                     (u32)iv[9] << 8 |
                     (u32)iv[10] << 16 |
                     (u32)iv[11] << 24;
}

/*
 * =============================================================================== Generating a key stream block ================================================================================
 */

static void chacha20_block(crypto_chacha20_ctx_t *ctx, u8 *output) {
    u32 working[16];
    memcpy(working, ctx->state, sizeof(working));
    
    /*
 * 10 rounds (20 half rounds)
 */
    for (int i = 0; i < 10; i++) {
        CHACHA_QR(working[0], working[4], working[ 8], working[12]);
        CHACHA_QR(working[1], working[5], working[ 9], working[13]);
        CHACHA_QR(working[2], working[6], working[10], working[14]);
        CHACHA_QR(working[3], working[7], working[11], working[15]);
        CHACHA_QR(working[0], working[5], working[10], working[15]);
        CHACHA_QR(working[1], working[6], working[11], working[12]);
        CHACHA_QR(working[2], working[7], working[ 8], working[13]);
        CHACHA_QR(working[3], working[4], working[ 9], working[14]);
    }
    
    /*
 * Adding an initial state
 */
    for (int i = 0; i < 16; i++) {
        working[i] += ctx->state[i];
    }
    
    /*
 * Convert to bytes (little-endian)
 */
    for (int i = 0; i < 16; i++) {
        output[i*4]   = working[i] & 0xFF;
        output[i*4+1] = (working[i] >> 8) & 0xFF;
        output[i*4+2] = (working[i] >> 16) & 0xFF;
        output[i*4+3] = (working[i] >> 24) & 0xFF;
    }
    
    /*
 * Block counter increment
 */
    ctx->state[12]++;
    if (ctx->state[12] == 0) {
        ctx->state[13]++;
    }
}

/*
 * ============================================================================== Encryption/Decryption ================================================================================
 */

void crypto_chacha20_xor(crypto_chacha20_ctx_t *ctx, u8 *data, u32 len) {
    u8 keystream[64];
    u32 offset = 0;
    
    while (len > 0) {
        chacha20_block(ctx, keystream);
        
        u32 chunk = (len < 64) ? len : 64;
        for (u32 i = 0; i < chunk; i++) {
            data[offset + i] ^= keystream[i];
        }
        
        offset += chunk;
        len -= chunk;
    }
}

void crypto_chacha20_encrypt(crypto_chacha20_ctx_t *ctx, const u8 *src,
                             u8 *dst, u32 len) {
    memcpy(dst, src, len);
    crypto_chacha20_xor(ctx, dst, len);
}

void crypto_chacha20_decrypt(crypto_chacha20_ctx_t *ctx, const u8 *src,
                             u8 *dst, u32 len) {
    /*
 * ChaCha20 is symmetrical
 */
    crypto_chacha20_encrypt(ctx, src, dst, len);
}
