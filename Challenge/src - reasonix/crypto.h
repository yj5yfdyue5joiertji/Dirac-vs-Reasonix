#ifndef CRYPTO_H
#define CRYPTO_H

#include "common.h"

#define CRYPTO_KEY_SIZE  16   /* 128 bits */
#define CRYPTO_BLOCK_SIZE 8   /* 64-bit blocks (simplified Feistel) */
#define CRYPTO_ROUNDS    4    /* default rounds — weak! */

typedef struct {
    uint8_t  key[CRYPTO_KEY_SIZE];
    uint32_t rounds;
    /* BUG: key_schedule is NOT stored — derived on every encrypt/decrypt call */
} CryptoContext;

/* Initialize with a key. Returns 0 on success. */
int  crypto_init(CryptoContext *ctx, const uint8_t *key, uint32_t key_len);

/* Encrypt 'len' bytes of 'plaintext' into 'ciphertext'.
 * Both buffers must be at least 'len' bytes.
 * Returns number of bytes encrypted, or 0 on error. */
uint32_t crypto_encrypt(CryptoContext *ctx, const uint8_t *plaintext,
                        uint8_t *ciphertext, uint32_t len);

/* Decrypt 'len' bytes of 'ciphertext' into 'plaintext'.
 * Returns number of bytes decrypted, or 0 on error. */
uint32_t crypto_decrypt(CryptoContext *ctx, const uint8_t *ciphertext,
                        uint8_t *plaintext, uint32_t len);

/* --- NEW: AI must implement --- */

/* Generate round keys from the master key using a key schedule.
 * Stores them in 'round_keys' (must be CRYPTO_ROUNDS * CRYPTO_BLOCK_SIZE bytes).
 * Returns number of round keys generated. */
uint32_t crypto_key_schedule(const CryptoContext *ctx, uint8_t *round_keys);

/* The Feistel round function F(R, K):
 *   R is a 32-bit half-block, K is a 64-bit round key.
 *   Must implement: expand R to 48 bits, XOR with K (48 bits from K),
 *   apply 8× 6→4 S-boxes (use standard DES S-box 1 for all),
 *   permute with the P-permutation.
 * Returns the 32-bit output. */
uint32_t crypto_feistel_f(uint32_t R, const uint8_t K[8]);

#endif /* CRYPTO_H */
