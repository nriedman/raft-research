#pragma once

#include "stdint.h"

typedef enum {
    PF_CURRENT_TERM = 0,        // latest term server has seen (init 0).
    PF_HAS_VOTED = 1,           // <hasVoted> is 0 when node hasn't yet voted in current term and 1 otherwise.
    PF_VOTED_FOR = 2            // <votedFor> only has a meaningful value if <hasVoted> is not 0.
} persistent_field_t;

// Synchronously writes a new value <v> to the specified field <f> in persisted memory.
//
// Returns 0 on success and -1 on error.
typedef int (*persistent_field_write_t)(int f, uint32_t v, void *ctx);

// Returns the most recent value of the specified field <f>,
// or -1 on error.
typedef int (*persistent_field_read_t)(int f, void *ctx);

// Dispatch container for getting and setting persistent fields in a Raft node.
typedef struct {
    persistent_field_read_t get;
    persistent_field_write_t set;
    void *context;
} persistent_fields_t;

persistent_fields_t persistent_fields_init(void);
void persistent_fields_free(persistent_field_t *pf);
