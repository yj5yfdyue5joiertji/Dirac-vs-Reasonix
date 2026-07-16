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

/* Message format: [1B type][4B total_len][2B checksum][4B key_len][key...][4B value_len][value...][2B TTL (optional)] */

int parse_message(const uint8_t *raw, uint32_t raw_len, ParsedMessage *out) {
    if (raw == NULL || out == NULL) return ERR_PARSE_FAILED;
    if (raw_len < 15) return ERR_PARSE_FAILED;  /* minimum: 1+4+2+4+0+4+0 = 15 */

    memset(out, 0, sizeof(ParsedMessage));
    out->type = raw[0];

    /* Validate message type */
    if (out->type != MSG_PUT && out->type != MSG_GET &&
        out->type != MSG_DEL && out->type != MSG_LIST &&
        out->type != MSG_STATS && out->type != MSG_ERROR) {
        return ERR_PARSE_FAILED;
    }

    uint32_t total_len = ntohl(*(uint32_t *)(raw + 1));
    if (total_len > raw_len) return ERR_PARSE_FAILED;  /* FIXED: validate against raw_len */
    if (total_len < 15) return ERR_PARSE_FAILED;

    out->checksum = ntohs(*(uint16_t *)(raw + 5));
    uint32_t key_len = ntohl(*(uint32_t *)(raw + 7));

    /* FIXED: bounds check key_len */
    if (key_len > 64) return ERR_PARSE_FAILED;
    if (11 + key_len > raw_len) return ERR_PARSE_FAILED;

    out->key_len = key_len;
    if (key_len > 0) memcpy(out->key, raw + 11, key_len);

    uint32_t val_offset = 11 + key_len;
    if (val_offset + 4 > raw_len) return ERR_PARSE_FAILED;

    uint32_t value_len = ntohl(*(uint32_t *)(raw + val_offset));

    /* FIXED: bounds check value_len */
    if (value_len > 512) return ERR_PARSE_FAILED;
    if (val_offset + 4 + value_len > raw_len) return ERR_PARSE_FAILED;

    out->value_len = value_len;
    if (value_len > 0) memcpy(out->value, raw + val_offset + 4, value_len);

    /* Try to parse TTL if present */
    uint32_t ttl_offset = val_offset + 4 + value_len;
    if (ttl_offset + 2 <= total_len) {
        out->ttl = ntohs(*(uint16_t *)(raw + ttl_offset));
    } else {
        out->ttl = 0;
    }

    return ERR_NONE;
}

uint32_t serialize_message(const ParsedMessage *msg, uint8_t *buf, uint32_t buf_len) {
    if (msg == NULL || buf == NULL) return 0;
    if (msg->key_len > 64 || msg->value_len > 512) return 0;

    uint32_t total = 1 + 4 + 2 + 4 + msg->key_len + 4 + msg->value_len;
    if (total > buf_len) return 0;

    buf[0] = msg->type;
    *(uint32_t *)(buf + 1) = htonl(total);
    *(uint16_t *)(buf + 5) = 0;  /* placeholder, filled below */
    *(uint32_t *)(buf + 7) = htonl(msg->key_len);
    if (msg->key_len > 0) memcpy(buf + 11, msg->key, msg->key_len);
    uint32_t val_off = 11 + msg->key_len;
    *(uint32_t *)(buf + val_off) = htonl(msg->value_len);
    if (msg->value_len > 0) memcpy(buf + val_off + 4, msg->value, msg->value_len);

    /* FIXED: CRC over correct range: from type byte (offset 0) through end of value,
       but excluding the checksum field itself (bytes 5-6).
       CRC over: [0] + [1..4] (total_len) + [7..total-1] (key_len through value) */
    /* Simplified: CRC over the entire message from byte 0, excluding bytes 5-6 */
    uint16_t crc = calc_crc16(buf, 5);  /* bytes 0-4 */
    /* Combine: CRC of two parts by feeding first CRC as initial value to second */
    crc = calc_crc16(buf + 7, total - 7);  /* Actually, just CRC the whole thing minus checksum */
    /* The cleanest fix: CRC everything except the checksum field */
    /* Recalculate: CRC over type (1B) + total_len (4B) + key_len (4B) + key + value_len (4B) + value */
    /* = CRC over buf[0..4] then buf[7..total-1] */
    /* We'll do a two-part CRC: */
    crc = 0xFFFF;
    /* Process byte 0 */
    crc ^= (uint16_t)buf[0] << 8;
    for (int j = 0; j < 8; j++) {
        if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
        else crc = (uint16_t)(crc << 1);
    }
    /* Process bytes 1-4 */
    for (uint32_t i = 1; i <= 4; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else crc = (uint16_t)(crc << 1);
        }
    }
    /* Process bytes 7..total-1 */
    for (uint32_t i = 7; i < total; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else crc = (uint16_t)(crc << 1);
        }
    }

    *(uint16_t *)(buf + 5) = htons(crc);
    return total;
}

