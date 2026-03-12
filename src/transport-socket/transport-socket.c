#include "transport-socket.h"
#include "../util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <fcntl.h>

#define MAX_CONNECTIONS 1024

typedef struct {
    uint32_t my_id;
    uint32_t num_nodes;
    char **peer_addrs;
    int listen_fd;
    
    // peer_fds[i] is the outgoing socket to node i.
    int *peer_fds; 
    
    // client_fds[client_id % MAX_CONNECTIONS] = fd
    int client_fds[MAX_CONNECTIONS];
    
    // All currently active fds for select()
    int active_fds[MAX_CONNECTIONS];
    int num_active;
} socket_context_t;

static int parse_addr(const char *addr_str, struct sockaddr_in *addr) {
    char buf[256];
    strncpy(buf, addr_str, sizeof(buf));
    char *colon = strchr(buf, ':');
    if (!colon) return -1;
    *colon = '\0';
    int port = atoi(colon + 1);
    
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    if (inet_pton(AF_INET, buf, &addr->sin_addr) <= 0) return -1;
    return 0;
}

static int connect_to_peer(socket_context_t *sctx, uint32_t node_id) {
    if (node_id >= sctx->num_nodes) return -1;
    
    struct sockaddr_in addr;
    if (parse_addr(sctx->peer_addrs[node_id], &addr) < 0) return -1;
    
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    
    sctx->peer_fds[node_id] = fd;
    return fd;
}

static int socket_send(const pkt_t *pkt, void *ctx) {
    socket_context_t *sctx = (socket_context_t *)ctx;
    uint32_t dst = pkt->header.dst;
    int fd = -1;
    
    if (dst < sctx->num_nodes) {
        fd = sctx->peer_fds[dst];
        if (fd == -1) {
            fd = connect_to_peer(sctx, dst);
        }
    } else {
        fd = sctx->client_fds[dst % MAX_CONNECTIONS];
    }
    
    if (fd == -1) return -1;
    
    ssize_t n = write(fd, pkt, sizeof(*pkt));
    if (n != (ssize_t)sizeof(*pkt)) {
        close(fd);
        if (dst < sctx->num_nodes) sctx->peer_fds[dst] = -1;
        else sctx->client_fds[dst % MAX_CONNECTIONS] = -1;
        return -1;
    }
    
    return 0;
}

static int socket_receive(const pkt_t *pkt, const uint32_t timeout_ms, void *ctx) {
    socket_context_t *sctx = (socket_context_t *)ctx;
    
    fd_set read_fds;
    int max_fd = sctx->listen_fd;
    
    FD_ZERO(&read_fds);
    FD_SET(sctx->listen_fd, &read_fds);
    
    for (int i = 0; i < sctx->num_active; i++) {
        FD_SET(sctx->active_fds[i], &read_fds);
        if (sctx->active_fds[i] > max_fd) max_fd = sctx->active_fds[i];
    }
    
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    
    int ret = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
    if (ret == 0) return 0;
    if (ret < 0) return -1;
    
    if (FD_ISSET(sctx->listen_fd, &read_fds)) {
        int new_fd = accept(sctx->listen_fd, NULL, NULL);
        if (new_fd >= 0) {
            if (sctx->num_active < MAX_CONNECTIONS) {
                sctx->active_fds[sctx->num_active++] = new_fd;
            } else {
                close(new_fd);
            }
        }
    }
    
    for (int i = 0; i < sctx->num_active; i++) {
        int fd = sctx->active_fds[i];
        if (FD_ISSET(fd, &read_fds)) {
            ssize_t n = read(fd, (void *)pkt, sizeof(*pkt));
            if (n == (ssize_t)sizeof(*pkt)) {
                if (pkt->header.src >= sctx->num_nodes) {
                    sctx->client_fds[pkt->header.src % MAX_CONNECTIONS] = fd;
                }
                return 1;
            } else {
                close(fd);
                sctx->active_fds[i] = sctx->active_fds[sctx->num_active - 1];
                sctx->num_active--;
                i--;
            }
        }
    }
    
    return 0;
}

transport_t transport_socket_init(uint32_t id, const char **peers, uint32_t num_peers) {
    socket_context_t *ctx = malloc(sizeof(socket_context_t));
    ctx->my_id = id;
    ctx->num_nodes = num_peers;
    ctx->peer_addrs = malloc(sizeof(char *) * num_peers);
    for (uint32_t i = 0; i < num_peers; i++) {
        ctx->peer_addrs[i] = strdup(peers[i]);
    }
    
    ctx->peer_fds = malloc(sizeof(int) * num_peers);
    for (uint32_t i = 0; i < num_peers; i++) ctx->peer_fds[i] = -1;
    
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        ctx->client_fds[i] = -1;
        ctx->active_fds[i] = -1;
    }
    ctx->num_active = 0;
    
    struct sockaddr_in addr;
    if (parse_addr(peers[id], &addr) < 0) {
        fprintf(stderr, "Failed to parse self address: %s\n", peers[id]);
        exit(1);
    }
    
    ctx->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(ctx->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    if (bind(ctx->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }
    listen(ctx->listen_fd, 10);
    
    transport_t t = {
        .send = socket_send,
        .receive = socket_receive,
        .context = ctx
    };
    return t;
}

void transport_free(transport_t *t) {
    socket_context_t *sctx = (socket_context_t *)t->context;
    for (uint32_t i = 0; i < sctx->num_nodes; i++) {
        if (sctx->peer_fds[i] != -1) close(sctx->peer_fds[i]);
        free(sctx->peer_addrs[i]);
    }
    for (int i = 0; i < sctx->num_active; i++) close(sctx->active_fds[i]);
    close(sctx->listen_fd);
    free(sctx->peer_fds);
    free(sctx->peer_addrs);
    free(sctx);
}
