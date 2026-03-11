#include "../log.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct log_context {
    FILE *meta_fd;              // Access to the log meta-data file.
    char *meta_filename;
    FILE *log_fd;               // Access to the log entries themselves.
    char *log_filename;
    size_t log_length;          // Volatile-mem log length. Store to meta-data before updating.
} log_context_t;

// TODO: Make a real persistent storage mechanism
typedef struct cached_entry {
    int n;
    char *data;
} cached_entry_t;

size_t cached_log_len = 0;
cached_entry_t *cached_log = NULL;

static int f_log_read(int idx, void *entry, int n, void *ctx) {

    if (!cached_log) {
        return -1;
    }

    if (idx < 0 || idx >= cached_log_len) {
        return -1;
    }

    memcpy(entry, cached_log[idx].data, n);

    return n;
}

static int f_log_write(int idx, void *entry, int n, void *ctx) {
    fprintf(stderr, "[Log] writing %d bytes to entry %d\n", n, idx);

    if (!cached_log) {
        return -1;
    }

    if (idx < 0 || idx > cached_log_len) {
        return -1;
    }

    char *data = malloc(n);
    memcpy(data, entry, n);

    cached_log_len += (idx == cached_log_len);

    cached_log[idx].data = data;
    cached_log[idx].n = n;

    return 0;
}

static int f_log_entry_size(int idx, void *ctx) {
    return cached_log_len;
}

static int f_log_start_index(void *ctx) {
    return 0;
}

static int f_log_end_index(void *ctx) {
    return cached_log_len > 0 ? -1 : cached_log_len - 1;
}

static int f_log_length(void *ctx) {
    return cached_log_len;
}

const char *meta_data_filename = "metadata.log";

// Checks to see if a log is already stored in the current
// working directory.
// 
// If so, returns a <log_context_t> created from the data on disk.
// Otherwise, returns NULL.
static log_context_t *recover_log(void) {

    return NULL;

}

static log_context_t *start_new_log(void) {
    log_context_t *ctx = malloc(sizeof(*ctx));
    ctx->log_fd = 0;
    ctx->log_filename = "none";
    ctx->log_length = 0;
    ctx->meta_fd = 0;
    ctx->meta_filename = "none";

    // sneaky lil volatile memory dup
    cached_log = malloc(1024 * sizeof(*cached_log));
    cached_log_len = 0;

    return ctx;
}

log_t log_init(void) {
    // Check if a log was already inited, and restore from there if so.
    // Otherwise, open a new file and store the fd in context.
    // NOTE: Make sure writes to persistent storage are synchronous.
    log_context_t *ctx = recover_log();
    if (ctx == NULL) {
        ctx = start_new_log();
    }

    log_t lg = {
        .read = f_log_read,
        .write = f_log_write,
        .entry_size = f_log_entry_size,
        .start_index = f_log_start_index,
        .end_index = f_log_end_index,
        .length = f_log_length,
        .context = ctx
    };
    return lg;
}

void log_free(log_t *log) {
    if (!log)
        return;

    free(log->context);

    if (cached_log) {
        for (int i = 0; i < cached_log_len; i++) {
            free(cached_log[i].data);
        }
        free(cached_log);
    }

    return;
}
