#include "raft.h"
#include <stdio.h>
#include <stdlib.h>

// MARK: Raft Utility Helpers

static uint64_t random_timeout_usec(uint32_t min, uint32_t max);
static void set_election_timer(raft_node_t *node);
static void set_heartbeat_timer(raft_node_t *node);

static void become_follower(raft_node_t *node, uint32_t term);
static void become_candidate(raft_node_t *node);
static void become_leader(raft_node_t *node);

static uint32_t raft_get_log_term(raft_node_t *node, uint32_t log_idx) {
    return node->log[log_idx].term;
}

static int log_is_up_to_date(raft_node_t *node, uint32_t req_last_log_idx, uint32_t req_last_log_term) {
    if (raft_get_log_term(node, node->log_size - 1) < req_last_log_term)
        return 1;

    if (raft_get_log_term(node, node->log_size - 1) == req_last_log_term)
        return node->log_size - 1 <= req_last_log_idx;

    return 0;   // node log has higher-term last entry than req log
}

// MARK: RPC Helpers

static void send_append_entries_request(raft_node_t *node, uint32_t dst, const append_entries_req_t *req) {
    fprintf(stderr, "[Node %d] Sending append entries request to Node %d\n", node->id, dst);
    pkt_t pkt;
    rpc_pack_append_entries_req(&pkt, dst, node->id, req);
    node->transport.send(&pkt, node->transport.context);
}

static void send_append_entries_response(raft_node_t *node, uint32_t dst, uint32_t term, uint8_t success) {
    fprintf(stderr, "[Node %d] Sending append entries response to Node %d\n", node->id, dst);
    append_entries_res_t resp = {.term = term, .success = success };
    pkt_t pkt;
    rpc_pack_append_entries_res(&pkt, dst, node->id, &resp);
    node->transport.send(&pkt, node->transport.context);
}

static void send_request_vote_request(raft_node_t *node, uint32_t dst, const request_vote_req_t *req) {
    fprintf(stderr, "[Node %d] Sending request vote request to Node %d\n", node->id, dst);
    pkt_t pkt;
    rpc_pack_request_vote_req(&pkt, dst, node->id, req);
    node->transport.send(&pkt, node->transport.context);
}

static void send_request_vote_response(raft_node_t *node, uint32_t dst, uint32_t term, uint8_t vote_granted) {
    fprintf(stderr, "[Node %d] Sending request vote response to Node %d\n", node->id, dst);
    request_vote_res_t resp = { .term = term, .vote_granted = vote_granted };
    pkt_t pkt;
    rpc_pack_request_vote_res(&pkt, dst, node->id, &resp);
    node->transport.send(&pkt, node->transport.context);
}

static void broadcast_request_vote(raft_node_t *node) {
    request_vote_req_t req = {
        .term = node->current_term,
        .candidate_id = node->id,
        .last_log_idx = node->log_size - 1,
        .last_log_term = raft_get_log_term(node, node->log_size - 1)
    };

    for (uint32_t i = 0; i < node->num_nodes; i++) {
        if (i == node->id)
            continue;
        
        send_request_vote_request(node, i, &req);
    }
}

static void send_heartbeats(raft_node_t *node) {
    if (node->role != LEADER) {
        return;
    }

    fprintf(stderr, "[Node %d] Sending heartbeat\n", node->id);

    for (uint32_t i = 0; i < node->num_nodes; i++) {
        if (i == node->id)
            continue;

        append_entries_req_t req = {
            .term = node->current_term,
            .leader_id = node->id,
            .prev_log_idx = node->next_index[i] - 1,
            .prev_log_term = raft_get_log_term(node, node->next_index[i] - 1),
            .leader_commit = node->commit_index,
            .n_entries = 0
        };

        send_append_entries_request(node, i, &req);
    }
}

// MARK: RPC Handlers

static void handle_append_entires_request(raft_node_t *node, append_entries_req_t req) {
    if (req.term < node->current_term) {
        fprintf(stderr, "[Node %d] Rejecting append entries: term %d < %d\n", node->id, req.term, node->current_term);
        send_append_entries_response(node, req.leader_id, node->current_term, 0);
        return;
    }

    if (req.term > node->current_term || (req.term == node->current_term && node->role == CANDIDATE)) {
        fprintf(stderr, "[Node %d] Accepting leader %d for term %d\n", node->id, req.leader_id, req.term);
        become_follower(node, req.term);
    }

    set_election_timer(node); // reset election timer

    if (req.prev_log_idx >= node->log_size || node->log[req.prev_log_idx].term != req.prev_log_term) {
        fprintf(stderr, "[Node %d] Log inconsistency at idx %d\n", node->id, req.prev_log_idx);
        send_append_entries_response(node, req.leader_id, node->current_term, 0);
        return;
    }

    uint32_t new_idx = req.prev_log_idx + 1;
    for (uint32_t i = 0; i < req.n_entries; i++) {
        uint32_t log_idx = new_idx + i;
        if (log_idx < node->log_size) {
            if (node->log[log_idx].term != req.entries[i].term) {
                fprintf(stderr, "[Node %d] Conflict at idx %d, truncating log\n", node->id, log_idx);
                node->log_size = log_idx;
                // fall through to append
            } else {
                continue; // already matches
            }
        }
        
        if (node->log_size < MAX_LOG_LEN) {
            node->log[node->log_size] = req.entries[i];
            node->log_size++;
        }
    }

    if (req.leader_commit > node->commit_index) {
        uint32_t last_new_entry_idx = req.prev_log_idx + req.n_entries;
        node->commit_index = req.leader_commit < last_new_entry_idx ? req.leader_commit : last_new_entry_idx;
    }

    send_append_entries_response(node, req.leader_id, node->current_term, 1);
}

