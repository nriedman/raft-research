#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include "rpc.h"
#include "transport-socket/transport-socket.h"
#include "util.h"

#define CLIENT_ID 999

static volatile int running = 1;

void signal_handler(int signum) {
    (void)signum;
    running = 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <peers> <client_addr> [throttle_ms]\n", argv[0]);
        fprintf(stderr, "  peers: comma-separated list of peer addresses (e.g. 127.0.0.1:8000,127.0.0.1:8001)\n");
        fprintf(stderr, "  client_addr: client's own address (e.g. 127.0.0.1:9000)\n");
        fprintf(stderr, "  throttle_ms: (optional) minimum interval between requests in milliseconds (default: 0)\n");
        return 1;
    }

    char *peers_str = strdup(argv[1]);
    char *client_addr = argv[2];
    uint32_t throttle_ms = (argc > 3) ? (uint32_t)atoi(argv[3]) : 0;

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
        free(peers_str);
        return 1;
    }

    // Initialize transport
    // all_addrs will contain [peer0, peer1, ..., peerN-1, client_addr]
    char **all_addrs = malloc(sizeof(char *) * (num_peers + 1));
    for (uint32_t i = 0; i < num_peers; i++) all_addrs[i] = peers[i];
    all_addrs[num_peers] = client_addr;

    // transport_socket_init(my_id, addrs, total_nodes)
    // The client uses its own address as the last entry in the list
    transport_t t = transport_socket_init(num_peers, (const char **)all_addrs, num_peers + 1, 2000);

    // Setup signal handler for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Open log file early so data is saved incrementally.
    FILE *log_file = fopen("client.csv", "w");
    if (log_file) {
        fprintf(log_file, "SeqNo,Sent(usec),Received(usec),Latency(ms),Result,TargetNode,LeaderHint,Term\n");
        fflush(log_file);
    } else {
        fprintf(stderr, "Warning: failed to open client.csv for writing\n");
    }

    printf("Starting simple client (CLIENT_ID=%d)...\n", CLIENT_ID);
    if (throttle_ms > 0) {
        printf("Throttle: %u ms\n", throttle_ms);
    }
    printf("Logging to client.csv\n");

    srand(time(NULL) ^ getpid());
    uint32_t seqno = (uint32_t)time(NULL);
    uint32_t current_leader = rand() % num_peers;
    uint64_t last_send_start_usec = 0;

    while (running) {
        // Throttling: ensure we don't send too frequently.
        // We measure the interval from the start of the last successful send attempt.
        if (throttle_ms > 0 && last_send_start_usec > 0) {
            uint64_t now = get_usec();
            uint64_t elapsed = now - last_send_start_usec;
            uint64_t throttle_usec = (uint64_t)throttle_ms * 1000;
            if (elapsed < throttle_usec) {
                usleep(throttle_usec - elapsed);
            }
        }

        // Prepare request
        proc_req_t req = {
            .client_id = CLIENT_ID,
            .cmd_seqno = seqno++,
            .cmd = 72 // Simple command
        };

        pkt_t pkt;
        if (rpc_pack_proc_req(&pkt, current_leader, CLIENT_ID, &req) != 0) {
            fprintf(stderr, "Failed to pack request %u\n", req.cmd_seqno);
            continue;
        }

        // Capture EXACT send time (after throttle delay)
        uint64_t sent_usec = get_usec();
        last_send_start_usec = sent_usec;

        if (t.send(&pkt, t.context) != 0) {
            printf("SEND_FAILURE: Failed to send request %u to node %u\n", req.cmd_seqno, current_leader);
            current_leader = rand() % num_peers;
            usleep(100000); // Wait 100ms before retrying
            continue;
        }

        uint64_t received_usec = 0;
        int success = 0;

        // Wait for response with timeout (1 second to prevent stalling)
        pkt_t res_pkt;
        int ret = t.receive(&res_pkt, 1000, t.context);
        uint64_t recv_time = get_usec();

        char *result = "UNKNOWN";
        uint32_t leader_hint = UINT32_MAX;
        int term = -1;

        if (ret > 0 && res_pkt.header.code == RPC_RESP_PROC) {
            proc_res_t res;
            if (rpc_unpack_proc_res(&res_pkt, &res) == 0) {
                term = res.term;
                leader_hint = res.leader_hint;
                if (res.success) {
                    received_usec = recv_time;
                    result = "OK";
                    success = 1;
                    printf("SUCCESS: Request %u completed in %.1f ms\n",
                           req.cmd_seqno, (recv_time - sent_usec) / 1000.0);
                } else {
                    result = "NOT_LEADER";
                    printf("REDIRECT: Request %u redirected to leader %u\n",
                           req.cmd_seqno, res.leader_hint);
                    if (res.leader_hint < num_peers) {
                        current_leader = res.leader_hint;
                    } else {
                        current_leader = rand() % num_peers;
                    }
                }
            } else {
                result = "UNPACK_ERROR";
                printf("ERROR: Failed to unpack response for request %u\n", req.cmd_seqno);
            }
        } else if (ret == 0) {
            result = "TIMEOUT";
            printf("TIMEOUT: Request %u timed out (target node %u)\n", req.cmd_seqno, current_leader);
            current_leader = rand() % num_peers;
        } else {
            result = "RECV_ERROR";
            printf("ERROR: Receive error for request %u\n", req.cmd_seqno);
            current_leader = rand() % num_peers;
        }

        // Persist record to disk immediately
        if (log_file) {
            double latency_ms = 0.0;
            if (success && received_usec > 0) {
                latency_ms = (received_usec - sent_usec) / 1000.0;
            }
            fprintf(log_file, "%u,%llu,%llu,%.1f,%s,%d,%d,%d\n",
                    req.cmd_seqno,
                    sent_usec,
                    received_usec,
                    latency_ms,
                    result,
                    pkt.header.dst,
                    leader_hint,
                    term
            );
            fflush(log_file);
        }

        // If we didn't succeed, wait a tiny bit before next attempt to avoid slamming nodes
        if (!success) {
            usleep(10000); // 10ms
        }
    }

    printf("\nExiting client...\n");

    if (log_file) {
        fclose(log_file);
        printf("Log written to client.csv\n");
    }

    // Cleanup
    transport_free(&t);
    free(all_addrs);
    free(peers_str);
    for (uint32_t i = 0; i < num_peers; i++) {
        free(peers[i]);
    }

    return 0;
}
