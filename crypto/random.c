#include <crypto/api.h>
#include <crypto/internal.h>
#include <asm/cpu.h>
#include <asm/io.h>
#include <libk/string.h>
#include <kernel/timer.h>

/*
 * ============================================================================== RNG states =====================================================================================
 */

static struct {
    crypto_chacha20_ctx_t ctx;
    u8 pool[256];           /*
 * Entropy pool
 */
    u32 pool_idx;
    u64 reseed_counter;
    bool initialized;
    bool rdrand_available;
} g_rng;

/*
 * ============================================================================== Checking RDRAND ==================================================================================
 */

static bool rdrand_available(void) {
    u32 eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    return (ecx & (1 << 30)) != 0;  /*
 * RDRAND bit
 */
}

static u64 rdrand64(void) {
    u64 val;
    int retry = 10;
    
    while (retry--) {
        unsigned char ok;
        __asm__ volatile(
            "rdrand %0; setc %1"
            : "=r"(val), "=qm"(ok)
            : : "cc"
        );
        if (ok) return val;
    }
    
    return 0;
}

void crypto_memzero(void *ptr, u32 len) {
    volatile u8 *p = (volatile u8 *)ptr;
    while (len--) {
        *p++ = 0;
    }
}

/*
 * =============================================================================== Entropy collection =====================================================================================
 */

static void add_entropy(const u8 *data, u32 len) {
    for (u32 i = 0; i < len; i++) {
        g_rng.pool[g_rng.pool_idx] ^= data[i];
        g_rng.pool_idx = (g_rng.pool_idx + 1) % sizeof(g_rng.pool);
    }
}

static void collect_entropy(void) {
    u64 entropy = 0;
    
    /*
 * TSC (high precision timer)
 */
    entropy ^= rdtsc();
    add_entropy((u8*)&entropy, sizeof(entropy));
    
    /*
 * RDRAND if available
 */
    if (g_rng.rdrand_available) {
        entropy ^= rdrand64();
        add_entropy((u8*)&entropy, sizeof(entropy));
    }
    
    /*
 * Stack address (ASLR)
 */
    u64 stack_ptr;
    __asm__ volatile("mov %%rsp, %0" : "=r"(stack_ptr));
    entropy ^= stack_ptr;
    add_entropy((u8*)&entropy, sizeof(entropy));
    
    /*
 * CPU flags
 */
    u64 flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags));
    entropy ^= flags;
    add_entropy((u8*)&entropy, sizeof(entropy));
}

/*
 * ============================================================================== RNG Initialization ================================================================================
 */

void crypto_rng_init(void) {
    memset(&g_rng, 0, sizeof(g_rng));
    
    g_rng.rdrand_available = rdrand_available();
    
    /*
 * Collecting the initial entropy
 */
    for (int i = 0; i < 32; i++) {
        collect_entropy();
    }
    
    /*
 * Initializing ChaCha20 with a pool
 */
    crypto_chacha20_init(&g_rng.ctx, g_rng.pool, (u8[]){
        (g_rng.pool[0] ^ g_rng.pool[32]),
        (g_rng.pool[1] ^ g_rng.pool[33]),
        (g_rng.pool[2] ^ g_rng.pool[34]),
        (g_rng.pool[3] ^ g_rng.pool[35]),
        (g_rng.pool[4] ^ g_rng.pool[36]),
        (g_rng.pool[5] ^ g_rng.pool[37]),
        (g_rng.pool[6] ^ g_rng.pool[38]),
        (g_rng.pool[7] ^ g_rng.pool[39]),
        (g_rng.pool[8] ^ g_rng.pool[40]),
        (g_rng.pool[9] ^ g_rng.pool[41]),
        (g_rng.pool[10] ^ g_rng.pool[42]),
        (g_rng.pool[11] ^ g_rng.pool[43])
    });
    
    g_rng.initialized = true;
    
    /*
 * First reseed
 */
    crypto_rng_reseed();
}

/*
 * =============================================================================== Reseed =================================================================================== Reseed
 */

void crypto_rng_reseed(void) {
    if (!g_rng.initialized) return;
    
    u8 new_key[32];
    
    /*
 * Generating a new key from the current state
 */
    crypto_chacha20_xor(&g_rng.ctx, new_key, sizeof(new_key));
    
    /*
 * Collecting fresh entropy
 */
    collect_entropy();
    
    /*
 * Mix with pool
 */
    for (int i = 0; i < 32; i++) {
        new_key[i] ^= g_rng.pool[i];
    }
    
    /*
 * New initialization
 */
    crypto_chacha20_init(&g_rng.ctx, new_key, (u8[]){
        g_rng.pool[0], g_rng.pool[1], g_rng.pool[2], g_rng.pool[3],
        g_rng.pool[4], g_rng.pool[5], g_rng.pool[6], g_rng.pool[7],
        g_rng.pool[8], g_rng.pool[9], g_rng.pool[10], g_rng.pool[11]
    });
    
    g_rng.reseed_counter++;
    crypto_memzero(new_key, sizeof(new_key));
}

/*
 * ============================================================================== Receiving random bytes ===============================================================================
 */

int crypto_get_random_bytes(u8 *buf, u32 len) {
    if (!buf || len == 0) return CRYPTO_ERR_INVALID_PARAM;
    if (!g_rng.initialized) return CRYPTO_ERR_HARDWARE_FAIL;
    
    /*
 * Every 1MB – reseed
 */
    if (g_rng.reseed_counter > 1024 * 1024 / 64) {
        crypto_rng_reseed();
    }
    
    /*
 * Generating random bytes
 */
    crypto_chacha20_xor(&g_rng.ctx, buf, len);
    g_rng.reseed_counter += len;
    
    return CRYPTO_OK;
}
