#include "raft.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// MARK: Raft Utility Helpers

static void set_election_timer(raft_node_t *node);
static void set_heartbeat_timer(raft_node_t *node);

static void become_follower(raft_node_t *node, uint32_t term, uint32_t leader_id);
static void become_candidate(raft_node_t *node);
static void become_leader(raft_node_t *node);

static uint32_t raft_get_log_term(raft_node_t *node, int log_idx) {
    if (log_idx < 0) {
        return 0; // Term of the "entry" before the first one is 0
    }
    
    int log_len = node->log.length(node->log.context);
    if (log_idx >= log_len) {
        return 0;
    }

    log_entry_t ent;
    int bytes_read = node->log.read(log_idx, &ent, sizeof(ent), node->log.context);

    if (bytes_read != (int)sizeof(ent)) {
        return 0;
    }

    return ent.term;
}

static int log_is_up_to_date(raft_node_t *node, uint32_t req_last_log_idx, uint32_t req_last_log_term) {
    int log_len = node->log.length(node->log.context);
    uint32_t my_last_term = raft_get_log_term(node, log_len - 1);

    if (my_last_term < req_last_log_term)
        return 1;

    if (my_last_term == req_last_log_term)
        return (uint32_t)(log_len - 1) <= req_last_log_idx;

    return 0;
}

static void append_log_entry(raft_node_t *node, log_entry_t entry) {
    int log_len = node->log.length(node->log.context);
    if (log_len < 0) {
        //fprintf(stderr, "[Node %d] Failed to append entry, log returned bad length <%d>\n", node->config.id, log_len);
        return;
    }

    node->log.write(log_len, &entry, sizeof(entry), node->log.context);
}

// MARK: RPC Helpers

static void send_append_entries_request(raft_node_t *node, uint32_t dst, const append_entries_req_t *req) {
    //fprintf(stderr, "[Node %d] Sending append entries request to Node %d\n", node->config.id, dst);
    pkt_t pkt;
    rpc_pack_append_entries_req(&pkt, dst, node->config.id, req);
    node->transport.send(&pkt, node->transport.context);
}

static void send_append_entries_response(raft_node_t *node, uint32_t dst, uint32_t term, uint32_t new_match, uint8_t success) {
    //fprintf(stderr, "[Node %d] Sending append entries response to Node %d\n", node->config.id, dst);
    append_entries_res_t resp = {.term = term, .new_match_index = new_match, .success = success };
    pkt_t pkt;
    rpc_pack_append_entries_res(&pkt, dst, node->config.id, &resp);
    node->transport.send(&pkt, node->transport.context);
}

static void send_request_vote_request(raft_node_t *node, uint32_t dst, const request_vote_req_t *req) {
    //fprintf(stderr, "[Node %d] Sending request vote request to Node %d\n", node->config.id, dst);
    pkt_t pkt;
    rpc_pack_request_vote_req(&pkt, dst, node->config.id, req);
    node->transport.send(&pkt, node->transport.context);
}

static void send_request_vote_response(raft_node_t *node, uint32_t dst, uint32_t term, uint8_t vote_granted) {
    //fprintf(stderr, "[Node %d] Sending request vote response to Node %d\n", node->config.id, dst);
    request_vote_res_t resp = { .term = term, .vote_granted = vote_granted };
    pkt_t pkt;
    rpc_pack_request_vote_res(&pkt, dst, node->config.id, &resp);
    node->transport.send(&pkt, node->transport.context);
}

static void send_proc_response(raft_node_t *node, uint32_t dst, const proc_res_t *resp) {
    //fprintf(stderr, "[Node %d] Sending proc response to Node %d\n", node->config.id, dst);
    pkt_t pkt;
    rpc_pack_proc_res(&pkt, dst, node->config.id, resp);
    node->transport.send(&pkt, node->transport.context);
}

