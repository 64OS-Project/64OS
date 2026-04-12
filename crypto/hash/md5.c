#include <crypto/api.h>
#include <crypto/internal.h>
#include <libk/string.h>

/*
 * ============================================================================== MD5 constants ====================================================================================
 */

static const u32 MD5_INIT[4] = {
    0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476
};

static const u32 MD5_K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static const u32 MD5_S[64] = {
    7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
    5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
    4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
    6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21
};

/*
 * =============================================================================== Auxiliary functions ================================================================================
 */

#define MD5_F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | ~(z)))

#define MD5_FF(a, b, c, d, k, s, i) \
    (a) += MD5_F((b), (c), (d)) + (k) + (i); \
    (a) = CRYPTO_ROTL32((a), (s)); \
    (a) += (b)

#define MD5_GG(a, b, c, d, k, s, i) \
    (a) += MD5_G((b), (c), (d)) + (k) + (i); \
    (a) = CRYPTO_ROTL32((a), (s)); \
    (a) += (b)

#define MD5_HH(a, b, c, d, k, s, i) \
    (a) += MD5_H((b), (c), (d)) + (k) + (i); \
    (a) = CRYPTO_ROTL32((a), (s)); \
    (a) += (b)

#define MD5_II(a, b, c, d, k, s, i) \
    (a) += MD5_I((b), (c), (d)) + (k) + (i); \
    (a) = CRYPTO_ROTL32((a), (s)); \
    (a) += (b)

/*
 * =============================================================================== Main function of MD5 =====================================================================================
 */

