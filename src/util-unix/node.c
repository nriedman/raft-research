#include "../raft.h"
#include "../transport.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <id> <num_nodes>\n", argv[0]);
        return 1;
    }

    raft_config_t config = {
        .id = atoi(argv[1]),
        .num_nodes = atoi(argv[2])
    };

    transport_t transport = transport_init();
    log_t log = log_init();
    persistent_fields_t pf = persistent_fields_init();

    raft_node_t *node = raft_create(config, transport, log, pf);

    fprintf(stderr, "[Node %d] Starting...\n", node->config.id);

    // blocks until done running
    raft_run(node);

    raft_destroy(node);
    transport_free(&transport);
}