static void broadcast_request_vote(raft_node_t *node) {
    int log_len = node->log.length(node->log.context);
    if (log_len < 0) {
        //fprintf(stderr, "[Node %d] Failed to broadcast req vote, log returned bad length <%d>\n", node->config.id, log_len);
        return;
    }
    
    request_vote_req_t req = {
        .term = (uint32_t)node->hard_state.get(PF_CURRENT_TERM, node->hard_state.context),
        .candidate_id = node->config.id,
        .last_log_idx = (uint32_t)(log_len - 1),
        .last_log_term = raft_get_log_term(node, log_len - 1)
    };

    for (uint32_t i = 0; i < node->config.num_nodes; i++) {
        if (i == node->config.id)
            continue;
        
        send_request_vote_request(node, i, &req);
    }
}

static void broadcast_append_entries(raft_node_t *node) {
    if (node->role != LEADER) {
        return;
    }

    int log_len = node->log.length(node->log.context);
    uint32_t cur_term = (uint32_t)node->hard_state.get(PF_CURRENT_TERM, node->hard_state.context);

    fprintf(stderr, "[Node %d] Broadcasting AppendEntries: <log=%d>, <term=%d>, <commit=%d>, <la=%d>\n",
        node->config.id, log_len, cur_term, node->commit_index, node->last_applied);

    for (uint32_t i = 0; i < node->config.num_nodes; i++) {
        if (i == node->config.id)
            continue;

        append_entries_req_t req = {
            .term = cur_term,
            .leader_id = node->config.id,
            .leader_commit = node->commit_index,
            .prev_log_idx = node->next_index[i] - 1,
            .prev_log_term = raft_get_log_term(node, (int)node->next_index[i] - 1),
        };

        int n_to_send = log_len - (int)node->next_index[i];
        if (n_to_send > MAX_APPEND_ENTRIES_N) {
            n_to_send = MAX_APPEND_ENTRIES_N;
        }
        if (n_to_send < 0) {
            n_to_send = 0;
        }

        req.n_entries = (uint32_t)n_to_send;
        for (int j = 0; j < n_to_send; j++) {
            node->log.read((int)node->next_index[i] + j, &req.entries[j], sizeof(log_entry_t), node->log.context);
        }

        send_append_entries_request(node, i, &req);
    }
}

static void send_heartbeats(raft_node_t *node) {
    if (node->role != LEADER) {
        return;
    }

    int cur_term = node->hard_state.get(PF_CURRENT_TERM, node->hard_state.context);

    //fprintf(stderr, "[Node %d] Sending heartbeat\n", node->config.id);
    for (uint32_t i = 0; i < node->config.num_nodes; i++) {
        if (i == node->config.id)
            continue;

        append_entries_req_t req = {
            .term = cur_term,
            .leader_id = node->config.id,
            .leader_commit = node->commit_index,
            .prev_log_idx = node->next_index[i] - 1,
            .prev_log_term = raft_get_log_term(node, (int)node->next_index[i] - 1),
            .n_entries = 0
        };
        send_append_entries_request(node, i, &req);
    }
}

// MARK: RPC Handlers

static void apply_to_state_machine(raft_node_t *node) {
    while (node->last_applied < node->commit_index) {
        node->last_applied++;
        log_entry_t entry;
        int bytes_read = node->log.read((int)node->last_applied, &entry, sizeof(entry), node->log.context);
        if (bytes_read != (int)sizeof(entry)) {
            //fprintf(stderr, "[Node %d] Failed to read log entry %d for state machine\n", node->config.id, (int)node->last_applied);
            continue;
        }

        fprintf(stderr, "[Node %d] Applying entry %d (cmd %d) to state machine\n", node->config.id, (int)node->last_applied, entry.cmd);
        
        if (node->role == LEADER) {
            for (int i = 0; i < MAX_OUTSTANDING_REQUESTS; i++) {
                if (node->outstanding_reqs[i].active && 
                    node->outstanding_reqs[i].client_id == entry.client_id &&
                    node->outstanding_reqs[i].cmd_seqno == entry.cmd_seqno) {
                    
                    proc_res_t resp = {
                        .client_id = entry.client_id,
                        .cmd_seqno = entry.cmd_seqno,
                        .success = 1,
                        .leader_hint = node->config.id
                    };
                    send_proc_response(node, entry.client_id, &resp);
                    node->outstanding_reqs[i].active = 0;
                    break;
                }
            }
        }
    }
}

