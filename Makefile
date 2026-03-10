CC = gcc
CFLAGS = -Wall -Wextra -g -Isrc

# Source files from src directory
NODE_SRCS := src/util-unix/node.c \
             src/util-unix/raft-util.c \
             src/util-unix/util.c \
             src/transport-pipe/transport-pipe.c \
             src/raft.c \
             src/rpc.c \
             src/log-entry.c \
             src/persist-unix/log.c \
             src/persist-unix/persistent-fields.c
NODE_OBJS := $(patsubst src/%.c,objs/%.o,$(NODE_SRCS))

RAFT_SRCS := src/transport-pipe/orchestrator.c
RAFT_OBJS := $(patsubst src/%.c,objs/%.o,$(RAFT_SRCS))

# Executables in project root
all: node raft

node: $(NODE_OBJS)
	$(CC) -o $@ $^

raft: $(RAFT_OBJS)
	$(CC) -o $@ $^

# Pattern rule to compile .c files from src/ to .o files in objs/
objs/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf objs node raft

.PHONY: all node raft clean
