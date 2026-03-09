// transport functions for Unix -- using pipes

#include "transport-pipe.h"

#include <stdio.h>

#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>

static int pipe_send(const pkt_t *pkt, void *ctx) {
    pipe_context_t *pctx = (pipe_context_t *)ctx;

    ssize_t bytes_to_write = sizeof(*pkt);
    ssize_t n = write(pctx->write_fd, pkt, bytes_to_write);

    // NOTE: might fail when writing to non-blocking IO on
    //       objects which are flow controlled, like sockets
    //       (<write> may write fewer bytes than requested)
    return (n == bytes_to_write) ? 0 : -1;
}

static int pipe_receive(const pkt_t *pkt, const uint32_t timeout_ms, void *ctx) {
    pipe_context_t *pctx = (pipe_context_t *)ctx;
    
    // we can use <select> for system-provided timeout functionality
    //
    // https://ftp.gnu.org/old-gnu/Manuals/glibc-2.2.3/html_node/libc_238.html
    // -- also see `man 2 select`
    fd_set set;
    struct timeval timeout;

    // init file descriptor set (the only element is <read_fd>)
    FD_ZERO(&set);
    FD_SET(pctx->read_fd, &set);

    // init timeout datastruct (convert from ms -> s.us)
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(FD_SETSIZE, &set, NULL, NULL, &timeout);

    // <select> returns 0 on timeout, 1 on fd ready to read, -1 on error
    if (ret == 0)
        return 0;
    
    if (ret < 0) {
        fprintf(stderr, "<select> returned an error\n");
        return -1;
    }

    // read_fd has contents we can read from!
    ssize_t bytes_to_read = sizeof(*pkt);
    ssize_t n = read(pctx->read_fd, pkt, bytes_to_read);
    
    return (n == bytes_to_read) ? 1 : -1;
}

transport_t transport_pipe_create(int read_fd, int write_fd) {
    pipe_context_t *ctx = malloc(sizeof(pipe_context_t));

    ctx->read_fd = read_fd;
    ctx->write_fd = write_fd;

    transport_t transport = {
        .send = pipe_send,
        .receive = pipe_receive,
        .context = ctx
    };

    return transport;
}

void transport_pipe_destroy(transport_t *transport) {
    if (transport->context) {
        free(transport->context);
        transport->context = NULL;
    }
    // not bothering to free fildes in context... for now using stdin/stdout

}