static void handle_append_entries_request(raft_node_t *node, append_entries_req_t req) {
    int cur_term_opt = node->hard_state.get(PF_CURRENT_TERM, node->hard_state.context);
    if (cur_term_opt < 0) {
        //fprintf(stderr, "[Node %d] Append entries request handler failed, bad term: <%d>\n", node->config.id, cur_term_opt);
        return;
    }
    uint32_t cur_term = (uint32_t)cur_term_opt;

    if (req.term < cur_term) {
        //fprintf(stderr, "[Node %d] Rejecting append entries: term %d < %d\n", node->config.id, req.term, cur_term);
        send_append_entries_response(node, req.leader_id, cur_term, 0, 0);
        return;
    }

    if (req.term > cur_term || (req.term == cur_term && node->role == CANDIDATE)) {
        //fprintf(stderr, "[Node %d] Accepting leader %d for term %d\n", node->config.id, req.leader_id, req.term);
        become_follower(node, req.term, req.leader_id);
    } else {
        node->leader_id = req.leader_id;
    }

    // Record heartbeat interval and check for leader failure (follower only)
    if (node->role == FOLLOWER) {
        uint64_t current_time_usec = get_usec();
        
        // Record the interval for telemetry
        heartbeat_telemetry_record_interval(&node->heartbeat_telemetry, current_time_usec);
        
        // compute current interval for scoring
        uint64_t current_interval = 0;
        if (node->heartbeat_telemetry.num_intervals > 0) {
            int idx = (node->heartbeat_telemetry.interval_index + HEARTBEAT_WINDOW_SIZE - 1) % HEARTBEAT_WINDOW_SIZE;
            current_interval = node->heartbeat_telemetry.intervals_usec[idx];
        }
        
        if (current_interval > 0) {
            if (heartbeat_telemetry_check_leader_failure(&node->heartbeat_telemetry, current_interval)) {
                // trigger election immediately
                fprintf(stderr, "[Node %d] Leader failure detected via φ, starting election\n", node->config.id);
                become_candidate(node);
                return; // skip rest of handler
            }
        }
    }

    set_election_timer(node); 

    int log_len = node->log.length(node->log.context);
    if (log_len < 0) {
        //fprintf(stderr, "[Node %d] Failed to handle append entries req, log returned bad length <%d>\n", node->config.id, log_len);
        return;
    }

    if ((int)req.prev_log_idx >= log_len || raft_get_log_term(node, (int)req.prev_log_idx) != req.prev_log_term) {
        //fprintf(stderr, "[Node %d] Log inconsistency at idx %d\n", node->config.id, req.prev_log_idx);
        send_append_entries_response(node, req.leader_id, cur_term, 0, 0);
        return;
    }

    uint32_t new_idx = req.prev_log_idx + 1;
    for (uint32_t i = 0; i < req.n_entries; i++) {
        uint32_t log_idx = new_idx + i;
        if (log_idx < (uint32_t)log_len) {
            if (raft_get_log_term(node, (int)log_idx) != req.entries[i].term) {
                //fprintf(stderr, "[Node %d] Conflict at idx %d, truncating log\n", node->config.id, log_idx);
                node->log.remove_last_n(log_len - (int)log_idx, node->log.context);
                log_len = (int)log_idx;
            } else {
                continue; 
            }
        }

        fprintf(stderr, "[Node %d] Writing <cmd=%d> from <cid=%d> to log at <i=%d>\n", node->config.id, req.entries[i].cmd, req.entries[i].client_id, log_idx);
        node->log.write((int)log_idx, &req.entries[i], sizeof(req.entries[i]), node->log.context);
        log_len++;
    }

    if (req.leader_commit > node->commit_index) {
        fprintf(stderr, "[Node %d] Increasing commit index from <old=%d> to <new=%d>\n", node->config.id, req.leader_commit, node->commit_index);
        int last_new_entry_idx = req.prev_log_idx + req.n_entries;
        node->commit_index = req.leader_commit < last_new_entry_idx ? req.leader_commit : last_new_entry_idx;
        apply_to_state_machine(node);
    }

    send_append_entries_response(node, req.leader_id, cur_term, req.prev_log_idx + req.n_entries, 1);
}

