#pragma once

#include <stdint.h>

typedef struct {
    uint32_t term;
    uint32_t client_id;
    uint32_t cmd_seqno;
    uint32_t cmd;
} log_entry_t;

uint8_t* log_entry_pack(uint8_t *buf, const log_entry_t *entry);
const uint8_t* log_entry_unpack(const uint8_t *buf, log_entry_t *entry);
uint32_t log_entry_packed_size(void);
