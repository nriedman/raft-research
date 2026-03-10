#include "../raft.h"
#include <stdlib.h>

raft_node_t *raft_create(
    raft_config_t config, 
    transport_t transport, 
    log_t log, 
    persistent_fields_t pf
) {
    uint32_t *next_index = malloc(sizeof(uint32_t) * config.num_nodes);
    uint32_t *match_index = malloc(sizeof(uint32_t) * config.num_nodes);

    raft_node_t *nd = calloc(1, sizeof(raft_node_t));

    nd->config = config;
    nd->role = FOLLOWER;
    nd->transport = transport;

    nd->log = log;
    if (nd->log.length(nd->log.context) == 0) {
        // Note: to avoid annoying edge cases,
        // we assume a non-empty log by starting with an empty entry

        // TODO: Don't do this workaround at all? It seems silly.
        log_entry_t noop = {
            .client_id = 0,
            .cmd = 0,
            .cmd_seqno = 0,
            .term = 0
        };
        // no need to worry about endianness, since we're the only ones reading the file
        nd->log.write(0, (void *)&noop, sizeof(noop), nd->log.context);
    }

    nd->hard_state = pf;
    nd->hard_state.set(PF_CURRENT_TERM, 0, nd->hard_state.context);
    nd->hard_state.set(PF_HAS_VOTED, 0, nd->hard_state.context);
    nd->hard_state.set(PF_VOTED_FOR, 0, nd->hard_state.context);

    nd->commit_index = 0;
    nd->last_applied = 0;

    nd->leader_id = NO_LEADER;
    nd->votes_received = 0;
    nd->next_index = next_index;
    nd->match_index = match_index;

    nd->running = 1;

    // TODO: When to start timer? I believe it's inited to 0 at the moment.
    
    return nd;
}

void raft_destroy(raft_node_t *node) {
    if (!node)
        return;

    log_free(&node->log);
    persistent_fields_free(&node->hard_state);
    
    if (node->next_index)
        free(node->next_index);
    
    if (node->match_index)
        free(node->match_index);

    free(node);
}
