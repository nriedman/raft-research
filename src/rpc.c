#include "util.h"
#include "rpc.h"
#include "transport.h"

// MARK: Append Entries

/*
    Offset | Field           | Size
    -------|-----------------|------
    0      | term            | 4 bytes
    4      | leader_id       | 4 bytes
    8      | prev_log_idx    | 4 bytes
    12     | prev_log_term   | 4 bytes
    16     | leader_commit   | 4 bytes
    20     | n_entries       | 4 bytes
    24     | entries[0].cmd  | 4 bytes
    28     | entries[0].term | 4 bytes
    32     | entries[1].cmd  | 4 bytes
    36     | entries[1].term | 4 bytes
    ...    | ...             | ...
*/

int rpc_pack_append_entries_req(pkt_t *pkt, uint32_t dst, uint32_t src, const append_entries_req_t *req) {
    pkt->header.dst = dst;
    pkt->header.src = src;
    pkt->header.code = RPC_CALL_APPEND_ENT;

    uint8_t *p = pkt->payload;
    write_u32_be(p, req->term); p += 4;
    write_u32_be(p, req->leader_id); p += 4;
    write_u32_be(p, req->prev_log_idx); p += 4;
    write_u32_be(p, req->prev_log_term); p += 4;
    write_u32_be(p, req->leader_commit); p += 4;
    write_u32_be(p, req->n_entries); p += 4;

    if (req->n_entries > MAX_APPEND_ENTRIES_N)
        return -1;

    for (uint32_t i = 0; i < req->n_entries; i++) {
        p = log_entry_pack(p, &req->entries[i]);
    }

    pkt->header.payload_n = p - pkt->payload;

    // TODO: guard against out-of-bounds buffer write instead
    // of checking afterwards.
    if (pkt->header.payload_n > MAX_PAYLOAD)
        return -1;

    return 0;
}

int rpc_unpack_append_entries_req(const pkt_t *pkt, append_entries_req_t *req) {
    const uint8_t *p = pkt->payload;

    if (pkt->header.payload_n < 24)
        return -1;

    req->term = read_u32_be(p); p += 4;
    req->leader_id = read_u32_be(p); p += 4;
    req->prev_log_idx = read_u32_be(p); p += 4;
    req->prev_log_term = read_u32_be(p); p += 4;
    req->leader_commit = read_u32_be(p); p += 4;
    req->n_entries = read_u32_be(p); p += 4;

    if (req->n_entries > MAX_APPEND_ENTRIES_N) {
        return -1;
    }

    uint32_t expected_size = 24 + (req->n_entries * log_entry_packed_size());
    if (pkt->header.payload_n < expected_size)
        return -1;

    for (uint32_t i = 0; i < req->n_entries; i++) {
        p = log_entry_unpack(p, &req->entries[i]);
    }

    return 0;
}

/*
    Offset | Field           | Size
    -------|-----------------|------
    0      | term            | 4 bytes
    4      | success         | 1 byte
*/

int rpc_pack_append_entries_res(pkt_t *pkt, uint32_t dst, uint32_t src, const append_entries_res_t *res) {
    pkt->header.dst = dst;
    pkt->header.src = src;
    pkt->header.code = RPC_RESP_APPEND_ENT;

    uint8_t *p = pkt->payload;
    write_u32_be(p, res->term); p += 4;
    *p = res->success; p++;
    
    pkt->header.payload_n = p - pkt->payload;

    if (pkt->header.payload_n > MAX_PAYLOAD)
        return -1;

    return 0;
}

int rpc_unpack_append_entries_res(const pkt_t *pkt, append_entries_res_t *res) {
    const uint8_t *p = pkt->payload;

    uint32_t expected_size = 5;
    if (pkt->header.payload_n < expected_size)
        return -1;

    res->term = read_u32_be(p); p += 4;
    res->success = *p; p++;

    return 0;
}

// MARK: Request Vote

/*
    Offset | Field           | Size
    -------|-----------------|------
    0      | term            | 4 bytes
    4      | candidate_id    | 4 bytes
    8      | last_log_idx    | 4 bytes
    12     | last_log_term   | 4 bytes
*/

int rpc_pack_request_vote_req(pkt_t *pkt, uint32_t dst, uint32_t src, const request_vote_req_t *req) {
    pkt->header.dst = dst;
    pkt->header.src = src;
    pkt->header.code = RPC_CALL_REQ_VOTE;

    uint8_t *p = pkt->payload;
    write_u32_be(p, req->term); p += 4;
    write_u32_be(p, req->candidate_id); p += 4;
    write_u32_be(p, req->last_log_idx); p += 4;
    write_u32_be(p, req->last_log_term); p += 4;

    pkt->header.payload_n = p - pkt->payload;

    if (pkt->header.payload_n > MAX_PAYLOAD)
        return -1;

    return 0;
}

