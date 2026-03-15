#include "../raft.h"
#include "../transport-socket/transport-socket.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
    Usage:
        ./raft-node --id <node_id> --peers <ip:port,ip:port,...>

    Arguments:
        --id <node_id>: The zero-indexed identifier for this node. It corresponds 
                        to the index of its own address in the --peers list.
        --peers <list>: A comma-separated list of "IP:PORT" pairs for all nodes 
                        in the Raft cluster.

    Example:
        To run a local cluster of 3 nodes, open three terminals and run:
        
        Terminal 0: ./raft-node --id 0 --peers 127.0.0.1:8000,127.0.0.1:8001,127.0.0.1:8002
        Terminal 1: ./raft-node --id 1 --peers 127.0.0.1:8000,127.0.0.1:8001,127.0.0.1:8002
        Terminal 2: ./raft-node --id 2 --peers 127.0.0.1:8000,127.0.0.1:8001,127.0.0.1:8002

    Persistence:
        Each node maintains its own state and log files in the current directory:
        - State: raft_<id>.state
        - Log:   raft_<id>.log and raft_<id>.log.meta
*/

static void record_start(raft_config_t config) {
    char filename[128];
    if (config.timeout_scheme == TS_TIMEOUT) {
        snprintf(filename, sizeof(filename), "node_%u_t_%u_%u.csv", 
                 config.id, config.timeout_lb_ms, config.timeout_ub_ms);
    } else {
        snprintf(filename, sizeof(filename), "node_%u_a_%.1f.csv", 
                 config.id, config.accrual_threshold);
    }
    
    FILE *f = fopen(filename, "a");
    if (f) {
        fseek(f, 0, SEEK_END);
        if (ftell(f) == 0) {
            fprintf(f, "node_id,timestamp_usec,event,term,leader_id,value\n");
        }
        int leader_id = -1;
        fprintf(f, "%u,%llu,%s,%d,%d,%d\n", config.id, get_usec(), "starting", -1, leader_id, -1);
        fclose(f);
    }
}

int main(int argc, char **argv) {
    uint32_t id = 0;
    char *peers_str = NULL;
    uint32_t ts = TS_TIMEOUT;
    uint32_t timeout_lb_ms = 1000;
    uint32_t timeout_ub_ms = 2000;
    double acc_thresh = 1.0;
    uint32_t acc_ws = 32;
    uint32_t acc_rs = 8;

    int crash_after_h = -1;
    
    srand(time(NULL) ^ getpid());

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            id = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--peers") == 0 && i + 1 < argc) {
            peers_str = argv[++i];
        } else if (strcmp(argv[i], "--a") == 0 && i + 3 < argc) {
            ts = TS_ACCRUAL;
            //fprintf(stderr, "%s", argv[i+1]);
            //fprintf(stderr, ", %f\n", atof(argv[i+1]));
            acc_thresh = (double)atof(argv[++i]);
            acc_ws = (uint32_t)atoi(argv[++i]);
            acc_rs = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--t") == 0 && i + 2 < argc) {
            ts = TS_TIMEOUT;
            timeout_lb_ms = (uint32_t)atoi(argv[++i]);
            timeout_ub_ms = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--crash_after_heartbeats") == 0 && i + 1 < argc){
            crash_after_h = (int)atoi(argv[++i]);
        } else {
            continue;
        }
    }
    
    if (peers_str == NULL) {
        //fprintf(stderr, "Usage: %s --id <id> --peers <p1,p2,...>\n", argv[0]);
        //fprintf(stderr, "Optional:\n");
        //fprintf(stderr, "  --t <lb> <ub>: (Default) Use randomized timeout from [<lb>ms, <ub>ms] (defaults to [150ms, 300ms])\n");
        //fprintf(stderr, "  --a <th> <ws> <rs>:      Use accrual detection with threshold <th>, window size <ws>, and ramp size <rs>.\n");
        return 1;
    } else if (timeout_lb_ms > timeout_ub_ms) {
        //fprintf(stderr, "Error: <lb> (%dms) must be at most <ub> (%dms)\n", timeout_lb_ms, timeout_ub_ms);
        return 1;
    }
    
    // Parse peers_str (comma-separated)
    char *peers[128];
    uint32_t num_peers = 0;
    char *peers_copy = strdup(peers_str);
    char *token = strtok(peers_copy, ",");
    while (token != NULL && num_peers < 128) {
        peers[num_peers++] = strdup(token);
        token = strtok(NULL, ",");
    }
    
    if (id >= num_peers) {
        //fprintf(stderr, "Error: id %u is out of range for %u peers\n", id, num_peers);
        for (uint32_t i = 0; i < num_peers; i++) free(peers[i]);
        free(peers_copy);
        return 1;
    }

    transport_t transport = transport_socket_init(id, (const char **)peers, num_peers, 5000);
    for (uint32_t i = 0; i < num_peers; i++) free(peers[i]);
    free(peers_copy); 


    log_t log = log_init(id);
    persistent_fields_t pf = persistent_fields_init(id);

    raft_config_t config = {
        .id = id,
        .num_nodes = num_peers,
        .timeout_scheme = ts,
        .accrual_threshold = acc_thresh,
        .accrual_window_size = acc_ws,
        .accrual_ramp_size = acc_rs,
        .timeout_lb_ms = timeout_lb_ms,
        .timeout_ub_ms = timeout_ub_ms,
        .crash_after_h = crash_after_h
    };

    // log node start
    record_start(config);

    raft_node_t *node = raft_create(config, transport, log, pf);

    //fprintf(stderr, "[Node %d] Starting with %d peers...\n", node->config.id, num_peers);

    // blocks until done running
    raft_run(node);

    raft_destroy(node);
    transport_free(&transport);
    return 0;
}
