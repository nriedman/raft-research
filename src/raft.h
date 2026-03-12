#pragma once

#include "log.h"
#include "persistent-fields.h"
#include "rpc.h"
#include "raft-timer.h"
#include "transport.h"
#include <stdint.h>

#define HEARTBEAT_INTERVAL_USEC     1*1000 * 1000   // 100 ms
#define ELECTION_INTERVAL_MIN_USEC  5*1000 * 1000   // 500 ms
#define ELECTION_INTERVAL_MAX_USEC  9*1000 * 1000   // 1000 ms

#define NO_LEADER                   UINT32_MAX      // index stored when no leader known

#define MAX_OUTSTANDING_REQUESTS    128

// Heartbeat telemetry configuration
#define HEARTBEAT_WINDOW_SIZE       10              // Number of intervals to track
// φ threshold for failure detection (1.0 ≈ 90% confidence)
#define LEADER_FAILURE_PHI_THRESHOLD 1.0f

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

// Heartbeat telemetry tracking
typedef struct {
    uint64_t intervals_usec[HEARTBEAT_WINDOW_SIZE];   // Circular buffer of recent intervals
    uint32_t interval_index;                          // Current index in circular buffer
    uint32_t num_intervals;                           // Number of intervals collected (increases until window full)
    uint64_t last_heartbeat_usec;                     // Timestamp of last received heartbeat
} heartbeat_telemetry_t;

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
    int commit_index;          // index of highest log entry known to be commited (init 0)
    int last_applied;          // index of highest log entry applied to state machine (init 0)

    // "volatile" state (follower only)
    uint32_t leader_id;             // id of leader if known, NO_LEADER otherwise

    // Heartbeat telemetry (follower only)
    heartbeat_telemetry_t heartbeat_telemetry;

    // "volatile" state (candidate only)
    uint32_t votes_received;        // for elections: when > num_nodes / 2, candidate wins

    // "volatile" state (only leaders)
    uint32_t *next_index;           // for each server, index of next log entry to send to that server
                                    // (init leader last log index + 1)
    int *match_index;               // for each server, index of highest log entry known to be replicated
                                    // (init -1)

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

// Heartbeat telemetry functions
void heartbeat_telemetry_init(heartbeat_telemetry_t *telemetry);
void heartbeat_telemetry_record_interval(heartbeat_telemetry_t *telemetry, uint64_t current_time_usec);
int heartbeat_telemetry_check_leader_failure(heartbeat_telemetry_t *telemetry, uint64_t current_interval_usec);
