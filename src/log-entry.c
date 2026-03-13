#include "log-entry.h"
#include "util.h"
#include <stdio.h>

/*
    Offset | Field           | Size
    -------|-----------------|------
    0      | term            | 4 bytes
    4      | client_id       | 4 bytes
    8      | cmd_seqno       | 4 bytes
    12     | cmd             | 4 bytes
*/

uint8_t* log_entry_pack(uint8_t *buf, const log_entry_t *entry) {
    write_u32_be(buf, entry->term); buf += 4;
    write_u32_be(buf, entry->client_id); buf += 4;
    write_u32_be(buf, entry->cmd_seqno); buf += 4;
    write_u32_be(buf, entry->cmd); buf += 4;
    return buf;
}

const uint8_t* log_entry_unpack(const uint8_t *buf, log_entry_t *entry) {
    entry->term = read_u32_be(buf); buf += 4;
    entry->client_id = read_u32_be(buf); buf += 4;
    entry->cmd_seqno = read_u32_be(buf); buf += 4;
    entry->cmd = read_u32_be(buf); buf += 4;
    return buf;
}

uint32_t log_entry_packed_size(void) {
    return 16;
}

static int apply_log_entry_to_sm(log_entry_t *entry, void *ctx) {
    // TODO: do a thing here?
    int node_id = -1;
    if (ctx)
        node_id = *(int *)ctx;
    
    //fprintf(stderr, "[Node %d] Applying <%x> to state machine\n", node_id, entry->cmd);
    return 0;
}

int apply_log_entries_to_sm(log_entry_t *entries, int n, void *ctx) {
    for (int i = 0; i < n; i++) {
        if (apply_log_entry_to_sm(&entries[i], ctx))
            return -1;
    }
    return 0;
}
