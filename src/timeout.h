#pragma once

#include <stdint.h>

// Heartbeat telemetry configuration
#define HEARTBEAT_WINDOW_SIZE       10              // Number of intervals to track
// φ threshold for failure detection (1.0 ≈ 90% confidence)
#define LEADER_FAILURE_PHI_THRESHOLD 1.0f

// Heartbeat telemetry tracking
typedef struct {
    uint64_t intervals_usec[HEARTBEAT_WINDOW_SIZE];   // Circular buffer of recent intervals
    uint32_t interval_index;                          // Current index in circular buffer
    uint32_t num_intervals;                           // Number of intervals collected (increases until window full)
    uint64_t last_heartbeat_usec;                     // Timestamp of last received heartbeat
} heartbeat_telemetry_t;

// Heartbeat telemetry functions
void heartbeat_telemetry_init(heartbeat_telemetry_t *telemetry);
void heartbeat_telemetry_record_interval(heartbeat_telemetry_t *telemetry, uint64_t current_time_usec);
int heartbeat_telemetry_check_leader_failure(heartbeat_telemetry_t *telemetry, uint64_t current_interval_usec);