static void handle_append_entries_response(raft_node_t *node, uint32_t src_id, append_entries_res_t resp) {
    uint32_t cur_term = (uint32_t)node->hard_state.get(PF_CURRENT_TERM, node->hard_state.context);
    if (resp.term > cur_term) {
        //fprintf(stderr, "[Node %d] Found higher term %d in append response, becoming follower\n", node->config.id, resp.term);
        become_follower(node, resp.term, src_id);
        return;
    }

    if (node->role != LEADER || resp.term < cur_term) {
        return;
    }

    if (resp.success) {
        int log_len = node->log.length(node->log.context);
        node->match_index[src_id] = resp.new_match_index;
        node->next_index[src_id] = log_len;

        for (int n = node->commit_index + 1; n < log_len; n++) {
            if (raft_get_log_term(node, (int)n) != cur_term) continue;
            
            uint32_t count = 1;
            for (uint32_t i = 0; i < node->config.num_nodes; i++) {
                if (i != node->config.id && node->match_index[i] >= n) {
                    count++;
                }
            }
            // See last bullet point of Rules for Servers in Fig. 2
            //      - N > commitIndex (n starts at commit_index + 1)
            //      - a majority of matchIndex[i] >= N
            //      - log[N].term == currentTerm (if statement above)
            if (count > node->config.num_nodes / 2) {
                fprintf(stderr, "[Node %d] Updating commit index to <cidx=%d>\n", node->config.id, n);
                node->commit_index = n;
            }
        }
        apply_to_state_machine(node);
    } else {
        if (node->next_index[src_id] > 0) {
            node->next_index[src_id]--;
            //fprintf(stderr, "[Node %d] Decrementing next_index for Node %d to %d\n", node->config.id, src_id, node->next_index[src_id]);
        }
    }
}

static void handle_request_vote_request(raft_node_t *node, uint32_t src_id, const request_vote_req_t req) {
    //fprintf(stderr, "[Node %d] Handle RequestVote from Node %d (term %d, last_idx %d, last_term %d)\n",
            // node->config.id, (int)req.candidate_id, (int)req.term, (int)req.last_log_idx, (int)req.last_log_term);

    int cur_term_opt = node->hard_state.get(PF_CURRENT_TERM, node->hard_state.context);
    if (cur_term_opt < 0) {
        //fprintf(stderr, "[Node %d] Vote request handler failed, bad term: <%d>\n", node->config.id, cur_term_opt);
        return;
    }
    uint32_t cur_term = (uint32_t)cur_term_opt;

    if (req.term < cur_term ) {
        //fprintf(stderr, "[Node %d] Rejecting vote: term %d < %d\n", node->config.id, req.term, cur_term);
        send_request_vote_response(node, req.candidate_id, cur_term, 0);
        return;
    }

    if (req.term > cur_term) {
        //fprintf(stderr, "[Node %d] Found higher term %d, becoming follower\n", node->config.id, req.term);
        become_follower(node, req.term, src_id);
        cur_term = req.term; 
    }

    int voted_for_opt = node->hard_state.get(PF_VOTED_FOR, node->hard_state.context);
    if (voted_for_opt < 0) {
        //fprintf(stderr, "[Node %d] Vote request handler failed, bad persisted vote: <%d>\n", node->config.id, voted_for_opt);
        return;
    }
    uint32_t voted_for = (uint32_t)voted_for_opt;
    uint8_t has_voted = (voted_for_opt != PF_NO_VOTE_V);

    uint8_t vote_granted = 0;

    if (has_voted && voted_for != req.candidate_id) {
        //fprintf(stderr, "[Node %d] Rejecting vote: already voted for %d\n", node->config.id, voted_for);
    } else if (!log_is_up_to_date(node, req.last_log_idx, req.last_log_term)) {
        //fprintf(stderr, "[Node %d] Rejecting vote: candidate log not up to date\n", node->config.id);
    } else {
        vote_granted = 1;
        node->hard_state.set(PF_VOTED_FOR, req.candidate_id, node->hard_state.context);
        //fprintf(stderr, "[Node %d] Granting vote to Node %d for term %d\n", node->config.id, req.candidate_id, cur_term);
        set_election_timer(node);
    }

    send_request_vote_response(node, req.candidate_id, cur_term, vote_granted);
}

