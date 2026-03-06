#include "log.h"
#include "util.h"

uint8_t* log_entry_pack(uint8_t *buf, const log_entry_t *entry) {
    write_u32_be(buf, entry->cmd); buf += 4;
    write_u32_be(buf, entry->term); buf += 4;
    return buf;
}

const uint8_t* log_entry_unpack(const uint8_t *buf, log_entry_t *entry) {
    entry->cmd = read_u32_be(buf); buf += 4;
    entry->term = read_u32_be(buf); buf += 4;
    return buf;
}

uint32_t log_entry_packed_size(void) {
    return 8;
}
