#pragma once

#include "log.h"
#include "rpc.h"
#include "raft-timer.h"
#include "transport.h"
#include <stdint.h>

#define HEARTBEAT_INTERVAL_USEC     50000       // 50 ms
#define ELECTION_INTERVAL_MIN_USEC  150000      // 150 ms
#define ELECTION_INTERVAL_MAX_USEC  300000      // 300 ms

#define NO_LEADER                   UINT32_MAX  // index stored when no leader known

typedef enum {
    FOLLOWER,
    CANDIDATE,
    LEADER
} raft_role_t;

typedef struct {
    uint32_t id;
    uint32_t num_nodes;
    raft_role_t role;
    transport_t transport;

    // "persistant" state (updated before responding to RPCs)
    uint32_t current_term;          // latest term server has seen (init 0).
    int has_voted;                  // <hasVoted> is 0 when node hasn't yet voted in current term and 1 otherwise.
    uint32_t voted_for;             // <votedFor> only has a meaningful value if <hasVoted> is not 0.

    // follower only - id of leader if known, NO_LEADER otherwise
    uint32_t leader_id;

    // candidate only - for elections
    uint32_t votes_received;

    uint32_t log_size;              // Note: to avoid annoying edge cases, we assume a non-empty log,
    log_entry_t *log;               //       so <log_size> is init 1 and <log> has an empty entry.

    // "volatile" state (all servers)
    uint32_t commit_index;          // index of highest log entry known to be commited (init 0)
    uint32_t last_applied;          // index of highest log entry applied to state machine (init 0)

    // "volatile" state (only leaders)
    uint32_t *next_index;           // for each server, index of next log entry to send to that server
                                    // (init leader last log index + 1)
    uint32_t *match_index;          // for each server, index of highest log entry known to be replicated
                                    // (init 0)
    
    // set to 0 on termination, 1 otherwise
    uint8_t running;

    // one timer: duration will depend on node role
    timer_t timer;
} raft_node_t;

raft_node_t *raft_create(uint32_t node_id, uint32_t num_nodes, transport_t transport);
void raft_destroy(raft_node_t *node);
void raft_run(raft_node_t *node);       // main event loop