static void md5_transform(u32 *state, const u8 *block) {
    u32 a = state[0];
    u32 b = state[1];
    u32 c = state[2];
    u32 d = state[3];
    u32 x[16];
    
    /*
 * Unpacking the block
 */
    for (int i = 0; i < 16; i++) {
        x[i] = (u32)block[i*4] |
               ((u32)block[i*4+1] << 8) |
               ((u32)block[i*4+2] << 16) |
               ((u32)block[i*4+3] << 24);
    }
    
    /*
 * Round 1
 */
    MD5_FF(a, b, c, d, x[ 0], MD5_S[ 0], MD5_K[ 0]);
    MD5_FF(d, a, b, c, x[ 1], MD5_S[ 1], MD5_K[ 1]);
    MD5_FF(c, d, a, b, x[ 2], MD5_S[ 2], MD5_K[ 2]);
    MD5_FF(b, c, d, a, x[ 3], MD5_S[ 3], MD5_K[ 3]);
    MD5_FF(a, b, c, d, x[ 4], MD5_S[ 4], MD5_K[ 4]);
    MD5_FF(d, a, b, c, x[ 5], MD5_S[ 5], MD5_K[ 5]);
    MD5_FF(c, d, a, b, x[ 6], MD5_S[ 6], MD5_K[ 6]);
    MD5_FF(b, c, d, a, x[ 7], MD5_S[ 7], MD5_K[ 7]);
    MD5_FF(a, b, c, d, x[ 8], MD5_S[ 8], MD5_K[ 8]);
    MD5_FF(d, a, b, c, x[ 9], MD5_S[ 9], MD5_K[ 9]);
    MD5_FF(c, d, a, b, x[10], MD5_S[10], MD5_K[10]);
    MD5_FF(b, c, d, a, x[11], MD5_S[11], MD5_K[11]);
    MD5_FF(a, b, c, d, x[12], MD5_S[12], MD5_K[12]);
    MD5_FF(d, a, b, c, x[13], MD5_S[13], MD5_K[13]);
    MD5_FF(c, d, a, b, x[14], MD5_S[14], MD5_K[14]);
    MD5_FF(b, c, d, a, x[15], MD5_S[15], MD5_K[15]);
    
    /*
 * Round 2
 */
    MD5_GG(a, b, c, d, x[ 1], MD5_S[16], MD5_K[16]);
    MD5_GG(d, a, b, c, x[ 6], MD5_S[17], MD5_K[17]);
    MD5_GG(c, d, a, b, x[11], MD5_S[18], MD5_K[18]);
    MD5_GG(b, c, d, a, x[ 0], MD5_S[19], MD5_K[19]);
    MD5_GG(a, b, c, d, x[ 5], MD5_S[20], MD5_K[20]);
    MD5_GG(d, a, b, c, x[10], MD5_S[21], MD5_K[21]);
    MD5_GG(c, d, a, b, x[15], MD5_S[22], MD5_K[22]);
    MD5_GG(b, c, d, a, x[ 4], MD5_S[23], MD5_K[23]);
    MD5_GG(a, b, c, d, x[ 9], MD5_S[24], MD5_K[24]);
    MD5_GG(d, a, b, c, x[14], MD5_S[25], MD5_K[25]);
    MD5_GG(c, d, a, b, x[ 3], MD5_S[26], MD5_K[26]);
    MD5_GG(b, c, d, a, x[ 8], MD5_S[27], MD5_K[27]);
    MD5_GG(a, b, c, d, x[13], MD5_S[28], MD5_K[28]);
    MD5_GG(d, a, b, c, x[ 2], MD5_S[29], MD5_K[29]);
    MD5_GG(c, d, a, b, x[ 7], MD5_S[30], MD5_K[30]);
    MD5_GG(b, c, d, a, x[12], MD5_S[31], MD5_K[31]);
    
    /*
 * Round 3
 */
    MD5_HH(a, b, c, d, x[ 5], MD5_S[32], MD5_K[32]);
    MD5_HH(d, a, b, c, x[ 8], MD5_S[33], MD5_K[33]);
    MD5_HH(c, d, a, b, x[11], MD5_S[34], MD5_K[34]);
    MD5_HH(b, c, d, a, x[14], MD5_S[35], MD5_K[35]);
    MD5_HH(a, b, c, d, x[ 1], MD5_S[36], MD5_K[36]);
    MD5_HH(d, a, b, c, x[ 4], MD5_S[37], MD5_K[37]);
    MD5_HH(c, d, a, b, x[ 7], MD5_S[38], MD5_K[38]);
    MD5_HH(b, c, d, a, x[10], MD5_S[39], MD5_K[39]);
    MD5_HH(a, b, c, d, x[13], MD5_S[40], MD5_K[40]);
    MD5_HH(d, a, b, c, x[ 0], MD5_S[41], MD5_K[41]);
    MD5_HH(c, d, a, b, x[ 3], MD5_S[42], MD5_K[42]);
    MD5_HH(b, c, d, a, x[ 6], MD5_S[43], MD5_K[43]);
    MD5_HH(a, b, c, d, x[ 9], MD5_S[44], MD5_K[44]);
    MD5_HH(d, a, b, c, x[12], MD5_S[45], MD5_K[45]);
    MD5_HH(c, d, a, b, x[15], MD5_S[46], MD5_K[46]);
    MD5_HH(b, c, d, a, x[ 2], MD5_S[47], MD5_K[47]);
    
    /*
 * Round 4
 */
    MD5_II(a, b, c, d, x[ 0], MD5_S[48], MD5_K[48]);
    MD5_II(d, a, b, c, x[ 7], MD5_S[49], MD5_K[49]);
    MD5_II(c, d, a, b, x[14], MD5_S[50], MD5_K[50]);
    MD5_II(b, c, d, a, x[ 5], MD5_S[51], MD5_K[51]);
    MD5_II(a, b, c, d, x[12], MD5_S[52], MD5_K[52]);
    MD5_II(d, a, b, c, x[ 3], MD5_S[53], MD5_K[53]);
    MD5_II(c, d, a, b, x[10], MD5_S[54], MD5_K[54]);
    MD5_II(b, c, d, a, x[ 1], MD5_S[55], MD5_K[55]);
    MD5_II(a, b, c, d, x[ 8], MD5_S[56], MD5_K[56]);
    MD5_II(d, a, b, c, x[15], MD5_S[57], MD5_K[57]);
    MD5_II(c, d, a, b, x[ 6], MD5_S[58], MD5_K[58]);
    MD5_II(b, c, d, a, x[13], MD5_S[59], MD5_K[59]);
    MD5_II(a, b, c, d, x[ 4], MD5_S[60], MD5_K[60]);
    MD5_II(d, a, b, c, x[11], MD5_S[61], MD5_K[61]);
    MD5_II(c, d, a, b, x[ 2], MD5_S[62], MD5_K[62]);
    MD5_II(b, c, d, a, x[ 9], MD5_S[63], MD5_K[63]);
    
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

/*
 * ==================================================================================== MD5 Public Functions =====================================================================================
 */

void crypto_md5_init(crypto_md5_ctx_t *ctx) {
    memcpy(ctx->state, MD5_INIT, sizeof(MD5_INIT));
    ctx->count = 0;
}

void crypto_md5_update(crypto_md5_ctx_t *ctx, const u8 *data, u32 len) {
    u32 free_space = 64 - (ctx->count % 64);
    
    for (u32 i = 0; i < len; i++) {
        ctx->buffer[ctx->count % 64] = data[i];
        ctx->count++;
        
        if (ctx->count % 64 == 0) {
            md5_transform(ctx->state, ctx->buffer);
        }
    }
}

void crypto_md5_final(crypto_md5_ctx_t *ctx, u8 out[CRYPTO_MD5_DIGEST_SIZE]) {
    u64 bit_len = ctx->count * 8;
    u8 padding[64];
    u32 pad_len = 64 - (ctx->count % 64);
    
    if (pad_len < 9) pad_len += 64;
    
    padding[0] = 0x80;
    for (u32 i = 1; i < pad_len; i++) padding[i] = 0;
    
    for (int i = 0; i < 8; i++) {
        padding[pad_len - 8 + i] = (bit_len >> (i*8)) & 0xFF;
    }
    
    crypto_md5_update(ctx, padding, pad_len);
    
    for (int i = 0; i < 4; i++) {
        out[i*4]   = (ctx->state[i] >> 0) & 0xFF;
        out[i*4+1] = (ctx->state[i] >> 8) & 0xFF;
        out[i*4+2] = (ctx->state[i] >> 16) & 0xFF;
        out[i*4+3] = (ctx->state[i] >> 24) & 0xFF;
    }
}

void crypto_md5(const u8 *data, u32 len, u8 out[CRYPTO_MD5_DIGEST_SIZE]) {
    crypto_md5_ctx_t ctx;
    crypto_md5_init(&ctx);
    crypto_md5_update(&ctx, data, len);
    crypto_md5_final(&ctx, out);
}
