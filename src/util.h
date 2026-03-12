#pragma once

#include <stdint.h>

// Helper functions based on:
// https://stackoverflow.com/questions/23019499/converting-uint8-t4-to-uint32-t-and-back-again-in-c

// Write 32-bits (bigendian). <p> not advanced.
static inline void write_u32_be(uint8_t *p, uint32_t val) {
    p[0] = (uint8_t)(val >> 24);
    p[1] = (uint8_t)(val >> 16);
    p[2] = (uint8_t)(val >> 8);
    p[3] = (uint8_t)(val >> 0);
}

// Read 32-bits (bigendian). <p> not advanced.
static inline uint32_t read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           ((uint32_t)p[3] << 0);
}

uint64_t get_usec();
uint64_t random_timeout_usec(uint32_t min, uint32_t max);
