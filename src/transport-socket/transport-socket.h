
#pragma once

#include "../transport.h"

// Initialize socket-based transport.
// <id> is the index into <peers> for this node.
// <peers> is an array of strings in "IP:PORT" format.
// <num_peers> is the length of <peers>.
transport_t transport_socket_init(uint32_t id, const char **peers, uint32_t num_peers);
