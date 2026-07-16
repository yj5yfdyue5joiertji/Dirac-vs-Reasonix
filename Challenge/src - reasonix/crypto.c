#include "crypto.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── DES S-box 1 (row = bits 1+6, col = bits 2-5) ────────────────── */

static const uint8_t SBOX1[4][16] = {
    {14,  4, 13,  1,  2, 15, 11,  8,  3, 10,  6, 12,  5,  9,  0,  7},
    { 0, 15,  7,  4, 14,  2, 13,  1, 10,  6, 12, 11,  9,  5,  3,  8},
    { 4,  1, 14,  8, 13,  6,  2, 11, 15, 12,  9,  7,  3, 10,  5,  0},
    {15, 12,  8,  2,  4,  9,  1,  7,  5, 11,  3, 14, 10,  0,  6, 13}
};

/* ── P-permutation table ──────────────────────────────────────────── */

static const uint8_t P_TABLE[32] = {
    16,  7, 20, 21, 29, 12, 28, 17,
     1, 15, 23, 26,  5, 18, 31, 10,
     2,  8, 24, 14, 32, 27,  3,  9,
    19, 13, 30,  6, 22, 11,  4, 25
};

/* ── Expansion table ──────────────────────────────────────────────── */

static const uint8_t E_TABLE[48] = {
    32,  1,  2,  3,  4,  5,
     4,  5,  6,  7,  8,  9,
     8,  9, 10, 11, 12, 13,
    12, 13, 14, 15, 16, 17,
    16, 17, 18, 19, 20, 21,
    20, 21, 22, 23, 24, 25,
    24, 25, 26, 27, 28, 29,
    28, 29, 30, 31, 32,  1
};

/* ── Round constant ───────────────────────────────────────────────── */

static const uint64_t ROUND_CONSTANT = 0x9E3779B97F4A7C15ULL;

/* ── Feistel block encrypt (one 64-bit block) ─────────────────────── */

static uint64_t feistel_encrypt_block(uint64_t block, const uint8_t *round_keys,
                                       uint32_t rounds) {
    uint32_t L = (uint32_t)(block >> 32);
    uint32_t R = (uint32_t)(block & 0xFFFFFFFF);

    for (uint32_t r = 0; r < rounds; r++) {
        const uint8_t *rk = &round_keys[r * CRYPTO_BLOCK_SIZE];
        uint32_t f_out = crypto_feistel_f(R, rk);
        uint32_t new_R = L ^ f_out;
        L = R;
        R = new_R;
    }

    return ((uint64_t)L << 32) | (uint64_t)R;
}

/* ── Feistel block decrypt (one 64-bit block) ─────────────────────── */

static uint64_t feistel_decrypt_block(uint64_t block, const uint8_t *round_keys,
                                       uint32_t rounds) {
    uint32_t L = (uint32_t)(block >> 32);
    uint32_t R = (uint32_t)(block & 0xFFFFFFFF);

    for (int r_ = (int)rounds - 1; r_ >= 0; r_--) {
        uint32_t r = (uint32_t)r_;
        const uint8_t *rk = &round_keys[r * CRYPTO_BLOCK_SIZE];
        uint32_t f_out = crypto_feistel_f(L, rk);
        uint32_t new_L = R ^ f_out;
        R = L;
        L = new_L;
    }

    return ((uint64_t)L << 32) | (uint64_t)R;
}

/* ── Public API ───────────────────────────────────────────────────── */

int crypto_init(CryptoContext *ctx, const uint8_t *key, uint32_t key_len) {
    if (ctx == NULL || key == NULL) return -1;

    memset(ctx, 0, sizeof(CryptoContext));
    uint32_t copy_len = key_len < CRYPTO_KEY_SIZE ? key_len : CRYPTO_KEY_SIZE;
    memcpy(ctx->key, key, copy_len);

    /* Pad short keys by repeating */
    for (uint32_t i = copy_len; i < CRYPTO_KEY_SIZE; i++) {
        ctx->key[i] = key[i % copy_len];
    }

    ctx->rounds = CRYPTO_ROUNDS;
    return 0;
}

