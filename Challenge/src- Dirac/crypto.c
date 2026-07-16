#include "crypto.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── crypto_key_schedule ─────────────────────────────────────────── */

uint32_t crypto_key_schedule(const CryptoContext *ctx, uint8_t *round_keys) {
    if (ctx == NULL || round_keys == NULL) return 0;

    /* Load the 128-bit master key as two 64-bit halves (big-endian) */
    uint64_t key_hi = 0, key_lo = 0;
    for (int i = 0; i < 8; i++) {
        key_hi = (key_hi << 8) | ctx->key[i];
        key_lo = (key_lo << 8) | ctx->key[i + 8];
    }

    for (uint32_t round = 0; round < CRYPTO_ROUNDS; round++) {
        /* Rotate 128-bit key left by round*2 bits, keep high 64 bits */
        uint32_t shift = (round * 2) % 128;
        uint64_t rk_hi;

        if (shift == 0) {
            rk_hi = key_hi;
        } else if (shift < 64) {
            rk_hi = (key_hi << shift) | (key_lo >> (64 - shift));
        } else if (shift == 64) {
            rk_hi = key_lo;
        } else {
            /* shift > 64, same as shift - 64 but swap halves */
            uint32_t s = shift - 64;
            rk_hi = (key_lo << s) | (key_hi >> (64 - s));
        }

        /* XOR with round constant */
        uint64_t round_const = 0x9E3779B97F4A7C15ULL >> (round * 7);
        rk_hi ^= round_const;

        /* Store 8-byte round key (big-endian) */
        uint64_t rk = rk_hi;
        for (int j = 7; j >= 0; j--) {
            round_keys[round * 8 + j] = (uint8_t)(rk & 0xFF);
            rk >>= 8;
        }
    }

    return CRYPTO_ROUNDS;
}

/* ── crypto_feistel_f ────────────────────────────────────────────── */

/* DES S-box 1 */
static const uint8_t SBOX1[4][16] = {
    { 14,  4, 13,  1,  2, 15, 11,  8,  3, 10,  6, 12,  5,  9,  0,  7 },
    {  0, 15,  7,  4, 14,  2, 13,  1, 10,  6, 12, 11,  9,  5,  3,  8 },
    {  4,  1, 14,  8, 13,  6,  2, 11, 15, 12,  9,  7,  3, 10,  5,  0 },
    { 15, 12,  8,  2,  4,  9,  1,  7,  5, 11,  3, 14, 10,  0,  6, 13 }
};

/* P-permutation table: output bit position = P[input_bit - 1] (1-indexed MSB) */
static const uint8_t P_TABLE[32] = {
    16,  7, 20, 21, 29, 12, 28, 17,  1, 15, 23, 26,  5, 18, 31, 10,
     2,  8, 24, 14, 32, 27,  3,  9, 19, 13, 30,  6, 22, 11,  4, 25
};

/* Expansion table: output bit i (1-indexed) gets input bit E_TABLE[i-1] (1-indexed) */
static const uint8_t E_TABLE[48] = {
    32,  1,  2,  3,  4,  5,   4,  5,  6,  7,  8,  9,
     8,  9, 10, 11, 12, 13,  12, 13, 14, 15, 16, 17,
    16, 17, 18, 19, 20, 21,  20, 21, 22, 23, 24, 25,
    24, 25, 26, 27, 28, 29,  28, 29, 30, 31, 32,  1
};

uint32_t crypto_feistel_f(uint32_t R, const uint8_t K[8]) {
    uint64_t expanded = 0;  /* 48 bits */

    /* 1. Expand 32-bit R to 48 bits using expansion table */
    for (int i = 0; i < 48; i++) {
        uint8_t src_bit_pos = E_TABLE[i];  /* 1-indexed, 1=MSB */
        /* Extract bit from R: MSB is bit 1 (position 31 in 0-indexed) */
        uint32_t bit = (R >> (32 - src_bit_pos)) & 1;
        expanded = (expanded << 1) | bit;
    }

    /* 2. XOR with first 48 bits of K (K[0..5] = 6 bytes = 48 bits) */
    uint64_t k48 = 0;
    for (int i = 0; i < 6; i++) {
        k48 = (k48 << 8) | K[i];
    }
    expanded ^= k48;

    /* 3. Split into 8 groups of 6 bits each, apply S-box */
    uint32_t sbox_output = 0;  /* 32 bits */
    for (int g = 0; g < 8; g++) {
        /* Extract 6-bit group (MSB-first), group 0 is the highest 6 bits (bits 0-5 of expanded) */
        uint8_t group6 = (uint8_t)((expanded >> (42 - g * 6)) & 0x3F);  /* bits at positions g*6 .. g*6+5 */
        /* b1 (MSB) = bit 5, b6 (LSB) = bit 0 */
        uint8_t b1 = (group6 >> 5) & 1;
        uint8_t b6 = group6 & 1;
        uint8_t row = (uint8_t)((b1 << 1) | b6);
        uint8_t col = (group6 >> 1) & 0x0F;
        uint8_t val = SBOX1[row][col];
        sbox_output = (sbox_output << 4) | (val & 0x0F);
    }

    /* 5. Apply P-permutation */
    uint32_t result = 0;
    for (int i = 0; i < 32; i++) {
        uint8_t src_pos = P_TABLE[i];  /* 1-indexed, which bit of sbox_output goes to output bit i+1 */
        uint32_t bit = (sbox_output >> (32 - src_pos)) & 1;
        result = (result << 1) | bit;
    }

    return result;
}

