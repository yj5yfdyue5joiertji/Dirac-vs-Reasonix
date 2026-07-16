#include "parser.h"
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

static uint16_t calc_crc16(const uint8_t *data, uint32_t len) {
    uint16_t crc = 0xFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* Message format: [1B type][4B total_len][2B checksum][4B key_len][key...][4B value_len][value...] */

int parse_message(const uint8_t *raw, uint32_t raw_len, ParsedMessage *out) {
    (void)raw_len;  /* BUG: raw_len never validated */
    /* BUG: no NULL check */
    memset(out, 0, sizeof(ParsedMessage));
    out->type = raw[0];
    uint32_t total_len = ntohl(*(uint32_t *)(raw + 1));
    (void)total_len;
    out->checksum = ntohs(*(uint16_t *)(raw + 5));
    uint32_t key_len = ntohl(*(uint32_t *)(raw + 7));
    out->key_len = key_len;
    if (key_len <= 64) memcpy(out->key, raw + 11, key_len);  /* BUG: overflow if > 64 */
    uint32_t val_offset = 11 + key_len;
    uint32_t value_len = ntohl(*(uint32_t *)(raw + val_offset));
    out->value_len = value_len;
    if (value_len <= 512) memcpy(out->value, raw + val_offset + 4, value_len);
    out->ttl = 0;  /* BUG: TTL not parsed */
    return ERR_NONE;
}

uint32_t serialize_message(const ParsedMessage *msg, uint8_t *buf, uint32_t buf_len) {
    /* BUG: no NULL check */
    uint32_t total = 1 + 4 + 2 + 4 + msg->key_len + 4 + msg->value_len;
    if (total > buf_len) return 0;  /* BUG: integer overflow */
    buf[0] = msg->type;
    *(uint32_t *)(buf + 1) = htonl(total);
    *(uint16_t *)(buf + 5) = htons(msg->checksum);
    *(uint32_t *)(buf + 7) = htonl(msg->key_len);
    memcpy(buf + 11, msg->key, msg->key_len);
    uint32_t val_off = 11 + msg->key_len;
    *(uint32_t *)(buf + val_off) = htonl(msg->value_len);
    memcpy(buf + val_off + 4, msg->value, msg->value_len);
    uint16_t crc = calc_crc16(buf + 1, total - 3);  /* BUG: wrong range */
    *(uint16_t *)(buf + 5) = htons(crc);
    return total;
}

/* ── STUB: AI must implement ──────────────────────────────────────── */

bool validate_message(const ParsedMessage *msg, char *err_msg, uint32_t err_msg_len) {
    (void)msg; (void)err_msg; (void)err_msg_len;
    fprintf(stderr, "validate_message: NOT IMPLEMENTED\n");
    return false;
}

int32_t recover_after_error(const uint8_t *raw, uint32_t raw_len, uint32_t error_offset) {
    (void)raw; (void)raw_len; (void)error_offset;
    fprintf(stderr, "recover_after_error: NOT IMPLEMENTED\n");
    return -1;
}
