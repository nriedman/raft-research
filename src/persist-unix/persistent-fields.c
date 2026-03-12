#include "../persistent-fields.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

typedef struct {
    int fd;
} pf_context_t;

static int pf_get(int f, void *ctx) {
    pf_context_t *pfc = (pf_context_t *)ctx;
    uint32_t val;
    if (lseek(pfc->fd, f * sizeof(uint32_t), SEEK_SET) == -1) return -1;
    if (read(pfc->fd, &val, sizeof(val)) != sizeof(val)) return -1;
    return (int)val;
}

static int pf_set(int f, uint32_t v, void *ctx) {
    pf_context_t *pfc = (pf_context_t *)ctx;
    if (lseek(pfc->fd, f * sizeof(uint32_t), SEEK_SET) == -1) return -1;
    if (write(pfc->fd, &v, sizeof(v)) != sizeof(v)) return -1;
    fsync(pfc->fd);
    return 0;
}

persistent_fields_t persistent_fields_init(void) {
    pf_context_t *ctx = malloc(sizeof(pf_context_t));
    ctx->fd = open("raft.state", O_RDWR | O_CREAT, 0644);
    if (ctx->fd == -1) {
        perror("open raft.state");
        exit(1);
    }
    
    struct stat st;
    if (fstat(ctx->fd, &st) == -1) {
        perror("fstat raft.state");
        exit(1);
    }

    if (st.st_size < (off_t)(2 * sizeof(uint32_t))) {
        uint32_t buf[2] = {0, PF_NO_VOTE_V};
        if (write(ctx->fd, buf, sizeof(buf)) != sizeof(buf)) {
            perror("write initial raft.state");
            exit(1);
        }
        fsync(ctx->fd);
    }

    persistent_fields_t pf = {
        .get = pf_get,
        .set = pf_set,
        .context = ctx
    };
    return pf;
}

void persistent_fields_free(persistent_fields_t *pf) {
    if (pf && pf->context) {
        pf_context_t *ctx = (pf_context_t *)pf->context;
        close(ctx->fd);
        free(ctx);
        pf->context = NULL;
    }
}
