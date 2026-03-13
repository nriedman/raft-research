#pragma once

#include <stdint.h>

enum {
    TS_TIMEOUT = 50,
    TS_ACCRUAL = 51,
};

// Until there are this many heartbeats seen,
// the accrual will behave like a random timeout election
#define ACCRUAL_MIN_SAMPLES         16

// Time between follower score sampling in idle periods
#define ACCRUAL_INTERVAL_MS         1000

// Heartbeat telemetry tracking
typedef struct {
    uint64_t *intervals_usec;                         // Circular buffer of recent intervals
    uint32_t window_size;                             // size is equal to <raft_node_t.config.accrual_window_size>
    uint32_t threshold;                               // Phi accrual fault threshold
    uint32_t interval_index;                          // Current index in circular buffer
    uint32_t num_intervals;                           // Number of intervals collected (increases until window full)
    uint64_t last_heartbeat_usec;                     // Timestamp of last received heartbeat
} heartbeat_telemetry_t;

// Heartbeat telemetry functions
void heartbeat_telemetry_init(heartbeat_telemetry_t *telemetry, uint32_t threshold, uint32_t window_size);
void heartbeat_telemetry_free(heartbeat_telemetry_t *telemetry);

void heartbeat_telemetry_reset(heartbeat_telemetry_t *telemetry);
void heartbeat_telemetry_record_interval(heartbeat_telemetry_t *telemetry, uint64_t current_time_usec);
int heartbeat_telemetry_check_leader_failure(heartbeat_telemetry_t *telemetry);