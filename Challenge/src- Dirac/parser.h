#ifndef PARSER_H
#define PARSER_H

#include "common.h"

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
    uint16_t checksum;
} ParsedMessage;

int      parse_message(const uint8_t *raw, uint32_t raw_len, ParsedMessage *out);
uint32_t serialize_message(const ParsedMessage *msg, uint8_t *buf, uint32_t buf_len);

/* --- NEW: AI must implement --- */
bool     validate_message(const ParsedMessage *msg, char *err_msg, uint32_t err_msg_len);
int32_t  recover_after_error(const uint8_t *raw, uint32_t raw_len, uint32_t error_offset);

#endif /* PARSER_H */
