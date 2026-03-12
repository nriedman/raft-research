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
    
    int *peer_fds; 
    int client_fds[MAX_CONNECTIONS];
    
    int active_fds[MAX_CONNECTIONS];
    int num_active;
} socket_context_t;

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return;
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void remove_active_fd(socket_context_t *sctx, int fd) {
    for (int i = 0; i < sctx->num_active; i++) {
        if (sctx->active_fds[i] == fd) {
            sctx->active_fds[i] = sctx->active_fds[sctx->num_active - 1];
            sctx->num_active--;
            return;
        }
    }
}

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
    
    set_nonblocking(fd);
    
    fprintf(stderr, "[Node %u] Connecting to Node %u at %s\n", sctx->my_id, node_id, sctx->peer_addrs[node_id]);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            perror("connect");
            close(fd);
            return -1;
        }
    }
    
    sctx->peer_fds[node_id] = fd;
    if (sctx->num_active < MAX_CONNECTIONS) {
        sctx->active_fds[sctx->num_active++] = fd;
    }
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
    
    fd_set rfds, wfds;
    struct timeval tv;
    int max_fd;
    
    // We loop here because we might accept new connections while waiting to send
    while (1) {
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        FD_SET(sctx->listen_fd, &rfds);
        max_fd = (fd > sctx->listen_fd) ? fd : sctx->listen_fd;
        
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms wait
        
        int ret = select(max_fd + 1, &rfds, &wfds, NULL, &tv);
        if (ret > 0) {
            if (FD_ISSET(sctx->listen_fd, &rfds)) {
                while (1) {
                    int new_fd = accept(sctx->listen_fd, NULL, NULL);
                    if (new_fd < 0) break;
                    set_nonblocking(new_fd);
                    if (sctx->num_active < MAX_CONNECTIONS) {
                        sctx->active_fds[sctx->num_active++] = new_fd;
                    } else {
                        close(new_fd);
                    }
                }
                // Check if fd is also ready, if not, select again
                if (!FD_ISSET(fd, &wfds)) continue;
            }
            if (FD_ISSET(fd, &wfds)) break; // Ready to send
        } else if (ret == 0) {
            return -1; // Timeout
        } else {
            if (errno == EINTR) continue;
            perror("select in socket_send");
            return -1;
        }
    }
    
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
        if (error != 0) errno = error;
        perror("socket error in socket_send");
        goto err_close;
    }

    fprintf(stderr, "[Node %u] Sending pkt to %u, code %u, size %zu\n", sctx->my_id, dst, pkt->header.code, sizeof(*pkt));
    
    size_t total_sent = 0;
    while (total_sent < sizeof(*pkt)) {
        ssize_t n = send(fd, (const char *)pkt + total_sent, sizeof(*pkt) - total_sent, 0);
        if (n > 0) {
            total_sent += n;
        } else if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Wait for writability again if we hit EAGAIN mid-packet
                fd_set w;
                FD_ZERO(&w);
                FD_SET(fd, &w);
                struct timeval t = {0, 10000};
                if (select(fd + 1, NULL, &w, NULL, &t) <= 0) {
                    // If we can't write for 10ms, just fail this packet
                    goto err_close;
                }
                continue;
            }
            perror("send");
            goto err_close;
        } else {
            goto err_close;
        }
    }
    
    return 0;

err_close:
    close(fd);
    remove_active_fd(sctx, fd);
    if (dst < sctx->num_nodes) sctx->peer_fds[dst] = -1;
    else sctx->client_fds[dst % MAX_CONNECTIONS] = -1;
    return -1;
}