static void handle_request_vote_response(raft_node_t *node, uint32_t src_id, request_vote_res_t resp) {
    if (node->role != CANDIDATE) {
        return;
    }

    uint32_t cur_term = (uint32_t)node->hard_state.get(PF_CURRENT_TERM, node->hard_state.context);
    if (resp.term > cur_term) {
        //fprintf(stderr, "[Node %d] Found higher term %d in vote response, becoming follower\n", node->config.id, resp.term);
        become_follower(node, resp.term, src_id);
        return;
    }

    if (resp.term < cur_term) {
        return;
    }

    if (resp.vote_granted) {
        node->votes_received++;

        //fprintf(stderr, "[Node %u] Received vote from Node %u, total: %u/%u\n",
                // node->config.id, src_id, node->votes_received, node->config.num_nodes);
            
        if (node->votes_received > node->config.num_nodes / 2) {
            //fprintf(stderr, "[Node %u] Majority reached! Becoming leader for term %d\n", node->config.id, cur_term);
            become_leader(node);
        }
    } else {
        //fprintf(stderr, "[Node %u] Vote denied by Node %u\n", node->config.id, src_id);
    }
}

static void handle_proc_request(raft_node_t *node, proc_req_t req, uint32_t src_id) {
    if (node->role != LEADER) {
        proc_res_t resp = {
            .client_id = req.client_id,
            .cmd_seqno = req.cmd_seqno,
            .success = 0,
            .leader_hint = node->leader_id
        };
        send_proc_response(node, src_id, &resp);
        return;
    }

    int log_len = node->log.length(node->log.context);
    log_entry_t new_entry = {
        .client_id = req.client_id,
        .cmd_seqno = req.cmd_seqno,
        .cmd = req.cmd,
        .term = (uint32_t)node->hard_state.get(PF_CURRENT_TERM, node->hard_state.context)
    };
    append_log_entry(node, new_entry);

    int found = 0;
    for (int i = 0; i < MAX_OUTSTANDING_REQUESTS; i++) {
        if (!node->outstanding_reqs[i].active) {
            node->outstanding_reqs[i].client_id = req.client_id;
            node->outstanding_reqs[i].cmd_seqno = req.cmd_seqno;
            node->outstanding_reqs[i].log_idx = (uint32_t)log_len;
            node->outstanding_reqs[i].active = 1;
            found = 1;
            break;
        }
    }
    if (!found) {
        //fprintf(stderr, "[Node %d] Too many outstanding requests!\n", node->config.id);
    }

    broadcast_append_entries(node);
}

static void handle_proc_response(raft_node_t *node, proc_res_t resp) {
    (void)node; (void)resp;
}

// MARK: RPC Dispatch

