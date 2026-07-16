#ifndef PARSER_H
#define PARSER_H

#include "common.h"

/* Protocol message types */
typedef enum {
    MSG_PUT     = 0x01,
    MSG_GET     = 0x02,
    MSG_DEL     = 0x03,
    MSG_LIST    = 0x04,
    MSG_STATS   = 0x05,
    MSG_ERROR   = 0xFF
} MsgType;

typedef struct {
    MsgType  type;
    uint8_t  key[64];
    uint32_t key_len;
    uint8_t  value[512];
    uint32_t value_len;
    uint32_t ttl;
    bool     compressed;
    uint16_t checksum;       /* CRC-16 of payload */
} ParsedMessage;

/* Parse a raw buffer into a ParsedMessage.
 * Returns ERR_NONE on success, or an error code. */
int  parse_message(const uint8_t *raw, uint32_t raw_len, ParsedMessage *out);

/* Serialize a ParsedMessage into a buffer.
 * Returns number of bytes written, or 0 on error. */
uint32_t serialize_message(const ParsedMessage *msg, uint8_t *buf, uint32_t buf_len);

/* --- NEW: AI must implement --- */

/* Validate a parsed message: check checksum, key/value lengths, TTL sanity.
 * Returns true if valid, false with error details in 'err_msg'. */
bool validate_message(const ParsedMessage *msg, char *err_msg, uint32_t err_msg_len);

/* Parse with error recovery: attempt to find the next valid message boundary
 * after a parse error. Returns offset of next valid message, or -1. */
int32_t recover_after_error(const uint8_t *raw, uint32_t raw_len, uint32_t error_offset);

#endif /* PARSER_H */