uint32_t crypto_encrypt(CryptoContext *ctx, const uint8_t *plaintext,
                        uint8_t *ciphertext, uint32_t len) {
    if (ctx == NULL || plaintext == NULL || ciphertext == NULL) return 0;
    if (len == 0 || len > 65536) return 0;

    uint8_t round_keys[CRYPTO_ROUNDS * CRYPTO_BLOCK_SIZE];
    crypto_key_schedule(ctx, round_keys);

    /* Pad input with PKCS#7; len is bounded, so no overflow */
    uint32_t padded_len = ((len + CRYPTO_BLOCK_SIZE - 1) / CRYPTO_BLOCK_SIZE) * CRYPTO_BLOCK_SIZE;

    uint8_t *padded = (uint8_t *)malloc(padded_len);
    if (padded == NULL) return 0;

    memcpy(padded, plaintext, len);
    uint8_t pad_val = (uint8_t)(padded_len - len);
    if (pad_val == 0) pad_val = CRYPTO_BLOCK_SIZE;
    memset(padded + len, pad_val, padded_len - len);

    for (uint32_t pos = 0; pos < padded_len; pos += CRYPTO_BLOCK_SIZE) {
        uint64_t block = 0;
        memcpy(&block, padded + pos, CRYPTO_BLOCK_SIZE);
        uint64_t enc = feistel_encrypt_block(block, round_keys, ctx->rounds);
        memcpy(ciphertext + pos, &enc, CRYPTO_BLOCK_SIZE);
    }

    free(padded);
    return padded_len;
}

uint32_t crypto_decrypt(CryptoContext *ctx, const uint8_t *ciphertext,
                        uint8_t *plaintext, uint32_t len) {
    if (ctx == NULL || ciphertext == NULL || plaintext == NULL) return 0;
    if (len == 0 || len % CRYPTO_BLOCK_SIZE != 0) return 0;

    uint8_t round_keys[CRYPTO_ROUNDS * CRYPTO_BLOCK_SIZE];
    crypto_key_schedule(ctx, round_keys);

    uint8_t *decrypted = (uint8_t *)malloc(len);
    if (decrypted == NULL) return 0;

    for (uint32_t pos = 0; pos < len; pos += CRYPTO_BLOCK_SIZE) {
        uint64_t block = 0;
        memcpy(&block, ciphertext + pos, CRYPTO_BLOCK_SIZE);
        uint64_t dec = feistel_decrypt_block(block, round_keys, ctx->rounds);
        memcpy(decrypted + pos, &dec, CRYPTO_BLOCK_SIZE);
    }

    /* Strip PKCS#7 padding */
    uint8_t pad_val = decrypted[len - 1];
    uint32_t out_len = len;
    if (pad_val > 0 && pad_val <= CRYPTO_BLOCK_SIZE) {
        /* Verify padding */
        bool valid = true;
        for (uint8_t i = 1; i <= pad_val && i < (uint8_t)(len); i++) {
            if (decrypted[len - i] != pad_val) { valid = false; break; }
        }
        if (valid) out_len = len - pad_val;
    }

    memcpy(plaintext, decrypted, out_len);
    free(decrypted);
    return out_len;
}

/* ── IMPLEMENTED: crypto_key_schedule ─────────────────────────────── */