static void handle_append_entries_response(raft_node_t *node, uint32_t src_id, append_entries_res_t resp) {
    if (resp.term > node->current_term) {
        fprintf(stderr, "[Node %d] Found higher term %d in append response, becoming follower\n", node->id, resp.term);
        become_follower(node, resp.term);
        return;
    }

    if (node->role != LEADER) {
        return;
    }

    if (resp.success) {
        // Heartbeats (n_entries=0) don't necessarily need to update these,
        // but for now let's just use it to track replication.
    } else {
        if (node->next_index[src_id] > 1) {
            node->next_index[src_id]--;
            fprintf(stderr, "[Node %d] Decrementing next_index for Node %d to %d\n", node->id, src_id, node->next_index[src_id]);
        }
    }
}

static void handle_request_vote_request(raft_node_t *node, const request_vote_req_t req) {
    fprintf(stderr, "[Node %d] Handle RequestVote from Node %d (term %d, last_idx %d, last_term %d)\n",
            node->id, req.candidate_id, req.term, req.last_log_idx, req.last_log_term);

    if (req.term < node->current_term) {
        fprintf(stderr, "[Node %d] Rejecting vote: term %d < %d\n", node->id, req.term, node->current_term);
        send_request_vote_response(node, req.candidate_id, node->current_term, 0);
        return;
    }

    // If we discover a server with higher term, convert back to Follower
    if (req.term > node->current_term) {
        fprintf(stderr, "[Node %d] Found higher term %d, becoming follower\n", node->id, req.term);
        become_follower(node, req.term);
    }

    uint8_t vote_granted = 0;

    if (node->has_voted != 0 && node->voted_for != req.candidate_id) {
        fprintf(stderr, "[Node %d] Rejecting vote: already voted for %d\n", node->id, node->voted_for);
    } else if (!log_is_up_to_date(node, req.last_log_idx, req.last_log_term)) {
        fprintf(stderr, "[Node %d] Rejecting vote: candidate log not up to date\n", node->id);
    } else {
        vote_granted = 1;
        node->has_voted = 1;
        node->voted_for = req.candidate_id;
        fprintf(stderr, "[Node %d] Granting vote to Node %d for term %d\n", node->id, req.candidate_id, node->current_term);
        set_election_timer(node);       // reset timer on granting vote
    }

    send_request_vote_response(node, req.candidate_id, node->current_term, vote_granted);
}

static void handle_request_vote_response(raft_node_t *node, uint32_t src_id, request_vote_res_t resp) {
    // only makes sense for candidates to process votes
    if (node->role != CANDIDATE) {
        return;
    }

    // if we detect a server with higher term, convert to follower
    if (resp.term > node->current_term) {
        fprintf(stderr, "[Node %d] Found higher term %d in vote response, becoming follower\n", node->id, resp.term);
        become_follower(node, resp.term);
        return;
    }

    // ignore old responses
    if (resp.term < node->current_term) {
        return;
    }

    if (resp.vote_granted) {
        node->votes_received++;

        fprintf(stderr, "[Node %u] Received vote from Node %u, total: %u/%u\n",
                node->id, src_id, node->votes_received, node->num_nodes);
            
        // check for majority
        if (node->votes_received > node->num_nodes / 2) {
            fprintf(stderr, "[Node %u] Majority reached! Becoming leader for term %d\n", node->id, node->current_term);
            become_leader(node);
        }
    } else {
        fprintf(stderr, "[Node %u] Vote denied by Node %u\n", node->id, src_id);
    }
}