/* ── validate_message ────────────────────────────────────────────── */

bool validate_message(const ParsedMessage *msg, char *err_msg, uint32_t err_msg_len) {
    if (msg == NULL) {
        if (err_msg && err_msg_len > 0) {
            snprintf(err_msg, err_msg_len, "NULL message pointer");
        }
        return false;
    }

    /* Type must be a known MsgType */
    if (msg->type != MSG_PUT && msg->type != MSG_GET &&
        msg->type != MSG_DEL && msg->type != MSG_LIST &&
        msg->type != MSG_STATS && msg->type != MSG_ERROR) {
        if (err_msg && err_msg_len > 0) {
            snprintf(err_msg, err_msg_len, "Unknown message type: 0x%02X", msg->type);
        }
        return false;
    }

    /* Key length must be 1-64 */
    if (msg->key_len < 1 || msg->key_len > 64) {
        if (err_msg && err_msg_len > 0) {
            snprintf(err_msg, err_msg_len, "Invalid key length: %u (must be 1-64)", msg->key_len);
        }
        return false;
    }

    /* Value length must be 0-512 */
    if (msg->value_len > 512) {
        if (err_msg && err_msg_len > 0) {
            snprintf(err_msg, err_msg_len, "Invalid value length: %u (must be 0-512)", msg->value_len);
        }
        return false;
    }

    /* TTL must be 0 or >= 100 (no tiny TTLs) */
    if (msg->ttl != 0 && msg->ttl < 100) {
        if (err_msg && err_msg_len > 0) {
            snprintf(err_msg, err_msg_len, "Invalid TTL: %u (must be 0 or >= 100)", msg->ttl);
        }
        return false;
    }

    /* Checksum: serialize to a buffer and recalculate */
    uint8_t buf[2048];
    uint32_t total = 1 + 4 + 2 + 4 + msg->key_len + 4 + msg->value_len;
    if (total > sizeof(buf)) {
        if (err_msg && err_msg_len > 0) {
            snprintf(err_msg, err_msg_len, "Message too large to validate: %u bytes", total);
        }
        return false;
    }

    buf[0] = msg->type;
    *(uint32_t *)(buf + 1) = htonl(total);
    /* Leave checksum bytes at 5-6 as zero for CRC calculation */
    *(uint16_t *)(buf + 5) = 0;
    *(uint32_t *)(buf + 7) = htonl(msg->key_len);
    if (msg->key_len > 0) memcpy(buf + 11, msg->key, msg->key_len);
    uint32_t val_off = 11 + msg->key_len;
    *(uint32_t *)(buf + val_off) = htonl(msg->value_len);
    if (msg->value_len > 0) memcpy(buf + val_off + 4, msg->value, msg->value_len);

    /* Calculate CRC over the same range as serialize_message: bytes 0-4 and 7..total-1 */
    uint16_t calc_crc = 0xFFFF;
    /* Byte 0 */
    calc_crc ^= (uint16_t)buf[0] << 8;
    for (int j = 0; j < 8; j++) {
        if (calc_crc & 0x8000) calc_crc = (uint16_t)((calc_crc << 1) ^ 0x1021);
        else calc_crc = (uint16_t)(calc_crc << 1);
    }
    /* Bytes 1-4 */
    for (uint32_t i = 1; i <= 4; i++) {
        calc_crc ^= (uint16_t)buf[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (calc_crc & 0x8000) calc_crc = (uint16_t)((calc_crc << 1) ^ 0x1021);
            else calc_crc = (uint16_t)(calc_crc << 1);
        }
    }
    /* Bytes 7..total-1 */
    for (uint32_t i = 7; i < total; i++) {
        calc_crc ^= (uint16_t)buf[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (calc_crc & 0x8000) calc_crc = (uint16_t)((calc_crc << 1) ^ 0x1021);
            else calc_crc = (uint16_t)(calc_crc << 1);
        }
    }

    if (calc_crc != msg->checksum) {
        if (err_msg && err_msg_len > 0) {
            snprintf(err_msg, err_msg_len, "Checksum mismatch: got 0x%04X, expected 0x%04X",
                     msg->checksum, calc_crc);
        }
        return false;
    }

    return true;
}

/* ── recover_after_error ─────────────────────────────────────────── */

int32_t recover_after_error(const uint8_t *raw, uint32_t raw_len, uint32_t error_offset) {
    if (raw == NULL || raw_len == 0) return -1;
    if (error_offset >= raw_len) return -1;

    /* Scan forward from error_offset + 1 to find next valid message boundary */
    for (uint32_t i = error_offset + 1; i < raw_len; i++) {
        uint8_t byte = raw[i];
        /* Valid MsgType bytes: 0x01-0x05, 0xFF */
        if ((byte >= 0x01 && byte <= 0x05) || byte == 0xFF) {
            /* Check if there's enough room for a minimal message */
            if (raw_len - i >= 15) {
                return (int32_t)i;
            }
        }
    }
    return -1;
}
