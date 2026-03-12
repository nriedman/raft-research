#include "../raft.h"
#include "../transport-socket/transport-socket.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    uint32_t id = 0;
    char *peers_str = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            id = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--peers") == 0 && i + 1 < argc) {
            peers_str = argv[++i];
        }
    }
    
    if (peers_str == NULL) {
        fprintf(stderr, "Usage: %s --id <id> --peers <p1,p2,...>\n", argv[0]);
        return 1;
    }
    
    // Parse peers_str (comma-separated)
    char *peers[128];
    uint32_t num_peers = 0;
    char *peers_copy = strdup(peers_str);
    char *token = strtok(peers_copy, ",");
    while (token != NULL && num_peers < 128) {
        peers[num_peers++] = token;
        token = strtok(NULL, ",");
    }
    
    if (id >= num_peers) {
        fprintf(stderr, "Error: id %u is out of range for %u peers\n", id, num_peers);
        free(peers_copy);
        return 1;
    }

    transport_t transport = transport_socket_init(id, (const char **)peers, num_peers);
    free(peers_copy); 

    log_t log = log_init(id);
    persistent_fields_t pf = persistent_fields_init(id);

    raft_config_t config = {
        .id = id,
        .num_nodes = num_peers
    };

    raft_node_t *node = raft_create(config, transport, log, pf);

    fprintf(stderr, "[Node %d] Starting with %d peers...\n", node->config.id, num_peers);

    // blocks until done running
    raft_run(node);

    raft_destroy(node);
    transport_free(&transport);
    return 0;
}
