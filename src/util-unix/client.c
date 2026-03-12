#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "rpc.h"
#include "transport-socket/transport-socket.h"
#include "util.h"

#define CLIENT_ID 999

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <cmd> <peer1,peer2,...>\n", argv[0]);
        return 1;
    }

    uint32_t cmd = atoi(argv[1]);
    char *peers_str = strdup(argv[2]);
    
    // Parse peers
    char *peers[16];
    uint32_t num_peers = 0;
    char *token = strtok(peers_str, ",");
    while (token && num_peers < 16) {
        peers[num_peers++] = token;
        token = strtok(NULL, ",");
    }

    // Initialize transport. 
    // We use a high ID for ourselves so we don't clash with nodes.
    char *client_addr = "127.0.0.1:9000";
    char *all_addrs[17];
    for (uint32_t i = 0; i < num_peers; i++) all_addrs[i] = peers[i];
    all_addrs[num_peers] = client_addr;
    
    transport_t t = transport_socket_init(num_peers, (const char **)all_addrs, num_peers + 1);

    srand(time(NULL));
    uint32_t target_node = rand() % num_peers;
    uint32_t seqno = (uint32_t)time(NULL);

    proc_req_t req = {
        .client_id = CLIENT_ID,
        .cmd_seqno = seqno,
        .cmd = cmd
    };

    int attempts = 0;
    while (attempts < 2) {
        printf("Sending command %u to node %u...\n", cmd, target_node);
        
        pkt_t pkt;
        if (rpc_pack_proc_req(&pkt, target_node, num_peers, &req) != 0) {
            fprintf(stderr, "Failed to pack request\n");
            break;
        }

        if (t.send(&pkt, t.context) != 0) {
            fprintf(stderr, "Failed to send packet to node %u\n", target_node);
            break;
        }

        // Wait for response
        pkt_t res_pkt;
        int ret = t.receive(&res_pkt, 2000, t.context);
        if (ret <= 0) {
            fprintf(stderr, "Timeout or error receiving response from node %u\n", target_node);
            break;
        }

        if (res_pkt.header.code != RPC_RESP_PROC) {
            fprintf(stderr, "Received unexpected RPC code: %u\n", res_pkt.header.code);
            break;
        }

        proc_res_t res;
        if (rpc_unpack_proc_res(&res_pkt, &res) != 0) {
            fprintf(stderr, "Failed to unpack response\n");
            break;
        }

        if (res.success) {
            printf("SUCCESS: Command %u committed by node %u\n", cmd, target_node);
            break;
        } else {
            printf("Node %u is not the leader. Leader hint: %u\n", target_node, res.leader_hint);
            if (res.leader_hint < num_peers) {
                target_node = res.leader_hint;
                attempts++;
            } else {
                fprintf(stderr, "Invalid leader hint received\n");
                break;
            }
        }
    }

    transport_free(&t);
    free(peers_str);
    return 0;
}
