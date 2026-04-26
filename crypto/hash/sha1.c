#include <crypto/api.h>
#include <crypto/internal.h>
#include <libk/string.h>

/*
 * ============================================================================== SHA-1 constants =================================================================================
 */

static const u32 SHA1_INIT[5] = {
    0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0
};

#define SHA1_K0 0x5a827999
#define SHA1_K1 0x6ed9eba1
#define SHA1_K2 0x8f1bbcdc
#define SHA1_K3 0xca62c1d6

#define SHA1_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define SHA1_F0(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define SHA1_F1(x, y, z) ((x) ^ (y) ^ (z))
#define SHA1_F2(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define SHA1_F3(x, y, z) ((x) ^ (y) ^ (z))

/*
 * ============================================================================== Main function of SHA-1 ==================================================================================
 */

static void sha1_transform(u32 *state, const u8 *block) {
    u32 w[80];
    u32 a, b, c, d, e;
    
    /*
 * Prepare message schedule
 */
    for (int i = 0; i < 16; i++) {
        w[i] = (u32)block[i*4] << 24 |
               (u32)block[i*4+1] << 16 |
               (u32)block[i*4+2] << 8 |
               (u32)block[i*4+3];
    }
    
    for (int i = 16; i < 80; i++) {
        w[i] = SHA1_ROTL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }
    
    /*
 * Initialize working variables
 */
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    
    /*
 * Main loop
 */
    for (int i = 0; i < 80; i++) {
        u32 temp;
        if (i < 20) {
            temp = SHA1_ROTL(a, 5) + SHA1_F0(b, c, d) + e + w[i] + SHA1_K0;
        } else if (i < 40) {
            temp = SHA1_ROTL(a, 5) + SHA1_F1(b, c, d) + e + w[i] + SHA1_K1;
        } else if (i < 60) {
            temp = SHA1_ROTL(a, 5) + SHA1_F2(b, c, d) + e + w[i] + SHA1_K2;
        } else {
            temp = SHA1_ROTL(a, 5) + SHA1_F3(b, c, d) + e + w[i] + SHA1_K3;
        }
        e = d;
        d = c;
        c = SHA1_ROTL(b, 30);
        b = a;
        a = temp;
    }
    
    /*
 * Update state
 */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

/*
 * =========================================================================================================== SHA-1 Public Functions =====================================================================================
 */

void crypto_sha1_init(crypto_sha1_ctx_t *ctx) {
    memcpy(ctx->state, SHA1_INIT, sizeof(SHA1_INIT));
    ctx->count = 0;
}

void crypto_sha1_update(crypto_sha1_ctx_t *ctx, const u8 *data, u32 len) {
    for (u32 i = 0; i < len; i++) {
        ctx->buffer[ctx->count % 64] = data[i];
        ctx->count++;
        
        if (ctx->count % 64 == 0) {
            sha1_transform(ctx->state, ctx->buffer);
        }
    }
}

void crypto_sha1_final(crypto_sha1_ctx_t *ctx, u8 out[CRYPTO_SHA1_DIGEST_SIZE]) {
    u64 bit_len = ctx->count * 8;
    u8 padding[64];
    u32 pad_len = 64 - (ctx->count % 64);
    
    if (pad_len < 9) pad_len += 64;
    
    padding[0] = 0x80;
    for (u32 i = 1; i < pad_len; i++) padding[i] = 0;
    
    for (int i = 0; i < 8; i++) {
        padding[pad_len - 8 + i] = (bit_len >> (56 - i*8)) & 0xFF;
    }
    
    crypto_sha1_update(ctx, padding, pad_len);
    
    for (int i = 0; i < 5; i++) {
        out[i*4]   = (ctx->state[i] >> 24) & 0xFF;
        out[i*4+1] = (ctx->state[i] >> 16) & 0xFF;
        out[i*4+2] = (ctx->state[i] >> 8) & 0xFF;
        out[i*4+3] = ctx->state[i] & 0xFF;
    }
}

void crypto_sha1(const u8 *data, u32 len, u8 out[CRYPTO_SHA1_DIGEST_SIZE]) {
    crypto_sha1_ctx_t ctx;
    crypto_sha1_init(&ctx);
    crypto_sha1_update(&ctx, data, len);
    crypto_sha1_final(&ctx, out);
}
