#ifndef CRYPTO_H
#define CRYPTO_H

#include "common.h"

#define CRYPTO_KEY_SIZE   16
#define CRYPTO_BLOCK_SIZE 8
#define CRYPTO_ROUNDS     4

typedef struct {
    uint8_t  key[CRYPTO_KEY_SIZE];
    uint32_t rounds;
    /* BUG: key_schedule not stored */
} CryptoContext;

int      crypto_init(CryptoContext *ctx, const uint8_t *key, uint32_t key_len);
uint32_t crypto_encrypt(CryptoContext *ctx, const uint8_t *plaintext, uint8_t *ciphertext, uint32_t len);
uint32_t crypto_decrypt(CryptoContext *ctx, const uint8_t *ciphertext, uint8_t *plaintext, uint32_t len);

/* --- NEW: AI must implement --- */
uint32_t crypto_key_schedule(const CryptoContext *ctx, uint8_t *round_keys);
uint32_t crypto_feistel_f(uint32_t R, const uint8_t K[8]);

#endif /* CRYPTO_H */
