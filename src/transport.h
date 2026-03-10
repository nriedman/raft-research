#pragma once

#include <stdint.h>

#define MAX_PAYLOAD 1024

typedef struct {
    uint32_t dst;
    uint32_t src;
    uint32_t payload_n;                 // payload size in 4-byte words
    uint8_t code;                       // see rpc.h for possible codes
} pkt_header_t;

typedef struct {
    pkt_header_t header;
    uint8_t payload[MAX_PAYLOAD];       // actual payload size in header
} pkt_t;

// returns 0 on success, -1 on error
typedef int (*tx_fn_t)(const pkt_t*, void *ctx);

// returns 0 on timeout, 1 on success, -1 on error
typedef int (*rx_fn_t)(const pkt_t*, const uint32_t timeout_ms, void *ctx);

typedef struct {
    tx_fn_t send;
    rx_fn_t receive;
    void *context;
} transport_t;

transport_t transport_init(void);
void transport_free(transport_t *t);