static void raft_handle_rpc(raft_node_t *node, pkt_t *rpc_pkt) {
    switch (rpc_pkt->header.code) {
        case RPC_CALL_APPEND_ENT: {
            //fprintf(stderr, "[Node %d] Processing append entries request from Node %d\n", node->config.id, rpc_pkt->header.src);
            append_entries_req_t req;
            if (rpc_unpack_append_entries_req(rpc_pkt, &req) == 0)
                handle_append_entries_request(node, req);
            else
                fprintf(stderr, "[Node %d] Failed to unpack append entries request\n", node->config.id);
            break;
        }
        case RPC_RESP_APPEND_ENT: {
            //fprintf(stderr, "[Node %d] Processing append entries response from Node %d\n", node->config.id, rpc_pkt->header.src);
            append_entries_res_t resp;
            if (rpc_unpack_append_entries_res(rpc_pkt, &resp) == 0)
                handle_append_entries_response(node, rpc_pkt->header.src, resp);
            else
                fprintf(stderr, "[Node %d] Failed to unpack append entries response\n", node->config.id);
            break;
        }
        case RPC_CALL_REQ_VOTE: {
            //fprintf(stderr, "[Node %d] Processing request vote request from Node %d\n", node->config.id, rpc_pkt->header.src);
            request_vote_req_t req;
            if (rpc_unpack_request_vote_req(rpc_pkt, &req) == 0)
                handle_request_vote_request(node, rpc_pkt->header.src, req);
            else
                fprintf(stderr, "[Node %d] Failed to unpack request vote request\n", node->config.id);
            break;
        }
        case RPC_RESP_REQ_VOTE: {
            //fprintf(stderr, "[Node %d] Processing request vote response from Node %d\n", node->config.id, rpc_pkt->header.src);
            request_vote_res_t resp;
            if (rpc_unpack_request_vote_res(rpc_pkt, &resp) == 0)
                handle_request_vote_response(node, rpc_pkt->header.src, resp);
            else
                fprintf(stderr, "[Node %d] Failed to unpack request vote response\n", node->config.id);
            break;
        }
        case RPC_CALL_PROC: {
            //fprintf(stderr, "[Node %d] Processing proc request from Node %d\n", node->config.id, rpc_pkt->header.src);
            proc_req_t req;
            if (rpc_unpack_proc_req(rpc_pkt, &req) == 0)
                handle_proc_request(node, req, rpc_pkt->header.src);
            else
                fprintf(stderr, "[Node %d] Failed to unpack proc request\n", node->config.id);
            break;
        }
        case RPC_RESP_PROC: {
            //fprintf(stderr, "[Node %d] Processing proc response from Node %d\n", node->config.id, rpc_pkt->header.src);
            proc_res_t resp;
            if (rpc_unpack_proc_res(rpc_pkt, &resp) == 0)
                handle_proc_response(node, resp);
            else
                fprintf(stderr, "[Node %d] Failed to unpack proc response\n", node->config.id);
            break;
        }
        default: {
            // fprintf(stderr, "[Node %d] Unknown RPC code: %u\n", 
                    // node->config.id, rpc_pkt->header.code);
        }
    }
}

// MARK: Election

static void start_election(raft_node_t *node) {
    int cur_term_opt = node->hard_state.get(PF_CURRENT_TERM, node->hard_state.context);
    if (cur_term_opt < 0) {
        //fprintf(stderr, "[Node %d] Election start failed, bad term: <%d>\n", node->config.id, cur_term_opt);
        return;
    }
    uint32_t cur_term = (uint32_t)cur_term_opt;
    node->hard_state.set(PF_CURRENT_TERM, cur_term + 1, node->hard_state.context);       
    
    node->hard_state.set(PF_VOTED_FOR, node->config.id, node->hard_state.context);       
    node->votes_received = 1;

    //fprintf(stderr, "[Node %d] Starting election for term %d\n", node->config.id, (int)cur_term + 1);

    set_election_timer(node);
    broadcast_request_vote(node);
}

static void become_follower(raft_node_t *node, uint32_t term, uint32_t leader_id) {
    //fprintf(stderr, "[Node %d] Becoming follower for term %d\n", node->config.id, term);
    node->hard_state.set(PF_CURRENT_TERM, term, node->hard_state.context);
    node->role = FOLLOWER;
    node->leader_id = leader_id;
    node->hard_state.set(PF_VOTED_FOR, PF_NO_VOTE_V, node->hard_state.context);
    node->votes_received = 0;
    
    // Reset telemetry when becoming a follower
    heartbeat_telemetry_init(&node->heartbeat_telemetry);
    
    set_election_timer(node);
}

