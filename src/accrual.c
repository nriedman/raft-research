#include "accrual.h"
#include "util.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void heartbeat_telemetry_init(heartbeat_telemetry_t *telemetry, double threshold, uint32_t window_size, uint32_t ramp_size) {
    // All elements inited to 0
    telemetry->intervals_usec = calloc(window_size, sizeof(*telemetry->intervals_usec));
    telemetry->window_size = window_size;
    telemetry->threshold = threshold;
    telemetry->ramp_size = ramp_size;
    telemetry->interval_index = 0;
    telemetry->num_intervals = 0;
    telemetry->last_heartbeat_usec = 0;
}

void heartbeat_telemetry_free(heartbeat_telemetry_t *telemetry) {
    if (telemetry->intervals_usec) {
        free(telemetry->intervals_usec);
        telemetry->intervals_usec = NULL;
    }
}

void heartbeat_telemetry_reset(heartbeat_telemetry_t *telemetry) {
    for (unsigned i = 0; i < telemetry->window_size; i++) {
        telemetry->intervals_usec[i] = 0;
    }
    telemetry->interval_index = 0;
    telemetry->num_intervals = 0;
    telemetry->last_heartbeat_usec = 0;
}

// Compute mean of heartbeat intervals
static double compute_mean_interval(const heartbeat_telemetry_t *telemetry) {
    if (telemetry->num_intervals == 0) {
        return 0.0;
    }

    uint64_t sum = 0;
    for (uint32_t i = 0; i < telemetry->num_intervals; i++) {
        sum += telemetry->intervals_usec[i];
    }

    return (double)sum / (double)telemetry->num_intervals;
}

void heartbeat_telemetry_record_interval(heartbeat_telemetry_t *telemetry, uint64_t current_time_usec) {
    // Initialize on first heartbeat
    if (telemetry->last_heartbeat_usec == 0) {
        telemetry->last_heartbeat_usec = current_time_usec;
        return;
    }

    // Calculate interval since last heartbeat
    uint64_t interval_usec = current_time_usec - telemetry->last_heartbeat_usec;
    telemetry->last_heartbeat_usec = current_time_usec;

    // Store in circular buffer
    telemetry->intervals_usec[telemetry->interval_index] = interval_usec;
    telemetry->interval_index = (telemetry->interval_index + 1) % telemetry->window_size;

    // Track number of intervals collected (until window is full)
    if (telemetry->num_intervals < telemetry->window_size) {
        telemetry->num_intervals++;
    }
}

// [AI] Check if leader has likely failed based on φ accrual detector
// Returns 1 if failure likely, 0 otherwise
int heartbeat_telemetry_check_leader_failure(heartbeat_telemetry_t *telemetry) {
    uint64_t current_interval_usec = get_usec() - telemetry->last_heartbeat_usec;

    double mean = compute_mean_interval(telemetry);
    if (mean <= 0.0) {
        return 0;
    }

    // λ = 1 / mean
    double lambda = 1.0 / mean;

    // φ = -log10( P(interval >= t) )
    // P(interval >= t) = exp(-λ * t)
    // φ = -log10(exp(-λ * t)) = (λ * t) * log10(e)
    double phi = (lambda * (double)current_interval_usec) * 0.4342944819; // log10(e)

    fprintf(stderr, "[Telemetry] Interval: %llu µs, Mean: %.1f µs, φ=%.3f\n",
            current_interval_usec, mean, phi);

    if (phi > telemetry->threshold) {
        fprintf(stderr, "[Telemetry] φ threshold %.1f exceeded, leader likely failed\n", telemetry->threshold);
        return 1;
    }

    return 0;
}