/* ── Feistel network encrypt/decrypt ─────────────────────────────── */

int crypto_init(CryptoContext *ctx, const uint8_t *key, uint32_t key_len) {
    if (ctx == NULL || key == NULL) return -1;
    memset(ctx, 0, sizeof(CryptoContext));
    uint32_t copy_len = key_len < CRYPTO_KEY_SIZE ? key_len : CRYPTO_KEY_SIZE;
    memcpy(ctx->key, key, copy_len);
    ctx->rounds = CRYPTO_ROUNDS;
    return 0;
}

/* Process one 8-byte block through Feistel network */
static void feistel_encrypt_block(const uint8_t *round_keys, uint32_t rounds,
                                   const uint8_t *plain, uint8_t *cipher) {
    /* Split into L (4 bytes) and R (4 bytes) */
    uint32_t L = ((uint32_t)plain[0] << 24) | ((uint32_t)plain[1] << 16) |
                 ((uint32_t)plain[2] << 8)  |  (uint32_t)plain[3];
    uint32_t R = ((uint32_t)plain[4] << 24) | ((uint32_t)plain[5] << 16) |
                 ((uint32_t)plain[6] << 8)  |  (uint32_t)plain[7];

    for (uint32_t i = 0; i < rounds; i++) {
        uint32_t F = crypto_feistel_f(R, &round_keys[i * 8]);
        uint32_t new_R = L ^ F;
        L = R;
        R = new_R;
    }

    /* Output: combine L and R */
    cipher[0] = (uint8_t)(L >> 24); cipher[1] = (uint8_t)(L >> 16);
    cipher[2] = (uint8_t)(L >> 8);  cipher[3] = (uint8_t)(L);
    cipher[4] = (uint8_t)(R >> 24); cipher[5] = (uint8_t)(R >> 16);
    cipher[6] = (uint8_t)(R >> 8);  cipher[7] = (uint8_t)(R);
}

static void feistel_decrypt_block(const uint8_t *round_keys, uint32_t rounds,
                                   const uint8_t *cipher, uint8_t *plain) {
    /* Split into L and R */
    uint32_t L = ((uint32_t)cipher[0] << 24) | ((uint32_t)cipher[1] << 16) |
                 ((uint32_t)cipher[2] << 8)  |  (uint32_t)cipher[3];
    uint32_t R = ((uint32_t)cipher[4] << 24) | ((uint32_t)cipher[5] << 16) |
                 ((uint32_t)cipher[6] << 8)  |  (uint32_t)cipher[7];

    /* Reverse rounds */
    for (int i = (int)rounds - 1; i >= 0; i--) {
        uint32_t F = crypto_feistel_f(L, &round_keys[i * 8]);
        uint32_t new_L = R ^ F;
        R = L;
        L = new_L;
    }

    plain[0] = (uint8_t)(L >> 24); plain[1] = (uint8_t)(L >> 16);
    plain[2] = (uint8_t)(L >> 8);  plain[3] = (uint8_t)(L);
    plain[4] = (uint8_t)(R >> 24); plain[5] = (uint8_t)(R >> 16);
    plain[6] = (uint8_t)(R >> 8);  plain[7] = (uint8_t)(R);
}

uint32_t crypto_encrypt(CryptoContext *ctx, const uint8_t *plaintext,
                        uint8_t *ciphertext, uint32_t len) {
    if (ctx == NULL || plaintext == NULL || ciphertext == NULL) return 0;

    /* Generate round keys */
    uint8_t round_keys[CRYPTO_ROUNDS * 8];
    crypto_key_schedule(ctx, round_keys);

    uint32_t blocks = len / CRYPTO_BLOCK_SIZE;
    uint32_t remainder = len % CRYPTO_BLOCK_SIZE;

    for (uint32_t b = 0; b < blocks; b++) {
        feistel_encrypt_block(round_keys, CRYPTO_ROUNDS,
                              plaintext + b * CRYPTO_BLOCK_SIZE,
                              ciphertext + b * CRYPTO_BLOCK_SIZE);
    }

    /* For remainder: XOR with key (padding-like behavior) */
    if (remainder > 0) {
        uint32_t offset = blocks * CRYPTO_BLOCK_SIZE;
        for (uint32_t i = 0; i < remainder; i++)
            ciphertext[offset + i] = plaintext[offset + i] ^ ctx->key[i];
    }

    return len;
}

uint32_t crypto_decrypt(CryptoContext *ctx, const uint8_t *ciphertext,
                        uint8_t *plaintext, uint32_t len) {
    if (ctx == NULL || ciphertext == NULL || plaintext == NULL) return 0;

    /* Generate round keys */
    uint8_t round_keys[CRYPTO_ROUNDS * 8];
    crypto_key_schedule(ctx, round_keys);

    uint32_t blocks = len / CRYPTO_BLOCK_SIZE;
    uint32_t remainder = len % CRYPTO_BLOCK_SIZE;

    for (uint32_t b = 0; b < blocks; b++) {
        feistel_decrypt_block(round_keys, CRYPTO_ROUNDS,
                              ciphertext + b * CRYPTO_BLOCK_SIZE,
                              plaintext + b * CRYPTO_BLOCK_SIZE);
    }

    /* For remainder: XOR with key (symmetric) */
    if (remainder > 0) {
        uint32_t offset = blocks * CRYPTO_BLOCK_SIZE;
        for (uint32_t i = 0; i < remainder; i++)
            plaintext[offset + i] = ciphertext[offset + i] ^ ctx->key[i];
    }

    return len;
}