static int socket_receive(pkt_t *pkt, const uint32_t timeout_ms, void *ctx) {
    socket_context_t *sctx = (socket_context_t *)ctx;
    
    uint64_t start_time = get_usec();
    uint64_t deadline = start_time + (uint64_t)timeout_ms * 1000;

    while (1) {
        uint64_t now = get_usec();
        if (now >= deadline) return 0;
        uint32_t remaining_ms = (uint32_t)((deadline - now) / 1000);

        fd_set read_fds;
        int max_fd = sctx->listen_fd;
        FD_ZERO(&read_fds);
        FD_SET(sctx->listen_fd, &read_fds);
        
        for (int i = 0; i < sctx->num_active; i++) {
            FD_SET(sctx->active_fds[i], &read_fds);
            if (sctx->active_fds[i] > max_fd) max_fd = sctx->active_fds[i];
        }
        
        struct timeval timeout;
        timeout.tv_sec = remaining_ms / 1000;
        timeout.tv_usec = (remaining_ms % 1000) * 1000;
        
        int ret = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ret == 0) return 0;
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("select in socket_receive");
            return -1;
        }
        
        if (FD_ISSET(sctx->listen_fd, &read_fds)) {
            while (1) {
                int new_fd = accept(sctx->listen_fd, NULL, NULL);
                if (new_fd < 0) break;
                set_nonblocking(new_fd);
                if (sctx->num_active < MAX_CONNECTIONS) {
                    sctx->active_fds[sctx->num_active++] = new_fd;
                } else {
                    close(new_fd);
                }
            }
            // Fall through to check data on active_fds, including those just accepted
            // but wait, read_fds was captured BEFORE accept. So we need to re-select
            // or just loop back. Let's loop back to be safe.
            continue;
        }
        
        for (int i = 0; i < sctx->num_active; i++) {
            int fd = sctx->active_fds[i];
            if (FD_ISSET(fd, &read_fds)) {
                size_t total_read = 0;
                while (total_read < sizeof(*pkt)) {
                    ssize_t n = read(fd, (char *)pkt + total_read, sizeof(*pkt) - total_read);
                    if (n > 0) {
                        total_read += n;
                    } else if (n < 0) {
                        if (errno == EINTR) continue;
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            if (total_read > 0) {
                                // Busy wait briefly for the rest of the packet
                                usleep(1000);
                                if (get_usec() > deadline) goto err_recv;
                                continue;
                            }
                            break; 
                        }
                        goto err_recv;
                    } else {
                        goto err_recv;
                    }
                }
                
                if (total_read == sizeof(*pkt)) {
                    fprintf(stderr, "[Node %u] Received pkt from %u, code %u, size %zu\n", 
                            sctx->my_id, pkt->header.src, pkt->header.code, total_read);
                    if (pkt->header.src >= sctx->num_nodes) {
                        int old_fd = sctx->client_fds[pkt->header.src % MAX_CONNECTIONS];
                        if (old_fd != -1 && old_fd != fd) {
                            close(old_fd);
                            remove_active_fd(sctx, old_fd);
                        }
                        sctx->client_fds[pkt->header.src % MAX_CONNECTIONS] = fd;
                    } else {
                        int old_fd = sctx->peer_fds[pkt->header.src];
                        if (old_fd != -1 && old_fd != fd) {
                            close(old_fd);
                            remove_active_fd(sctx, old_fd);
                        }
                        sctx->peer_fds[pkt->header.src] = fd;
                    }
                    return 1;
                }
                
                continue; 

            err_recv:
                close(fd);
                remove_active_fd(sctx, fd);
                for (uint32_t j = 0; j < sctx->num_nodes; j++) {
                    if (sctx->peer_fds[j] == fd) sctx->peer_fds[j] = -1;
                }
                for (int j = 0; j < MAX_CONNECTIONS; j++) {
                    if (sctx->client_fds[j] == fd) sctx->client_fds[j] = -1;
                }
                i--;
            }
        }
    }
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
    if (ctx->listen_fd < 0) { perror("socket"); exit(1); }
    int opt = 1;
    setsockopt(ctx->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblocking(ctx->listen_fd);
    
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
    if (!t || !t->context) return;
    socket_context_t *sctx = (socket_context_t *)t->context;
    for (uint32_t i = 0; i < sctx->num_nodes; i++) {
        free(sctx->peer_addrs[i]);
    }
    for (int i = 0; i < sctx->num_active; i++) close(sctx->active_fds[i]);
    close(sctx->listen_fd);
    free(sctx->peer_fds);
    free(sctx->peer_addrs);
    free(sctx);
    t->context = NULL;
}
