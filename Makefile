CC = gcc
CFLAGS = -Wall -Wextra -g -Isrc

# Source files from src directory
NODE_SRCS := src/util-unix/node.c \
             src/util-unix/raft-util.c \
             src/util-unix/util.c \
             src/transport-socket/transport-socket.c \
             src/raft.c \
             src/rpc.c \
             src/log-entry.c \
             src/persist-unix/log.c \
             src/persist-unix/persistent-fields.c
NODE_OBJS := $(patsubst src/%.c,objs/%.o,$(NODE_SRCS))

CLIENT_SRCS := src/util-unix/client.c \
               src/util-unix/util.c \
               src/transport-socket/transport-socket.c \
               src/rpc.c \
               src/log-entry.c
CLIENT_OBJS := $(patsubst src/%.c,objs/%.o,$(CLIENT_SRCS))

# Executables in project root
all: raft-node raft-client

raft-node: $(NODE_OBJS)
	$(CC) -o $@ $^

raft-client: $(CLIENT_OBJS)
	$(CC) -o $@ $^

# Pattern rule to compile .c files from src/ to .o files in objs/
objs/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf objs raft-node raft-client *.log* *.state *.out *.dSYM

.PHONY: all clean
