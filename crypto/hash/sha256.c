#include <crypto/api.h>
#include <crypto/internal.h>
#include <libk/string.h>

/*
 * ============================================================================== SHA-256 constants =================================================================================
 */

static const u32 SHA256_INIT[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

static const u32 SHA256_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/*
 * =============================================================================== Auxiliary functions ================================================================================
 */

#define SHA256_ROTR(x, n) CRYPTO_ROTR32((x), (n))
#define SHA256_SHR(x, n) ((x) >> (n))

#define SHA256_Ch(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

#define SHA256_Sigma0(x) (SHA256_ROTR((x), 2) ^ SHA256_ROTR((x), 13) ^ SHA256_ROTR((x), 22))
#define SHA256_Sigma1(x) (SHA256_ROTR((x), 6) ^ SHA256_ROTR((x), 11) ^ SHA256_ROTR((x), 25))

#define SHA256_sigma0(x) (SHA256_ROTR((x), 7) ^ SHA256_ROTR((x), 18) ^ SHA256_SHR((x), 3))
#define SHA256_sigma1(x) (SHA256_ROTR((x), 17) ^ SHA256_ROTR((x), 19) ^ SHA256_SHR((x), 10))

/*
 * =============================================================================== Main function of SHA-256 =================================================================================
 */

static void sha256_transform(u32 *state, const u8 *block) {
    u32 w[64];
    u32 a, b, c, d, e, f, g, h;
    
    /*
 * Prepare message schedule
 */
    for (int i = 0; i < 16; i++) {
        w[i] = (u32)block[i*4] << 24 |
               (u32)block[i*4+1] << 16 |
               (u32)block[i*4+2] << 8 |
               (u32)block[i*4+3];
    }
    
    for (int i = 16; i < 64; i++) {
        w[i] = SHA256_sigma1(w[i-2]) + w[i-7] + SHA256_sigma0(w[i-15]) + w[i-16];
    }
    
    /*
 * Initialize working variables
 */
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];
    
    /*
 * Main loop
 */
    for (int i = 0; i < 64; i++) {
        u32 t1 = h + SHA256_Sigma1(e) + SHA256_Ch(e, f, g) + SHA256_K[i] + w[i];
        u32 t2 = SHA256_Sigma0(a) + SHA256_Maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    
    /*
 * Update state
 */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

/*
 * =========================================================================================================== SHA-256 Public Functions ======================================================================================
 */

void crypto_sha256_init(crypto_sha256_ctx_t *ctx) {
    memcpy(ctx->state, SHA256_INIT, sizeof(SHA256_INIT));
    ctx->count = 0;
}

void crypto_sha256_update(crypto_sha256_ctx_t *ctx, const u8 *data, u32 len) {
    u32 free_space = 64 - (ctx->count % 64);
    
    for (u32 i = 0; i < len; i++) {
        ctx->buffer[ctx->count % 64] = data[i];
        ctx->count++;
        
        if (ctx->count % 64 == 0) {
            sha256_transform(ctx->state, ctx->buffer);
        }
    }
}

void crypto_sha256_final(crypto_sha256_ctx_t *ctx, u8 out[CRYPTO_SHA256_DIGEST_SIZE]) {
    u64 bit_len = ctx->count * 8;
    u8 padding[64];
    u32 pad_len = 64 - (ctx->count % 64);
    
    if (pad_len < 9) pad_len += 64;
    
    padding[0] = 0x80;
    for (u32 i = 1; i < pad_len; i++) padding[i] = 0;
    
    for (int i = 0; i < 8; i++) {
        padding[pad_len - 8 + i] = (bit_len >> (56 - i*8)) & 0xFF;
    }
    
    crypto_sha256_update(ctx, padding, pad_len);
    
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (ctx->state[i] >> 24) & 0xFF;
        out[i*4+1] = (ctx->state[i] >> 16) & 0xFF;
        out[i*4+2] = (ctx->state[i] >> 8) & 0xFF;
        out[i*4+3] = ctx->state[i] & 0xFF;
    }
}

void crypto_sha256(const u8 *data, u32 len, u8 out[CRYPTO_SHA256_DIGEST_SIZE]) {
    crypto_sha256_ctx_t ctx;
    crypto_sha256_init(&ctx);
    crypto_sha256_update(&ctx, data, len);
    crypto_sha256_final(&ctx, out);
}
