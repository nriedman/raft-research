#include "../log.h"

static int f_log_read(int idx, void *entry, int n, void *ctx) {
    return 0;
}

static int f_log_write(int idx, void *entry, int n, void *ctx) {
    return 0;
}

static int f_log_entry_size(int idx, void *ctx) {
    return 0;
}

static int f_log_start_index(void *ctx) {
    return 0;
}

static int f_log_end_index(void *ctx) {
    return 0;
}

log_t log_init(void) {

    // TODO: Check if a log was already inited, and restore from there if so.
    //       Otherwise, open a new file and store the fd in context.
    // NOTE: Make sure writes to persistent storage are synchronous.

    log_t lg = {
        .read = f_log_read,
        .write = f_log_write,
        .entry_size = f_log_entry_size,
        .start_index = f_log_start_index,
        .end_index = f_log_end_index,
        .context = 0
    };
    return lg;
}

void log_free(log_t *log) {
    return;
}