static void become_candidate(raft_node_t *node) {
    //fprintf(stderr, "[Node %d] Becoming candidate\n", node->config.id);
    node->role = CANDIDATE;
    node->leader_id = NO_LEADER;
    node->votes_received = 0;
    
    // Reset telemetry when transitioning away from follower
    heartbeat_telemetry_init(&node->heartbeat_telemetry);
    
    start_election(node);
}

static void become_leader(raft_node_t *node) {
    //fprintf(stderr, "[Node %d] Becoming leader for term %d\n", node->config.id, (uint32_t)node->hard_state.get(PF_CURRENT_TERM, node->hard_state.context));
    node->role = LEADER;
    node->leader_id = NO_LEADER;

    // Reset telemetry when transitioning away from follower
    heartbeat_telemetry_init(&node->heartbeat_telemetry);

    int log_len = node->log.length(node->log.context);
    for (unsigned i = 0; i < node->config.num_nodes; i++) {
        node->next_index[i] = (uint32_t)log_len;
        node->match_index[i] = -1;
    }

    set_heartbeat_timer(node);
    send_heartbeats(node);
}

// MARK: Timeout

static void set_election_timer(raft_node_t *node) {
    node->timer.duration_usec = random_timeout_usec(
        ELECTION_INTERVAL_MIN_USEC,
        ELECTION_INTERVAL_MAX_USEC
    );
    //fprintf(stderr, "[Node %d] Set election timer to %llu usec\n", node->config.id, node->timer.duration_usec);
    timer_reset(&node->timer);
}

static void set_heartbeat_timer(raft_node_t *node) {
    node->timer.duration_usec = HEARTBEAT_INTERVAL_USEC;
    timer_reset(&node->timer);
}

static uint32_t raft_get_timeout_ms(raft_node_t *node) {
    uint64_t rem = time_remaining_usec(&node->timer);
    if (rem == 0) return 0;
    return (uint32_t)((rem + 999) / 1000);
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
            set_heartbeat_timer(node);
            send_heartbeats(node);
            break;
        }
    }
}

// Heartbeat Telemetry

void heartbeat_telemetry_init(heartbeat_telemetry_t *telemetry) {
    for (int i = 0; i < HEARTBEAT_WINDOW_SIZE; i++) {
        telemetry->intervals_usec[i] = 0;
    }
    telemetry->interval_index = 0;
    telemetry->num_intervals = 0;
    telemetry->last_heartbeat_usec = 0;
}

// Compute mean of heartbeat intervals
static double compute_mean_interval(const heartbeat_telemetry_t *telemetry) {
    if (telemetry->num_intervals == 0) {
        return 0.0;
    }
    
    uint64_t sum = 0;
    for (uint32_t i = 0; i < telemetry->num_intervals; i++) {
        sum += telemetry->intervals_usec[i];
    }
    
    return (double)sum / (double)telemetry->num_intervals;
}

// [AI] Compute standard deviation of heartbeat intervals (no longer used)
static double compute_std_dev_interval(const heartbeat_telemetry_t *telemetry) {
    if (telemetry->num_intervals <= 1) {
        return 0.0;
    }
    
    double mean = compute_mean_interval(telemetry);
    double sum_sq_diff = 0.0;
    
    for (uint32_t i = 0; i < telemetry->num_intervals; i++) {
        double diff = (double)telemetry->intervals_usec[i] - mean;
        sum_sq_diff += diff * diff;
    }
    
    double variance = sum_sq_diff / (double)(telemetry->num_intervals - 1);
    return sqrt(variance);
}

