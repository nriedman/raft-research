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

static int compare_doubles(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da < db) ? -1 : (da > db) ? 1 : 0;
}

#define CLIENT_ID 999
#define MAX_IN_FLIGHT 16384
#define TIMEOUT_USEC 5000000ULL

typedef struct {
    uint32_t seqno;
    uint64_t sent_usec;
    uint32_t target_node;
    int active;
} in_flight_req_t;

static in_flight_req_t in_flight_table[MAX_IN_FLIGHT];

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
    if (argc < 6) {
        //fprintf(stderr, "Usage: %s <interval_ms> <duration_sec> <cmd> <peer1,peer2,...> <client_addr>\n", argv[0]);
        //fprintf(stderr, "  interval_ms: milliseconds between requests\n");
        //fprintf(stderr, "  duration_sec: how long to run the benchmark (0 for indefinite)\n");
        //fprintf(stderr, "  cmd: command to send\n");
        //fprintf(stderr, "  peers: comma-separated list of peer addresses\n");
        //fprintf(stderr, "  client_addr: client's own address (e.g., 10.152.0.2:9000)\n");
        return 1;
    }

    uint32_t interval_ms = atoi(argv[1]);
    uint32_t duration_sec = atoi(argv[2]);
    uint32_t cmd = atoi(argv[3]);
    char *peers_str = strdup(argv[4]);
    char *client_addr = argv[5];

    // Parse peers
    char *peers[16];
    uint32_t num_peers = 0;
    char *token = strtok(peers_str, ",");
    while (token && num_peers < 16) {
        peers[num_peers++] = strdup(token);
        token = strtok(NULL, ",");
    }

    if (num_peers == 0) {
        //fprintf(stderr, "No peers specified\n");
        return 1;
    }

    // Initialize transport
    char *all_addrs[17];
    for (uint32_t i = 0; i < num_peers; i++) all_addrs[i] = peers[i];
    all_addrs[num_peers] = client_addr;

    transport_t t = transport_socket_init(num_peers, (const char **)all_addrs, num_peers + 1, 1000);

    // Setup signal handler for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Seed randomness for reservoir sampling
    srand(time(NULL));

    // Track statistics in-memory (for summary) but log every request to disk.
    uint32_t record_count = 0;
    uint32_t successful = 0;
    uint64_t total_latency_us = 0;

    // Reservoir sample for latency percentiles (bounded memory even for long runs)
    const uint32_t sample_size = 10000;
    double *latency_sample = malloc(sizeof(double) * sample_size);
    uint32_t sample_count = 0;

    // Open log file early so data is saved incrementally (survives crashes).
    FILE *log_file = fopen("client_benchmark.csv", "w");
    if (log_file) {
        fprintf(log_file, "SeqNo,Sent(usec),Received(usec),Latency(ms),Result,TargetNode,LeaderHint,Term,IsPrompt\n");
        fflush(log_file);
    } else {
        // fprintf(stderr, "Warning: failed to open client_benchmark.csv for writing\n");
    }

    printf("Starting benchmark client:\n");
    printf("  Interval: %u ms\n", interval_ms);
    printf("  Duration: %u seconds\n", duration_sec);
    printf("  Command: %u\n", cmd);
    printf("  Peers: %u\n", num_peers);
    printf("  Client address: %s\n", client_addr);
    printf("Starting benchmark...\n");

    uint64_t start_time = get_usec();
    uint64_t end_time = (duration_sec > 0)
        ? start_time + (duration_sec * 1000000ULL)
        : UINT64_MAX;
    uint32_t seqno = (uint32_t)time(NULL);
    uint32_t current_leader = 0; // Start with first peer

    uint64_t next_send_time = get_usec();
    uint64_t interval_us = (uint64_t)interval_ms * 1000ULL;
    uint64_t last_timeout_check = get_usec();

    // Baseline RTT tracking (avg of first 50 successful requests)
    double baseline_ms = 0.0;
    uint32_t baseline_samples = 0;

    while (running && (duration_sec == 0 || get_usec() < end_time)) {
        uint64_t now = get_usec();

        // 1. Send if it's time
        if (now >= next_send_time) {
            uint32_t idx = seqno % MAX_IN_FLIGHT;
            
            // If slot is occupied by a very old request that never timed out
            if (in_flight_table[idx].active) {
                if (now - in_flight_table[idx].sent_usec >= TIMEOUT_USEC) {
                    // Log timeout before overwriting
                    if (log_file) {
                        fprintf(log_file, "%u,%llu,%llu,%.1f,%s,%u,%d,%d,0\n",
                                in_flight_table[idx].seqno, in_flight_table[idx].sent_usec, 0ULL, 0.0, "TIMEOUT", in_flight_table[idx].target_node, -1, -1);
                        fflush(log_file);
                    }
                    record_count++;
                    in_flight_table[idx].active = 0;
                }
            }

            if (!in_flight_table[idx].active) {
                proc_req_t req = { .client_id = CLIENT_ID, .cmd_seqno = seqno, .cmd = cmd };
                pkt_t pkt;
                int sent = 0;
                rpc_pack_proc_req(&pkt, current_leader, CLIENT_ID, &req);
                if (t.send(&pkt, t.context) == 0) {
                    sent = 1;
                } else {
                    // Send failed, probably current_leader crashed.
                    // Pick a random OTHER node.
                    current_leader = (current_leader + 1 + (rand() % (num_peers - 1))) % num_peers;
                    printf("SEND FAILURE: Retrying with node %u\n", current_leader);
                    rpc_pack_proc_req(&pkt, current_leader, CLIENT_ID, &req);
                    if (t.send(&pkt, t.context) == 0) {
                        sent = 1;
                    }
                }

                if (sent) {
                    in_flight_table[idx].seqno = seqno;
                    in_flight_table[idx].sent_usec = now;
                    in_flight_table[idx].target_node = current_leader;
                    in_flight_table[idx].active = 1;
                    seqno++;
                }
            }
            
            next_send_time += interval_us;
            if (next_send_time < now) next_send_time = now + interval_us;
        }

        // 2. Receive responses
        pkt_t res_pkt;
        while (t.receive(&res_pkt, 0, t.context) > 0) {
            if (res_pkt.header.code == RPC_RESP_PROC) {
                proc_res_t res;
                if (rpc_unpack_proc_res(&res_pkt, &res) == 0) {
                    uint32_t idx = res.cmd_seqno % MAX_IN_FLIGHT;
                    if (in_flight_table[idx].active && in_flight_table[idx].seqno == res.cmd_seqno) {
                        uint64_t recv_time = get_usec();
                        char *result = res.success ? "OK" : "NOT_LEADER";
                        double latency_ms = (recv_time - in_flight_table[idx].sent_usec) / 1000.0;
                        
                        // Update baseline if early in the run
                        if (res.success && baseline_samples < 50) {
                            baseline_ms = (baseline_ms * baseline_samples + latency_ms) / (baseline_samples + 1);
                            baseline_samples++;
                        }

                        int is_prompt = 0;
                        if (res.success) {
                            // If we have a baseline, consider prompt if within 50% or 50ms (whichever is larger)
                            double threshold = (baseline_ms * 1.5 > baseline_ms + 50) ? baseline_ms * 1.5 : baseline_ms + 50;
                            if (baseline_samples < 10 || latency_ms <= threshold) {
                                is_prompt = 1;
                            }
                        }

                        if (log_file) {
                            fprintf(log_file, "%u,%llu,%llu,%.1f,%s,%u,%u,%u,%d\n",
                                    res.cmd_seqno, in_flight_table[idx].sent_usec, recv_time, latency_ms, result, in_flight_table[idx].target_node, res.leader_hint, res.term, is_prompt);
                            fflush(log_file);
                        }
                        
                        record_count++;
                        if (res.success) {
                            successful++;
                            total_latency_us += (recv_time - in_flight_table[idx].sent_usec);
                            
                            // Reservoir sampling
                            if (sample_count < sample_size) {
                                latency_sample[sample_count++] = (recv_time - in_flight_table[idx].sent_usec) / 1000.0;
                            } else {
                                uint32_t r = (uint32_t)(rand() % record_count);
                                if (r < sample_size) {
                                    latency_sample[r] = (recv_time - in_flight_table[idx].sent_usec) / 1000.0;
                                }
                            }
                            printf("SUCCESS: Request %u completed in %.1f ms%s\n", 
                                   res.cmd_seqno, latency_ms, is_prompt ? "" : " (QUEUED)");
                        } else {
                            printf("REDIRECT: Request %u redirected to leader %u\n", res.cmd_seqno, res.leader_hint);
                            if (res.leader_hint < num_peers) {
                                current_leader = res.leader_hint;
                            }
                        }
                        in_flight_table[idx].active = 0;
                    }
                }
            }
        }

        // 3. Periodic timeout check
        if (now - last_timeout_check >= 100000) {
            for (int i = 0; i < MAX_IN_FLIGHT; i++) {
                if (in_flight_table[i].active && (now - in_flight_table[i].sent_usec >= TIMEOUT_USEC)) {
                    if (log_file) {
                        fprintf(log_file, "%u,%llu,%llu,%.1f,%s,%u,%d,%d,0\n",
                                in_flight_table[i].seqno, in_flight_table[i].sent_usec, 0ULL, 0.0, "TIMEOUT", in_flight_table[i].target_node, -1, -1);
                        fflush(log_file);
                    }
                    printf("TIMEOUT: Request %u timed out\n", in_flight_table[i].seqno);
                    record_count++;
                    in_flight_table[i].active = 0;
                }
            }
            last_timeout_check = now;
        }

        usleep(100);
    }

    // Print summary
    printf("\nBenchmark completed. Processing %u requests...\n", record_count);
    uint32_t failed = record_count - successful;
    double avg_latency_ms = (successful > 0) ? (double)total_latency_us / successful / 1000.0 : 0.0;

    printf("\nResults:\n");
    printf("  Total requests: %u\n", record_count);
    printf("  Successful: %u (%.1f%%)\n", successful, record_count ? (double)successful / record_count * 100.0 : 0.0);
    printf("  Failed/Timeout: %u (%.1f%%)\n", failed, record_count ? (double)failed / record_count * 100.0 : 0.0);

    if (successful > 0) {
        printf("  Average latency: %.1f ms\n", avg_latency_ms);

        if (sample_count > 0) {
            // Calculate 95th percentile from the reservoir sample
            qsort(latency_sample, sample_count, sizeof(double), compare_doubles);
            uint32_t p95_idx = (uint32_t)(sample_count * 0.95);
            if (p95_idx >= sample_count) p95_idx = sample_count - 1;
            printf("  95th percentile latency (sampled): %.1f ms\n", latency_sample[p95_idx]);
        }
    }

    // Close log file (it has been written incrementally during the run)
    if (log_file) {
        fclose(log_file);
        printf("Detailed log written to client_benchmark.csv\n");
    }

    free(latency_sample);
    free(peers_str);
    for (uint32_t i = 0; i < num_peers; i++) {
        free(peers[i]);
    }

    return 0;
}