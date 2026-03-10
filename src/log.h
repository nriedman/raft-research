#pragma once

#include <stdint.h>

#define MAX_LOG_LEN 1024

typedef struct {
    uint32_t term;
    uint32_t client_id;
    uint32_t cmd_seqno;
    uint32_t cmd;
} log_entry_t;

uint8_t* log_entry_pack(uint8_t *buf, const log_entry_t *entry);
const uint8_t* log_entry_unpack(const uint8_t *buf, log_entry_t *entry);
uint32_t log_entry_packed_size(void);

// Synchronously write a given log entry to persistent storage at index <idx>.
//
// If <idx> is equal to the current log length, the entry is appended to the log
// and the log length is incremented.
//
// Returns 0 on success, -1 on error.
typedef int (*log_write_entry_t)(int idx, void *entry, int n, void *ctx);

// Synchronously read at most <n> bytes of the log entry at index <idx> from persistent
// storage.
// 
// Returns number of bytes read into <entry> on success, -1 on error.
typedef int (*log_read_entry_t)(int idx, void *entry, int n, void *ctx);

// Returns the size of the <idx>'th log entry in bytes, or -1 on error.
typedef int (*log_entry_size_t)(int idx);

// Returns the index of the first element in the log, or -1 on error.
typedef int (*log_start_t)(void);

// Returns the index of the last element in the log, or -1 on error.
typedef int (*log_end_t)(void);

typedef struct {
    log_read_entry_t read;
    log_write_entry_t write;
    log_entry_size_t entry_size;
    log_start_t start;
    log_end_t end;
    void *context;
} log_t;

log_t log_init(void);
void log_free(log_t *log);