// [AI] Gaussian CDF approximation (standard normal distribution)
// Returns cumulative probability P(Z <= z)
static double gaussian_cdf(double z) {
    // Using error function approximation (Abramowitz and Stegun)
    if (z == 0.0) return 0.5;
    
    double sign = (z > 0) ? 1.0 : -1.0;
    z = fabs(z);
    
    // Approximation constants
    double a1 =  0.254829592;
    double a2 = -0.284496736;
    double a3 =  1.421413741;
    double a4 = -1.453152027;
    double a5 =  1.061405429;
    double p  =  0.3275911;
    
    double t = 1.0 / (1.0 + p * z);
    double t2 = t * t;
    double t3 = t2 * t;
    double t4 = t3 * t;
    double t5 = t4 * t;
    
    double erf = 1.0 - (a1*t + a2*t2 + a3*t3 + a4*t4 + a5*t5) * exp(-z*z);
    double cdf = 0.5 * (1.0 + sign * erf);
    
    return cdf;
}

void heartbeat_telemetry_record_interval(heartbeat_telemetry_t *telemetry, uint64_t current_time_usec) {
    // Initialize on first heartbeat
    if (telemetry->last_heartbeat_usec == 0) {
        telemetry->last_heartbeat_usec = current_time_usec;
        return;
    }
    
    // Calculate interval since last heartbeat
    uint64_t interval_usec = current_time_usec - telemetry->last_heartbeat_usec;
    telemetry->last_heartbeat_usec = current_time_usec;
    
    // Store in circular buffer
    telemetry->intervals_usec[telemetry->interval_index] = interval_usec;
    telemetry->interval_index = (telemetry->interval_index + 1) % HEARTBEAT_WINDOW_SIZE;
    
    // Track number of intervals collected (until window is full)
    if (telemetry->num_intervals < HEARTBEAT_WINDOW_SIZE) {
        telemetry->num_intervals++;
    }
}

// [AI] Check if leader has likely failed based on φ accrual detector
// Returns 1 if failure likely, 0 otherwise
int heartbeat_telemetry_check_leader_failure(heartbeat_telemetry_t *telemetry, uint64_t current_interval_usec) {
    // Need at least 3 samples to compute a reliable mean
    if (telemetry->num_intervals < 3) {
        return 0;
    }

    double mean = compute_mean_interval(telemetry);
    if (mean <= 0.0) {
        return 0;
    }

    // λ = 1 / mean
    double lambda = 1.0 / mean;

    // φ = -log10( P(interval >= t) )
    // P(interval >= t) = exp(-λ * t)
    // φ = -log10(exp(-λ * t)) = (λ * t) * log10(e)
    double phi = (lambda * (double)current_interval_usec) * 0.4342944819; // log10(e)

    fprintf(stderr, "[Telemetry] Interval: %llu µs, Mean: %.1f µs, φ=%.3f\n",
            current_interval_usec, mean, phi);

    if (phi > LEADER_FAILURE_PHI_THRESHOLD) {
        fprintf(stderr, "[Telemetry] φ threshold %.1f exceeded, leader likely failed\n", LEADER_FAILURE_PHI_THRESHOLD);
        return 1;
    }

    return 0;
}

// MARK: Run Loop

void raft_run(raft_node_t *node) {
    pkt_t pkt;

    while (node->running) {
        uint32_t timeout_ms = raft_get_timeout_ms(node);
        int rc = node->transport.receive(&pkt, timeout_ms, node->transport.context);

        if (rc == 1) {
            if (pkt.header.code == RPC_SHUTDOWN) {
                //fprintf(stderr, "[Node %d] Received shutdown signal\n", node->config.id);
                node->running = 0;
                break;
            }

            if (pkt.header.dst != node->config.id) {
                //fprintf(stderr, "[Node %d] Rx unintended packet\n", node->config.id);
                continue;
            }

            raft_handle_rpc(node, &pkt);
        } else if (rc == 0 || timer_expired(&node->timer)) {
            raft_handle_timeout(node);
        } else {
            //fprintf(stderr, "[Node %d] Rx failure, exiting\n", node->config.id);
            node->running = 0;
            break;
        }
    }

    //fprintf(stderr, "[Node %d] Exiting gracefully\n", node->config.id);
}
