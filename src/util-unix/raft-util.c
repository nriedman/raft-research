#include "../raft.h"
#include "../util.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

raft_node_t *raft_create(
    raft_config_t config, 
    transport_t transport, 
    log_t log, 
    persistent_fields_t pf
) {
    uint32_t *next_index = malloc(sizeof(*next_index) * config.num_nodes);
    int *match_index = malloc(sizeof(*match_index) * config.num_nodes);

    raft_node_t *nd = calloc(1, sizeof(raft_node_t));

    nd->config = config;
    nd->role = FOLLOWER;
    nd->transport = transport;

    nd->heartbeat_count = 0;

    heartbeat_telemetry_t t;
    heartbeat_telemetry_init(&t, config.accrual_threshold, config.accrual_window_size, config.accrual_ramp_size);
    nd->heartbeat_telemetry = t;

    nd->log = log;
    nd->hard_state = pf;

    nd->commit_index = -1;
    nd->last_applied = -1;

    nd->leader_id = NO_LEADER;
    nd->votes_received = 0;
    nd->next_index = next_index;
    nd->match_index = match_index;

    nd->running = 1;
    
    return nd;
}

void raft_destroy(raft_node_t *node) {
    if (!node)
        return;

    log_free(&node->log);
    persistent_fields_free(&node->hard_state);

    heartbeat_telemetry_free(&node->heartbeat_telemetry);
    
    if (node->next_index)
        free(node->next_index);
    
    if (node->match_index)
        free(node->match_index);

    free(node);
}
