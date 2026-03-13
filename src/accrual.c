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
    telemetry->sum_intervals = 0.0;
    telemetry->sum_squares_intervals = 0.0;
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
    telemetry->sum_intervals = 0.0;
    telemetry->sum_squares_intervals = 0.0;
    telemetry->last_heartbeat_usec = 0;
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

    // Remove old value from sums if we're overwriting
    if (telemetry->num_intervals == telemetry->window_size) {
        uint64_t old_interval = telemetry->intervals_usec[telemetry->interval_index];
        telemetry->sum_intervals -= (double)old_interval;
        telemetry->sum_squares_intervals -= (double)old_interval * old_interval;
    }

    // Update sums
    telemetry->sum_intervals += (double)interval_usec;
    telemetry->sum_squares_intervals += (double)interval_usec * interval_usec;

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
    if (telemetry->num_intervals < 1) {
        return 0;
    }

    uint64_t current_interval_usec = get_usec() - telemetry->last_heartbeat_usec;

    double mean = telemetry->sum_intervals / telemetry->num_intervals;
    if (mean <= 0.0) {
        return 0;
    }

    // Variance = E[X^2] - (E[X])^2
    double variance = (telemetry->sum_squares_intervals / telemetry->num_intervals) - (mean * mean);
    if (variance < 0) {
        variance = 0;
    }
    
    double stddev = sqrt(variance);

    // Floor stddev to avoid division by zero and extreme sensitivity.
    // 10ms (10,000 usec) is a reasonable floor for network heartbeats.
    if (stddev < 10000.0) {
        stddev = 10000.0;
    }

    // φ = -log10( P(T > t) )
    // For a normal distribution, P(T > t) = 1 - Φ(t; μ, σ) = 0.5 * erfc((t - μ) / (σ * sqrt(2)))
    double x = ((double)current_interval_usec - mean) / (stddev * 1.4142135623730951);
    double p_later = 0.5 * erfc(x);

    if (p_later <= 0) {
        // Probablity is effectively zero, phi is infinity.
        return 1;
    }

    double phi = -log10(p_later);

    //fprintf(stderr, "[Telemetry] Interval: %llu µs, Mean: %.1f µs, StdDev: %.1f µs, φ=%.3f\n",
            //current_interval_usec, mean, stddev, phi);

    if (phi > telemetry->threshold) {
        //fprintf(stderr, "[Telemetry] φ threshold %.1f exceeded, leader likely failed\n", telemetry->threshold);
        return 1;
    }

    return 0;
}