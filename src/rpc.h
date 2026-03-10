#pragma once

#include "transport.h"
#include "log-entry.h"

#define MAX_APPEND_ENTRIES_N 8

// rpc codes we know how to intepret
enum {
    RPC_CALL_APPEND_ENT,        // append new entries to log
    RPC_RESP_APPEND_ENT,
    RPC_CALL_REQ_VOTE,          // initiate and execute election
    RPC_RESP_REQ_VOTE,
    RPC_CALL_PROC,              // process client request (public)
    RPC_RESP_PROC,
    RPC_SHUTDOWN = 99           // shut down cluster
};

typedef struct {
    uint32_t term;
    uint32_t leader_id;

    uint32_t prev_log_idx;
    uint32_t prev_log_term;

    uint32_t leader_commit;

    uint32_t n_entries;
    log_entry_t entries[MAX_APPEND_ENTRIES_N];
} append_entries_req_t;

typedef struct {
    uint32_t term;
    uint8_t success;
} append_entries_res_t;

typedef struct {
    uint32_t term;
    uint32_t candidate_id;
    uint32_t last_log_idx;
    uint32_t last_log_term;
} request_vote_req_t;

typedef struct {
    uint32_t term;
    uint8_t vote_granted;
} request_vote_res_t;

typedef struct {
    uint32_t cmd;
} proc_req_t;

typedef struct {
    uint8_t success;
} proc_res_t;

// serialize/deserialize RPC requests/responses into transport packet, setting packet size in header
// returns 0 on success, 1 on failure

int rpc_pack_append_entries_req(pkt_t *pkt, uint32_t dst, uint32_t src, const append_entries_req_t *req);
int rpc_unpack_append_entries_req(const pkt_t *pkt, append_entries_req_t *req);

int rpc_pack_append_entries_res(pkt_t *pkt, uint32_t dst, uint32_t src, const append_entries_res_t *res);
int rpc_unpack_append_entries_res(const pkt_t *pkt, append_entries_res_t *res);

int rpc_pack_request_vote_req(pkt_t *pkt, uint32_t dst, uint32_t src, const request_vote_req_t *req);
int rpc_unpack_request_vote_req(const pkt_t *pkt, request_vote_req_t *req);

int rpc_pack_request_vote_res(pkt_t *pkt, uint32_t dst, uint32_t src, const request_vote_res_t *res);
int rpc_unpack_request_vote_res(const pkt_t *pkt, request_vote_res_t *res);

int rpc_pack_proc_req(pkt_t *pkt, uint32_t dst, uint32_t src, const proc_req_t *req);
int rpc_unpack_proc_req(const pkt_t *pkt, proc_req_t *req);

int rpc_pack_proc_res(pkt_t *pkt, uint32_t dst, uint32_t src, const proc_res_t *res);
int rpc_unpack_proc_res(const pkt_t *pkt, proc_res_t *res);
