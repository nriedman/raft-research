#include "../log.h"
#include "../log-entry.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct log_context {
    int log_fd;
    int meta_fd;
    uint32_t length;
} log_context_t;

static int f_log_read(int idx, void *entry, int n, void *ctx) {
    log_context_t *lctx = (log_context_t *)ctx;
    if (idx < 0 || (uint32_t)idx >= lctx->length) return EOBND;
    
    if (lseek(lctx->log_fd, (off_t)idx * log_entry_packed_size(), SEEK_SET) == -1) return -1;
    
    uint8_t buf[log_entry_packed_size()];
    if (read(lctx->log_fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) return -1;
    
    log_entry_unpack(buf, (log_entry_t *)entry);
    return n;
}

static int f_log_write(int idx, void *entry, int n, void *ctx) {
    log_context_t *lctx = (log_context_t *)ctx;
    if (idx < 0 || (uint32_t)idx > lctx->length) return EOBND;

    if (lseek(lctx->log_fd, (off_t)idx * log_entry_packed_size(), SEEK_SET) == -1) return -1;
    
    uint8_t buf[log_entry_packed_size()];
    log_entry_pack(buf, (const log_entry_t *)entry);
    
    if (write(lctx->log_fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) return -1;
    fsync(lctx->log_fd);

    if ((uint32_t)idx == lctx->length) {
        lctx->length++;
        if (lseek(lctx->meta_fd, 0, SEEK_SET) == -1) return -1;
        if (write(lctx->meta_fd, &lctx->length, sizeof(lctx->length)) != sizeof(lctx->length)) return -1;
        fsync(lctx->meta_fd);
    }

    return 0;
}

static int f_log_entry_size(int idx, void *ctx) {
    (void)idx; (void)ctx;
    return log_entry_packed_size();
}

static int f_log_start_index(void *ctx) {
    (void)ctx;
    return 0;
}

static int f_log_end_index(void *ctx) {
    log_context_t *lctx = (log_context_t *)ctx;
    return (int)lctx->length - 1;
}

static int f_log_length(void *ctx) {
    log_context_t *lctx = (log_context_t *)ctx;
    return (int)lctx->length;
}

static int f_log_remove_last_n(int n, void *ctx) {
    log_context_t *lctx = (log_context_t *)ctx;
    if (n <= 0) return 0;
    if ((uint32_t)n > lctx->length) n = (int)lctx->length;
    
    lctx->length -= n;
    if (lseek(lctx->meta_fd, 0, SEEK_SET) == -1) return -1;
    if (write(lctx->meta_fd, &lctx->length, sizeof(lctx->length)) != sizeof(lctx->length)) return -1;
    fsync(lctx->meta_fd);
    
    ftruncate(lctx->log_fd, (off_t)lctx->length * log_entry_packed_size());
    return n;
}

log_t log_init(uint32_t node_id) {
    char log_name[64], meta_name[64];
    sprintf(log_name, "raft_%u.log", node_id);
    sprintf(meta_name, "raft_%u.log.meta", node_id);

    log_context_t *ctx = malloc(sizeof(log_context_t));
    ctx->log_fd = open(log_name, O_RDWR | O_CREAT, 0644);
    ctx->meta_fd = open(meta_name, O_RDWR | O_CREAT, 0644);
    
    if (ctx->log_fd == -1 || ctx->meta_fd == -1) {
        perror("open log files");
        exit(1);
    }
    
    struct stat st;
    fstat(ctx->meta_fd, &st);
    if (st.st_size < (off_t)sizeof(uint32_t)) {
        ctx->length = 0;
        write(ctx->meta_fd, &ctx->length, sizeof(ctx->length));
        fsync(ctx->meta_fd);
    } else {
        lseek(ctx->meta_fd, 0, SEEK_SET);
        read(ctx->meta_fd, &ctx->length, sizeof(ctx->length));
    }

    log_t lg = {
        .read = f_log_read,
        .write = f_log_write,
        .entry_size = f_log_entry_size,
        .start_index = f_log_start_index,
        .end_index = f_log_end_index,
        .length = f_log_length,
        .remove_last_n = f_log_remove_last_n,
        .context = ctx
    };
    return lg;
}

void log_free(log_t *log) {
    if (!log || !log->context) return;
    log_context_t *ctx = (log_context_t *)log->context;
    close(ctx->log_fd);
    close(ctx->meta_fd);
    free(ctx);
    log->context = NULL;
}