int rpc_unpack_request_vote_req(const pkt_t *pkt, request_vote_req_t *req) {
    const uint8_t *p = pkt->payload;

    uint32_t expected_size = 16;
    if (pkt->header.payload_n < expected_size)
        return -1;

    req->term = read_u32_be(p); p += 4;
    req->candidate_id = read_u32_be(p); p += 4;
    req->last_log_idx = read_u32_be(p); p += 4;
    req->last_log_term = read_u32_be(p); p += 4;

    return 0;
}

/*
    Offset | Field           | Size
    -------|-----------------|------
    0      | term            | 4 bytes
    4      | vote_granted    | 1 byte
*/

int rpc_pack_request_vote_res(pkt_t *pkt, uint32_t dst, uint32_t src, const request_vote_res_t *res) {
    pkt->header.dst = dst;
    pkt->header.src = src;
    pkt->header.code = RPC_RESP_REQ_VOTE;

    uint8_t *p = pkt->payload;
    write_u32_be(p, res->term); p += 4;
    *p = res->vote_granted; p++;
    
    pkt->header.payload_n = p - pkt->payload;

    if (pkt->header.payload_n > MAX_PAYLOAD)
        return -1;

    return 0;
}

int rpc_unpack_request_vote_res(const pkt_t *pkt, request_vote_res_t *res) {
    const uint8_t *p = pkt->payload;

    uint32_t expected_size = 5;
    if (pkt->header.payload_n < expected_size)
        return -1;

    res->term = read_u32_be(p); p += 4;
    res->vote_granted = *p; p++;

    return 0;
}

// MARK: Process Client Request

/*
    Offset | Field           | Size
    -------|-----------------|------
    0      | client_id       | 4 bytes
    4      | cmd_seqno       | 4 bytes
    8      | cmd             | 4 bytes
*/

int rpc_pack_proc_req(pkt_t *pkt, uint32_t dst, uint32_t src, const proc_req_t *req) {
    pkt->header.dst = dst;
    pkt->header.src = src;
    pkt->header.code = RPC_CALL_PROC;

    uint8_t *p = pkt->payload;
    write_u32_be(p, req->client_id); p += 4;
    write_u32_be(p, req->cmd_seqno); p += 4;
    write_u32_be(p, req->cmd); p += 4;
    
    pkt->header.payload_n = p - pkt->payload;

    if (pkt->header.payload_n > MAX_PAYLOAD)
        return -1;

    return 0;
}

int rpc_unpack_proc_req(const pkt_t *pkt, proc_req_t *req) {
    const uint8_t *p = pkt->payload;

    uint32_t expected_size = 12;
    if (pkt->header.payload_n < expected_size)
        return -1;

    req->client_id = read_u32_be(p); p += 4;
    req->cmd_seqno = read_u32_be(p); p += 4;
    req->cmd = read_u32_be(p); p += 4;

    return 0;
}

/*
    Offset | Field           | Size
    -------|-----------------|------
    0      | client_id       | 4 bytes
    4      | cmd_seqno       | 4 bytes
    8      | success         | 1 byte
    9      | leader_hint     | 4 bytes
*/

int rpc_pack_proc_res(pkt_t *pkt, uint32_t dst, uint32_t src, const proc_res_t *res) {
    pkt->header.dst = dst;
    pkt->header.src = src;
    pkt->header.code = RPC_RESP_PROC;

    uint8_t *p = pkt->payload;
    write_u32_be(p, res->client_id); p += 4;
    write_u32_be(p, res->cmd_seqno); p += 4;
    *p = res->success; p++;
    write_u32_be(p, res->leader_hint); p += 4;
    
    pkt->header.payload_n = p - pkt->payload;

    if (pkt->header.payload_n > MAX_PAYLOAD)
        return -1;

    return 0;
}

int rpc_unpack_proc_res(const pkt_t *pkt, proc_res_t *res) {
    const uint8_t *p = pkt->payload;

    uint32_t expected_size = 13;
    if (pkt->header.payload_n < expected_size)
        return -1;

    res->client_id = read_u32_be(p); p += 4;
    res->cmd_seqno = read_u32_be(p); p += 4;
    res->success = *p; p++;
    res->leader_hint = read_u32_be(p); p += 4;

    return 0;
}
