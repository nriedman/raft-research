#pragma once

#include <stdint.h>

#define MAX_LOG_LEN 1024

typedef struct {
    uint32_t cmd;
    uint32_t term;
} log_entry_t;

uint8_t* log_entry_pack(uint8_t *buf, const log_entry_t *entry);
const uint8_t* log_entry_unpack(const uint8_t *buf, log_entry_t *entry);
uint32_t log_entry_packed_size(void);
