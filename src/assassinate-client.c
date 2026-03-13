#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "rpc.h"
#include "transport-socket/transport-socket.h"
#include "util.h"

#define CLIENT_ID 998  // Different from regular client (999)

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <pause_seconds> <peer1,peer2,...>\n", argv[0]);
        fprintf(stderr, "  pause_seconds: how long the leader should pause (simulate crash)\n");
        fprintf(stderr, "  peers: comma-separated list of peer addresses\n");
        fprintf(stderr, "\nExample: %s 5 127.0.0.1:8000,127.0.0.1:8001,127.0.0.1:8002\n", argv[0]);
        return 1;
    }

    uint32_t pause_seconds = atoi(argv[1]);
    char *peers_str = strdup(argv[2]);

    // Parse peers
    char *peers[16];
    uint32_t num_peers = 0;
    char *token = strtok(peers_str, ",");
    while (token && num_peers < 16) {
        peers[num_peers++] = strdup(token);
        token = strtok(NULL, ",");
    }

    if (num_peers == 0) {
        fprintf(stderr, "No peers specified\n");
        return 1;
    }

    // Initialize transport
    char *client_addr = "127.0.0.1:8999";  // Different port from regular client
    char *all_addrs[17];
    for (uint32_t i = 0; i < num_peers; i++) all_addrs[i] = peers[i];
    all_addrs[num_peers] = client_addr;

    transport_t t = transport_socket_init(num_peers, (const char **)all_addrs, num_peers + 1);

    srand(time(NULL));
    uint32_t target_node = rand() % num_peers;
    uint32_t seqno = (uint32_t)time(NULL);

    // Negative command value indicates assassination request
    // The absolute value is the pause time in seconds
    int32_t assassination_cmd = -((int32_t)pause_seconds);

    proc_req_t req = {
        .client_id = CLIENT_ID,
        .cmd_seqno = seqno,
        .cmd = assassination_cmd
    };

    int attempts = 0;
    while (attempts < 3) {  // Try up to 3 times to find the leader
        if (target_node >= num_peers) {
            fprintf(stderr, "Target node %u out of range\n", target_node);
            break;
        }

        printf("🔪 Sending assassination request to Node %u (pause for %u seconds)...\n",
               target_node, pause_seconds);

        pkt_t pkt;
        if (rpc_pack_proc_req(&pkt, target_node, CLIENT_ID, &req) != 0) {
            fprintf(stderr, "Failed to pack assassination request\n");
            break;
        }

        if (t.send(&pkt, t.context) != 0) {
            fprintf(stderr, "Failed to send assassination request to node %u\n", target_node);
            break;
        }

        // Wait for response
        pkt_t res_pkt;
        int ret = t.receive(&res_pkt, 10000, t.context);  // 10 second timeout
        if (ret <= 0) {
            fprintf(stderr, "Timeout waiting for assassination response from node %u\n", target_node);
            break;
        }

        if (res_pkt.header.code != RPC_RESP_PROC) {
            fprintf(stderr, "Received unexpected RPC code: %u\n", res_pkt.header.code);
            break;
        }

        proc_res_t res;
        if (rpc_unpack_proc_res(&res_pkt, &res) != 0) {
            fprintf(stderr, "Failed to unpack assassination response\n");
            break;
        }

        if (res.success) {
            printf("✅ ASSASSINATION SUCCESSFUL: Leader %u will pause for %u seconds\n",
                   target_node, pause_seconds);
            printf("💤 The leader is now simulating a crash. Watch for election timeout and new leader election!\n");
            break;
        } else {
            printf("❌ Node %u is not the leader. Leader hint: %u\n", target_node, res.leader_hint);
            if (res.leader_hint < num_peers) {
                target_node = res.leader_hint;
                attempts++;
                printf("🎯 Retrying with leader hint...\n");
            } else {
                fprintf(stderr, "❌ Invalid leader hint received\n");
                break;
            }
        }
    }

    if (attempts >= 3) {
        fprintf(stderr, "❌ Failed to assassinate leader after %d attempts\n", attempts);
        return 1;
    }

    free(peers_str);
    for (uint32_t i = 0; i < num_peers; i++) {
        free(peers[i]);
    }

    return 0;
}