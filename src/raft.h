#pragma once

#include "log.h"
#include "persistent-fields.h"
#include "rpc.h"
#include "raft-timer.h"
#include "transport.h"
#include <stdint.h>

#define HEARTBEAT_INTERVAL_USEC     50  * 1000   // 50 ms
#define ELECTION_INTERVAL_MIN_USEC  150 * 1000   // 150 ms
#define ELECTION_INTERVAL_MAX_USEC  300 * 1000   // 300 ms

#define NO_LEADER                   UINT32_MAX  // index stored when no leader known

#define MAX_OUTSTANDING_REQUESTS    128

typedef enum {
    FOLLOWER,
    CANDIDATE,
    LEADER
} raft_role_t;

typedef struct {
    uint32_t client_id;
    uint32_t cmd_seqno;
    uint32_t log_idx;
    uint8_t active;
} outstanding_req_t;

typedef struct {
    uint32_t id;
    uint32_t num_nodes;
} raft_config_t;

typedef struct {
    raft_config_t config;
    raft_role_t role;
    transport_t transport;

    // "persistant" state (updated before responding to RPCs)

    log_t log;
    persistent_fields_t hard_state;

    // "volatile" state (all servers)
    uint32_t commit_index;          // index of highest log entry known to be commited (init 0)
    uint32_t last_applied;          // index of highest log entry applied to state machine (init 0)

    // "volatile" state (follower only)
    uint32_t leader_id;             // id of leader if known, NO_LEADER otherwise

    // "volatile" state (candidate only)
    uint32_t votes_received;        // for elections: when > num_nodes / 2, candidate wins

    // "volatile" state (only leaders)
    uint32_t *next_index;           // for each server, index of next log entry to send to that server
                                    // (init leader last log index + 1)
    uint32_t *match_index;          // for each server, index of highest log entry known to be replicated
                                    // (init 0)

    // Outstanding client requests (only for leaders)
    outstanding_req_t outstanding_reqs[MAX_OUTSTANDING_REQUESTS];

    // set to 0 on termination, 1 otherwise
    uint8_t running;

    // one timer: duration will depend on node role
    timer_t timer;
} raft_node_t;

raft_node_t *raft_create(raft_config_t config, transport_t transport, log_t log, persistent_fields_t pf);
void raft_destroy(raft_node_t *node);
void raft_run(raft_node_t *node);       // main event loop
