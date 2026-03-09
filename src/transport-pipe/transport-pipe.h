// public interface for instantiating a pipe-based transport handle

#pragma once

#include "../transport.h"

typedef struct {
    int read_fd;
    int write_fd;
} pipe_context_t;

transport_t transport_pipe_create(int read_fd, int write_fd);
void transport_pipe_destroy(transport_t *transport);
