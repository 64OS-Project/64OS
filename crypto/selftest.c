#include <crypto/api.h>
#include <kernel/terminal.h>
#include <libk/string.h>

/*
 * ============================================================================== MD5 test ==================================================================================
 */
static int crypto_test_md5(void) {
    const u8 input[] = "abc";
    const u8 expected[16] = {
        0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
        0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72
    };
    u8 digest[16];
    
    crypto_md5(input, sizeof(input) - 1, digest);
    
    if (memcmp(digest, expected, 16) == 0)
        return CRYPTO_OK;
    return CRYPTO_ERR_HARDWARE_FAIL;
}

/*
 * ============================================================================== SHA1 test ====================================================================================
 */
static int crypto_test_sha1(void) {
    const u8 input[] = "abc";
    const u8 expected[20] = {
        0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
        0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d
    };
    u8 digest[20];
    
    crypto_sha1(input, sizeof(input) - 1, digest);
    
    if (memcmp(digest, expected, 20) == 0)
        return CRYPTO_OK;
    return CRYPTO_ERR_HARDWARE_FAIL;
}

/*
 * ============================================================================== SHA256 test =================================================================================
 */
static int crypto_test_sha256(void) {
    const u8 input[] = "abc";
    const u8 expected[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    u8 digest[32];
    
    crypto_sha256(input, sizeof(input) - 1, digest);
    
    if (memcmp(digest, expected, 32) == 0)
        return CRYPTO_OK;
    return CRYPTO_ERR_HARDWARE_FAIL;
}

/*
 * ============================================================================== ChaCha20 test ===================================================================================
 */
static int crypto_test_chacha20(void) {
    u8 key[32] = {0};
    u8 iv[12] = {0};
    u8 plaintext[64];
    u8 ciphertext[64];
    u8 decrypted[64];
    
    for (int i = 0; i < 64; i++)
        plaintext[i] = i;
    
    crypto_chacha20_ctx_t ctx;
    crypto_chacha20_init(&ctx, key, iv);
    crypto_chacha20_encrypt(&ctx, plaintext, ciphertext, 64);
    
    crypto_chacha20_init(&ctx, key, iv);
    crypto_chacha20_decrypt(&ctx, ciphertext, decrypted, 64);
    
    if (memcmp(decrypted, plaintext, 64) == 0)
        return CRYPTO_OK;
    return CRYPTO_ERR_HARDWARE_FAIL;
}

/*
 * ============================================================================== RNG test ====================================================================================
 */
static int crypto_test_rng(void) {
    u8 buf[256];
    int non_zero = 0;
    
    crypto_get_random_bytes(buf, sizeof(buf));
    
    for (int i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0) non_zero++;
    }
    
    if (non_zero > 0)
        return CRYPTO_OK;
    return CRYPTO_ERR_HARDWARE_FAIL;
}

/*
 * =============================================================================== Test table ================================================================================== Test table
 */
static const struct {
    const char *name;
    int (*test)(void);
} tests[] = {
    {"MD5",     crypto_test_md5},
    {"SHA1",    crypto_test_sha1},
    {"SHA256",  crypto_test_sha256},
    {"ChaCha20", crypto_test_chacha20},
    {"RNG",     crypto_test_rng},
    {NULL, NULL}
};

/*
 * =============================================================================== Public function self-test =================================================================================
 */
int crypto_self_test(void) {
    terminal_printf("[CRYPTO] Running self-tests...\n");
    
    int passed = 0;
    int failed = 0;
    
    for (int i = 0; tests[i].name; i++) {
        terminal_printf("  %s... ", tests[i].name);
        if (tests[i].test() == CRYPTO_OK) {
            terminal_success_printf("PASS\n");
            passed++;
        } else {
            terminal_error_printf("FAIL\n");
            failed++;
        }
    }
    
    terminal_printf("[CRYPTO] %d passed, %d failed\n", passed, failed);
    
    return (failed == 0) ? CRYPTO_OK : CRYPTO_ERR_HARDWARE_FAIL;
}
