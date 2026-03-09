#include "../raft.h"
#include <stdlib.h>

raft_node_t *raft_create(uint32_t node_id, uint32_t num_nodes, transport_t transport) {
    // Note: to avoid annoying edge cases,
    // we assume a non-empty log by starting with an empty entry
    log_entry_t *log = malloc(sizeof(log_entry_t) * MAX_LOG_LEN);
    log[0].cmd = 0;
    log[0].term = 0;

    uint32_t *next_index = malloc(sizeof(uint32_t) * num_nodes);
    uint32_t *match_index = malloc(sizeof(uint32_t) * num_nodes);

    raft_node_t *nd = calloc(1, sizeof(raft_node_t));

    nd->id = node_id;
    nd->num_nodes = num_nodes;
    nd->role = FOLLOWER;
    nd->transport = transport;
    nd->current_term = 0;
    nd->leader_id = NO_LEADER;
    nd->has_voted = 0;
    nd->voted_for = 0;
    nd->log_size = 1;   // see note above
    nd->log = log;
    nd->commit_index = 0;
    nd->last_applied = 0;
    nd->next_index = next_index;        // init on ascent to leader (see raft.h)
    nd->match_index = match_index;      // ^^
    nd->running = 1;
    
    return nd;
}

void raft_destroy(raft_node_t *node) {
    if (!node)
        return;

    if (node->log)
        free(node->log);
    
    if (node->next_index)
        free(node->next_index);
    
    if (node->match_index)
        free(node->match_index);

    free(node);
}
