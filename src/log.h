#pragma once

#include <stdint.h>

// Error codes that log functions may return.
enum {
    EOBND = -51,        // an index was out of bounds
    EINIT = -52         // log methods called before init
};

// Synchronously read at most <n> bytes of the log entry at index <idx> from persistent
// storage.
// 
// Returns number of bytes read into <entry> on success,
// or a code <0 on error (see <log.h>).
typedef int (*log_read_entry_t)(int idx, void *entry, int n, void *ctx);

// Synchronously write a given log entry to persistent storage at index <idx>.
//
// If <idx> is equal to the current log length, the entry is appended to the log
// and the log length is incremented.
//
// Returns 0 on success, or a code <0 on error (see <log.h>).
typedef int (*log_write_entry_t)(int idx, void *entry, int n, void *ctx);

// Returns the size of the <idx>'th log entry in bytes,
// or a code <0 on error (see <log.h>).
typedef int (*log_entry_size_t)(int idx, void *ctx);

// Returns the index of the first element in the log,
// or a code <0 on error (see <log.h>).
typedef int (*log_start_index_t)(void *ctx);

// Returns the index of the last element in the log,
// or a code <0 on error (see <log.h>).
typedef int (*log_end_index_t)(void *ctx);

// Returns the number of entries stored in the log,
// or a code <0 on error (see <log.h).
typedef int (*log_length_t)(void *ctx);

// Truncates the log by removing the last <n> entries.
//
// Will not remove more entries than there are in the log.
//
// Returns number of entries removed on success,
// or a code <0 on error (see <log.h).
typedef int (*log_remove_last_n_t)(int n, void *ctx);

typedef struct {
    log_read_entry_t read;
    log_write_entry_t write;
    log_entry_size_t entry_size;
    log_start_index_t start_index;
    log_end_index_t end_index;
    log_length_t length;
    log_remove_last_n_t remove_last_n;
    void *context;
} log_t;

log_t log_init(uint32_t node_id);
void log_free(log_t *log);
