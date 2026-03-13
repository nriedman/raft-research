#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include "rpc.h"
#include "transport-socket/transport-socket.h"
#include "util.h"

#define CLIENT_ID 999

typedef struct {
    uint32_t seqno;
    uint64_t sent_usec;
    uint64_t received_usec;
    int success;
} request_record_t;

static volatile int running = 1;

void signal_handler(int signum) {
    (void)signum;
    running = 0;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <interval_ms> <duration_sec> <cmd> <peer1,peer2,...>\n", argv[0]);
        fprintf(stderr, "  interval_ms: milliseconds between requests\n");
        fprintf(stderr, "  duration_sec: how long to run the benchmark\n");
        fprintf(stderr, "  cmd: command to send\n");
        fprintf(stderr, "  peers: comma-separated list of peer addresses\n");
        return 1;
    }

    uint32_t interval_ms = atoi(argv[1]);
    uint32_t duration_sec = atoi(argv[2]);
    uint32_t cmd = atoi(argv[3]);
    char *peers_str = strdup(argv[4]);

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
    char *client_addr = "127.0.0.1:9000";
    char *all_addrs[17];
    for (uint32_t i = 0; i < num_peers; i++) all_addrs[i] = peers[i];
    all_addrs[num_peers] = client_addr;

    transport_t t = transport_socket_init(num_peers, (const char **)all_addrs, num_peers + 1);

    // Setup signal handler for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Allocate records for tracking requests
    const uint32_t max_records = 10000;
    request_record_t *records = calloc(max_records, sizeof(request_record_t));
    uint32_t record_count = 0;

    printf("Starting benchmark client:\n");
    printf("  Interval: %u ms\n", interval_ms);
    printf("  Duration: %u seconds\n", duration_sec);
    printf("  Command: %u\n", cmd);
    printf("  Peers: %u\n", num_peers);
    printf("Starting benchmark...\n");

    uint64_t start_time = get_usec();
    uint64_t end_time = start_time + (duration_sec * 1000000ULL);
    uint32_t seqno = (uint32_t)time(NULL);
    uint32_t current_leader = 0; // Start with first peer

    while (running && get_usec() < end_time && record_count < max_records) {
        uint64_t now = get_usec();

        // Send request
        proc_req_t req = {
            .client_id = CLIENT_ID,
            .cmd_seqno = seqno++,
            .cmd = cmd
        };

        pkt_t pkt;
        if (rpc_pack_proc_req(&pkt, current_leader, CLIENT_ID, &req) != 0) {
            fprintf(stderr, "Failed to pack request %u\n", req.cmd_seqno);
            usleep(interval_ms * 1000);
            continue;
        }

        if (t.send(&pkt, t.context) != 0) {
            fprintf(stderr, "Failed to send packet %u to node %u\n", req.cmd_seqno, current_leader);
            usleep(interval_ms * 1000);
            continue;
        }

        // Record send time
        records[record_count].seqno = req.cmd_seqno;
        records[record_count].sent_usec = now;
        records[record_count].received_usec = 0;
        records[record_count].success = 0;

        // Wait for response with timeout
        pkt_t res_pkt;
        int ret = t.receive(&res_pkt, 5000, t.context); // 5 second timeout
        uint64_t recv_time = get_usec();

        if (ret > 0 && res_pkt.header.code == RPC_RESP_PROC) {
            proc_res_t res;
            if (rpc_unpack_proc_res(&res_pkt, &res) == 0) {
                if (res.success) {
                    records[record_count].received_usec = recv_time;
                    records[record_count].success = 1;
                    printf("SUCCESS: Request %u completed in %.1f ms\n",
                           req.cmd_seqno,
                           (recv_time - now) / 1000.0);
                } else {
                    printf("REDIRECT: Request %u redirected to leader %u\n",
                           req.cmd_seqno, res.leader_hint);
                    if (res.leader_hint < num_peers) {
                        current_leader = res.leader_hint;
                    }
                }
            } else {
                printf("ERROR: Failed to unpack response for request %u\n", req.cmd_seqno);
            }
        } else {
            printf("TIMEOUT: Request %u timed out\n", req.cmd_seqno);
        }

        record_count++;

        // Wait for next interval
        uint64_t next_send = now + (interval_ms * 1000ULL);
        while (get_usec() < next_send && running) {
            usleep(1000); // Sleep 1ms at a time to check for signals
        }
    }

    // Print summary
    printf("\nBenchmark completed. Processing %u requests...\n", record_count);

    uint32_t successful = 0;
    uint32_t failed = 0;
    double total_latency = 0.0;
    uint32_t latency_count = 0;

    for (uint32_t i = 0; i < record_count; i++) {
        if (records[i].success && records[i].received_usec > 0) {
            successful++;
            double latency_ms = (records[i].received_usec - records[i].sent_usec) / 1000.0;
            total_latency += latency_ms;
            latency_count++;
        } else {
            failed++;
        }
    }

    printf("\nResults:\n");
    printf("  Total requests: %u\n", record_count);
    printf("  Successful: %u (%.1f%%)\n", successful, (double)successful / record_count * 100.0);
    printf("  Failed/Timeout: %u (%.1f%%)\n", failed, (double)failed / record_count * 100.0);

    if (latency_count > 0) {
        double avg_latency = total_latency / latency_count;
        printf("  Average latency: %.1f ms\n", avg_latency);

        // Calculate 95th percentile
        double latencies[latency_count];
        for (uint32_t i = 0; i < latency_count; i++) {
            uint32_t idx = 0;
            for (uint32_t j = 0; j < record_count; j++) {
                if (records[j].success && records[j].received_usec > 0) {
                    latencies[idx++] = (records[j].received_usec - records[j].sent_usec) / 1000.0;
                    if (idx >= latency_count) break;
                }
            }
        }

        // Simple sort for percentile (not efficient for large arrays)
        for (uint32_t i = 0; i < latency_count - 1; i++) {
            for (uint32_t j = i + 1; j < latency_count; j++) {
                if (latencies[j] < latencies[i]) {
                    double temp = latencies[i];
                    latencies[i] = latencies[j];
                    latencies[j] = temp;
                }
            }
        }

        uint32_t p95_idx = (uint32_t)(latency_count * 0.95);
        if (p95_idx >= latency_count) p95_idx = latency_count - 1;
        printf("  95th percentile latency: %.1f ms\n", latencies[p95_idx]);
    }

    // Write detailed log to file
    FILE *log_file = fopen("client_benchmark.log", "w");
    if (log_file) {
        fprintf(log_file, "# SeqNo\tSent(usec)\tReceived(usec)\tLatency(ms)\tSuccess\n");
        for (uint32_t i = 0; i < record_count; i++) {
            double latency = 0.0;
            if (records[i].success && records[i].received_usec > 0) {
                latency = (records[i].received_usec - records[i].sent_usec) / 1000.0;
            }
            fprintf(log_file, "%u\t%llu\t%llu\t%.1f\t%d\n",
                    records[i].seqno,
                    records[i].sent_usec,
                    records[i].received_usec,
                    latency,
                    records[i].success);
        }
        fclose(log_file);
        printf("Detailed log written to client_benchmark.log\n");
    }

    free(records);
    free(peers_str);
    for (uint32_t i = 0; i < num_peers; i++) {
        free(peers[i]);
    }

    return 0;
}