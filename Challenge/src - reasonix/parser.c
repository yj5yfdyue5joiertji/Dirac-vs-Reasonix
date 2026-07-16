#include "parser.h"
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

/* ── Internal CRC-16 (CCITT-style, same as before) ────────────────── */

static uint16_t calc_crc16(const uint8_t *data, uint32_t len) {
    uint16_t crc = 0xFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* ── Message format:
 *   [1B type][4B total_len][2B checksum][4B key_len][key...][4B value_len][value...]
 *   total_len = 1 + 4 + 2 + 4 + key_len + 4 + value_len  (header included)
 * ─────────────────────────────────────────────────────────────────── */

int parse_message(const uint8_t *raw, uint32_t raw_len, ParsedMessage *out) {
    /* FIXED: NULL checks */
    if (raw == NULL || out == NULL) return ERR_PARSE_FAILED;

    /* FIXED: minimum length check (need at least 11 bytes for header) */
    if (raw_len < 11) return ERR_PARSE_FAILED;

    memset(out, 0, sizeof(ParsedMessage));

    /* FIXED: validate type */
    uint8_t msg_type = raw[0];
    switch (msg_type) {
        case MSG_PUT: case MSG_GET: case MSG_DEL:
        case MSG_LIST: case MSG_STATS: case MSG_ERROR:
            out->type = (MsgType)msg_type;
            break;
        default:
            return ERR_PARSE_FAILED;
    }

    /* Parse total_len (using memcpy to avoid alignment issues) */
    uint32_t total_len;
    memcpy(&total_len, raw + 1, sizeof(uint32_t));
    total_len = ntohl(total_len);

    /* FIXED: validate total_len against raw_len */
    if (total_len > raw_len || total_len < 11) return ERR_PARSE_FAILED;

    /* Parse checksum */
    uint16_t checksum;
    memcpy(&checksum, raw + 5, sizeof(uint16_t));
    out->checksum = ntohs(checksum);

    /* Parse key_len (using memcpy to avoid alignment UB) */
    uint32_t key_len;
    memcpy(&key_len, raw + 7, sizeof(uint32_t));
    key_len = ntohl(key_len);

    /* FIXED: validate key_len */
    if (key_len > 64) return ERR_PARSE_FAILED;
    out->key_len = key_len;

    /* FIXED: validate we have enough data for key + value header */
    uint32_t val_header_off = 11 + key_len;
    if (val_header_off + 4 > raw_len) return ERR_PARSE_FAILED;

    if (key_len > 0) {
        memcpy(out->key, raw + 11, key_len);
    }

    /* Parse value_len */
    uint32_t value_len;
    memcpy(&value_len, raw + val_header_off, sizeof(uint32_t));
    value_len = ntohl(value_len);

    /* FIXED: validate value_len */
    if (value_len > 512) return ERR_PARSE_FAILED;
    out->value_len = value_len;

    /* FIXED: validate total_len matches */
    uint32_t expected_total = 1 + 4 + 2 + 4 + key_len + 4 + value_len;
    if (expected_total != total_len) return ERR_PARSE_FAILED;

    uint32_t val_data_off = val_header_off + 4;
    if (val_data_off + value_len > raw_len) return ERR_PARSE_FAILED;

    if (value_len > 0) {
        memcpy(out->value, raw + val_data_off, value_len);
    }

    /* Verify checksum: CRC over everything after the 2-byte checksum field
     * (bytes at offset 7 through end of message) */
    uint32_t crc_range_start = 7;
    uint32_t crc_range_len = total_len - crc_range_start;
    uint16_t computed_crc = calc_crc16(raw + crc_range_start, crc_range_len);
    if (computed_crc != out->checksum) return ERR_PARSE_FAILED;

    /* TTL is not in the wire format, set default */
    out->ttl = 0;
    out->compressed = false;

    return ERR_NONE;
}

uint32_t serialize_message(const ParsedMessage *msg, uint8_t *buf, uint32_t buf_len) {
    /* FIXED: NULL check */
    if (msg == NULL || buf == NULL) return 0;

    uint32_t total = 1 + 4 + 2 + 4 + msg->key_len + 4 + msg->value_len;

    /* FIXED: check for overflow and buf_len */
    if (total > buf_len || total < 11) return 0;
    if (msg->key_len > 64 || msg->value_len > 512) return 0;

    buf[0] = msg->type;

    /* Use memcpy for alignment safety */
    uint32_t net_total = htonl(total);
    memcpy(buf + 1, &net_total, sizeof(uint32_t));

    /* Checksum placeholder (will be recalculated) */
    uint16_t net_crc = htons(0);
    memcpy(buf + 5, &net_crc, sizeof(uint16_t));

    uint32_t net_key_len = htonl(msg->key_len);
    memcpy(buf + 7, &net_key_len, sizeof(uint32_t));

    if (msg->key_len > 0) {
        memcpy(buf + 11, msg->key, msg->key_len);
    }

    uint32_t val_off = 11 + msg->key_len;
    uint32_t net_val_len = htonl(msg->value_len);
    memcpy(buf + val_off, &net_val_len, sizeof(uint32_t));

    if (msg->value_len > 0) {
        memcpy(buf + val_off + 4, msg->value, msg->value_len);
    }

    /* Calculate checksum over bytes from offset 7 onwards */
    uint16_t crc = calc_crc16(buf + 7, total - 7);
    net_crc = htons(crc);
    memcpy(buf + 5, &net_crc, sizeof(uint16_t));

    return total;
}

/* ── IMPLEMENTED: validate_message ────────────────────────────────── */

bool validate_message(const ParsedMessage *msg, char *err_msg, uint32_t err_msg_len) {
    if (msg == NULL) {
        if (err_msg && err_msg_len > 0) {
            snprintf(err_msg, err_msg_len, "NULL message pointer");
        }
        return false;
    }

    /* Validate type */
    switch (msg->type) {
        case MSG_PUT: case MSG_GET: case MSG_DEL:
        case MSG_LIST: case MSG_STATS: case MSG_ERROR:
            break;
        default:
            if (err_msg && err_msg_len > 0) {
                snprintf(err_msg, err_msg_len,
                         "Invalid message type: 0x%02X", msg->type);
            }
            return false;
    }

    /* Validate key length: 1-64 */
    if (msg->key_len == 0 || msg->key_len > 64) {
        if (err_msg && err_msg_len > 0) {
            snprintf(err_msg, err_msg_len,
                     "Invalid key length: %u (must be 1-64)", msg->key_len);
        }
        return false;
    }

    /* Validate value length: 0-512 */
    if (msg->value_len > 512) {
        if (err_msg && err_msg_len > 0) {
            snprintf(err_msg, err_msg_len,
                     "Invalid value length: %u (must be 0-512)", msg->value_len);
        }
        return false;
    }

    /* Validate TTL: 0 or >= 100 */
    if (msg->ttl != 0 && msg->ttl < 100) {
        if (err_msg && err_msg_len > 0) {
            snprintf(err_msg, err_msg_len,
                     "Invalid TTL: %u (must be 0 or >= 100)", msg->ttl);
        }
        return false;
    }

    /* NOTE: checksum is validated during parse_message, not here,
     * since validate_message receives an already-parsed struct
     * and we don't have the raw bytes to recompute CRC. */

    return true;
}

/* ── IMPLEMENTED: recover_after_error ─────────────────────────────── */

int32_t recover_after_error(const uint8_t *raw, uint32_t raw_len,
                             uint32_t error_offset) {
    if (raw == NULL || raw_len == 0) return -1;

    /* Start scanning from error_offset + 1 */
    for (uint32_t i = error_offset + 1; i < raw_len; i++) {
        uint8_t byte = raw[i];
        switch (byte) {
            case MSG_PUT: case MSG_GET: case MSG_DEL:
            case MSG_LIST: case MSG_STATS: case MSG_ERROR:
                /* Found a valid message type byte */
                return (int32_t)i;
            default:
                break;
        }
    }

    return -1;
}