static void raft_handle_rpc(raft_node_t *node, pkt_t *rpc_pkt) {
    switch (rpc_pkt->header.code) {
        case RPC_CALL_APPEND_ENT: {
            fprintf(stderr, "[Node %d] Processing append entries request from Node %d\n", node->id, rpc_pkt->header.src);
            append_entries_req_t req;
            rpc_unpack_append_entries_req(rpc_pkt, &req);
            handle_append_entires_request(node, req);
            break;
        }
        case RPC_RESP_APPEND_ENT: {
            fprintf(stderr, "[Node %d] Processing append entries response from Node %d\n", node->id, rpc_pkt->header.src);
            append_entries_res_t resp;
            rpc_unpack_append_entries_res(rpc_pkt, &resp);
            handle_append_entries_response(node, rpc_pkt->header.src, resp);
            break;
        }
        case RPC_CALL_REQ_VOTE: {
            fprintf(stderr, "[Node %d] Processing request vote request from Node %d\n", node->id, rpc_pkt->header.src);
            request_vote_req_t req;
            rpc_unpack_request_vote_req(rpc_pkt, &req);
            handle_request_vote_request(node, req);
            break;
        }
        case RPC_RESP_REQ_VOTE: {
            fprintf(stderr, "[Node %d] Processing request vote response from Node %d\n", node->id, rpc_pkt->header.src);
            request_vote_res_t resp;
            rpc_unpack_request_vote_res(rpc_pkt, &resp);
            handle_request_vote_response(node, rpc_pkt->header.src, resp);
            break;
        }
        default: {
            fprintf(stderr, "[Node %d] Unknown RPC code: %u\n", 
                    node->id, rpc_pkt->header.code);
        }
    }
}

// MARK: Election

static void start_election(raft_node_t *node) {    
    node->current_term++;       // new term!
    node->has_voted = 1;        // vote for self
    node->voted_for = node->id;
    node->votes_received = 1;

    fprintf(stderr, "[Node %d] Starting election for term %d\n", node->id, node->current_term);

    set_election_timer(node);
    broadcast_request_vote(node);
}

static void become_follower(raft_node_t *node, uint32_t term) {
    fprintf(stderr, "[Node %d] Becoming follower for term %d\n", node->id, term);
    node->current_term = term;
    node->role = FOLLOWER;
    node->has_voted = 0;
    node->voted_for = 0;
    node->votes_received = 0;
    set_election_timer(node);
}

static void become_candidate(raft_node_t *node) {
    fprintf(stderr, "[Node %d] Becoming candidate\n", node->id);
    node->role = CANDIDATE;
    node->votes_received = 0;
    start_election(node);
}

static void become_leader(raft_node_t *node) {
    fprintf(stderr, "[Node %d] Becoming leader for term %d\n", node->id, node->current_term);
    node->role = LEADER;

    // init next_idx and match_idx
    for (unsigned i = 0; i < node->num_nodes; i++) {
        node->next_index[i] = node->log_size;
        node->match_index[i] = 0;   // q: should this be one because logs are inited non-empty?
    }

    set_heartbeat_timer(node);
    send_heartbeats(node);
}

// MARK: Timeout

static uint64_t random_timeout_usec(uint32_t min, uint32_t max) {
    return min + (rand() % (max - min));
}

static void set_election_timer(raft_node_t *node) {
    node->timer.duration_usec = random_timeout_usec(
        ELECTION_INTERVAL_MIN_USEC,
        ELECTION_INTERVAL_MAX_USEC
    );
    fprintf(stderr, "[Node %d] Set election timer to %llu usec\n", node->id, node->timer.duration_usec);
    timer_reset(&node->timer);
}

static void set_heartbeat_timer(raft_node_t *node) {
    node->timer.duration_usec = HEARTBEAT_INTERVAL_USEC;
    timer_reset(&node->timer);
}

static uint32_t raft_get_timeout_ms(raft_node_t *node) {
    return time_remaining_usec(&node->timer) / 1000;
}

static void raft_handle_timeout(raft_node_t *node) {
    switch (node->role) {
        case FOLLOWER: {
            become_candidate(node);
            break;
        }
        case CANDIDATE: {
            start_election(node);
            break;
        }
        case LEADER: {
            send_heartbeats(node);
            break;
        }
    }
}

// MARK: Run Loop

void raft_run(raft_node_t *node) {
    pkt_t pkt;

    while (node->running) {
        uint32_t timeout_ms = raft_get_timeout_ms(node);
        int rc = node->transport.receive(&pkt, timeout_ms, node->transport.context);

        if (rc == 1) {
            if (pkt.header.code == RPC_SHUTDOWN) {
                fprintf(stderr, "[Node %d] Received shutdown signal\n", node->id);
                node->running = 0;
                break;
            }

            if (pkt.header.dst != node->id) {
                fprintf(stderr, "[Node %d] Rx unintended packet\n", node->id);
                continue;
            }

            raft_handle_rpc(node, &pkt);
        } else if (rc == 0 || timer_expired(&node->timer)) {
            raft_handle_timeout(node);
        } else {
            fprintf(stderr, "[Node %d] Rx failure, exiting\n", node->id);
            node->running = 0;
            break;
        }
    }

    fprintf(stderr, "[Node %d] Exiting gracefully\n", node->id);
}

// MARK: Prev font size = 12