uint32_t crypto_key_schedule(const CryptoContext *ctx, uint8_t *round_keys) {
    if (ctx == NULL || round_keys == NULL) return 0;

    /* Load master key as 64-bit big-endian value */
    uint64_t master = 0;
    for (int i = 0; i < 8 && i < CRYPTO_KEY_SIZE; i++) {
        master = (master << 8) | ctx->key[i];
    }

    for (uint32_t r = 0; r < ctx->rounds; r++) {
        /* Rotate master key left by r*2 bits */
        uint64_t rotated = (master << (r * 2)) | (master >> (64 - r * 2));

        /* XOR with round constant shifted */
        uint64_t rc = ROUND_CONSTANT >> (r * 7);
        uint64_t rk = rotated ^ rc;

        /* Store 8 bytes of round key (big-endian: MSB first) */
        for (int b = 0; b < 8; b++) {
            round_keys[r * CRYPTO_BLOCK_SIZE + b] = (uint8_t)(rk >> (56 - b * 8));
        }
    }

    return ctx->rounds;
}

/* ── IMPLEMENTED: crypto_feistel_f ────────────────────────────────── */
/* All bit numbers are DES-style: 1-indexed, bit 1 = MSB.              */
/* Internal storage: expanded 48-bit in uint64_t bits 47..0 where     */
/* bit 47 = DES bit 1 (MSB of 48-bit value).                          */

uint32_t crypto_feistel_f(uint32_t R, const uint8_t K[8]) {
    /* Step 1: Expand 32-bit R to 48 bits using E-table.
     * E_TABLE[i] (i=0..47): output DES-bit (i+1) gets R's DES-bit E_TABLE[i].
     * Output bit 1 (=MSB of 48-bit) stored at uint64 bit 47.
     * Output bit 48 (=LSB) stored at uint64 bit 0. */
    uint64_t expanded = 0;
    for (int i = 0; i < 48; i++) {
        uint8_t src_des_bit = E_TABLE[i];  /* DES 1-indexed, 1=MSB of R */
        /* R is 32-bit. DES bit k (1=MSB) = (R >> (32-k)) & 1 */
        uint64_t bit_val = (R >> (32 - src_des_bit)) & 1;
        /* Output DES bit (i+1) → uint64 bit (47 - i) */
        expanded |= (bit_val << (47 - i));
    }

    /* Step 2: XOR with first 48 bits of K.
     * K[0] is most significant byte of 48-bit key → uint64 bits 47..40.
     * K[5] is least significant byte → uint64 bits 7..0. */
    uint64_t key48 = 0;
    for (int i = 0; i < 6; i++) {
        key48 |= ((uint64_t)K[i]) << (40 - i * 8);
    }
    expanded ^= key48;

    /* Step 3: Split into 8 groups of 6 bits each.
     * Group 0 = bits 47..42 (MSB group), Group 7 = bits 5..0 (LSB group).
     * Apply DES S-box 1 to each.
     * DES S-box: row = (bit1 << 1) | bit6 (bit1=MSB, bit6=LSB of 6-bit input)
     *             col = bits 2-5 (middle 4 bits) */
    uint32_t sbox_output = 0;
    for (int g = 0; g < 8; g++) {
        int lsb_pos = 42 - g * 6;  /* LSB position of this group in uint64 */
        uint8_t group6 = (uint8_t)((expanded >> lsb_pos) & 0x3F);

        /* row: bit1 (MSB of group6 = bit 5) * 2 + bit6 (LSB = bit 0) */
        uint8_t row = (uint8_t)(((group6 >> 4) & 2) | (group6 & 1));
        uint8_t col = (uint8_t)((group6 >> 1) & 0x0F);

        uint8_t sbox_val = SBOX1[row][col];
        sbox_output = (sbox_output << 4) | sbox_val;
    }

    /* Step 4: P-permutation.
     * P_TABLE[i] (i=0..31): output DES-bit (i+1) gets sbox_output DES-bit P_TABLE[i].
     * DES bit 1 = MSB of sbox_output → uint32 bit 31.
     * Output bit 1 = MSB of result → uint32 bit 31. */
    uint32_t result = 0;
    for (int i = 0; i < 32; i++) {
        uint8_t src_des_bit = P_TABLE[i];
        uint32_t bit_val = (sbox_output >> (32 - src_des_bit)) & 1;
        result |= (bit_val << (31 - i));
    }

    return result;
}
