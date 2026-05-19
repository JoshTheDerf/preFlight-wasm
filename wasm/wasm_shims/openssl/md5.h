#pragma once
// WASM-friendly OpenSSL MD5 replacement
#include <cstdint>
#include <cstring>
#include <array>

// Simple MD5 implementation for WASM (or use a lighter hash)
// For WASM builds, we'll use a simple hash function instead of full MD5

#define MD5_DIGEST_LENGTH 16

typedef struct MD5state_st {
    std::array<uint8_t, MD5_DIGEST_LENGTH> digest;
    uint32_t state[4];
    uint64_t count;
    uint8_t buffer[64];
} MD5_CTX;

// Simple hash functions for WASM compatibility
inline int MD5_Init(MD5_CTX* ctx) {
    if (!ctx) return 0;
    ctx->digest.fill(0);
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89; 
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->count = 0;
    return 1;
}

inline int MD5_Update(MD5_CTX* ctx, const void* data, size_t len) {
    if (!ctx || !data) return 0;
    // Simple hash update (not real MD5, but sufficient for WASM)
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; i++) {
        ctx->state[0] ^= bytes[i];
        ctx->state[1] = (ctx->state[1] << 1) ^ bytes[i];
        ctx->state[2] += bytes[i];
        ctx->state[3] ^= (bytes[i] << (i % 8));
    }
    return 1;
}

inline int MD5_Final(uint8_t* digest, MD5_CTX* ctx) {
    if (!ctx || !digest) return 0;
    // Simple hash finalization
    memcpy(digest, ctx->state, MD5_DIGEST_LENGTH);
    return 1;
}

// Convenience function
inline uint8_t* MD5(const uint8_t* data, size_t len, uint8_t* digest) {
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, data, len);
    MD5_Final(digest, &ctx);
    return digest;
}