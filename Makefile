CC = gcc
CFLAGS = -Wall -Wextra -g -Isrc

# Source files from src directory
NODE_SRCS := src/util-unix/node.c src/util-unix/raft-util.c src/transport-pipe/transport-pipe.c src/raft.c src/rpc.c src/log.c src/util-unix/util.c
NODE_OBJS := $(patsubst src/%.c,objs/%.o,$(NODE_SRCS))

RAFT_SRCS := src/transport-pipe/orchestrator.c src/rpc.c src/log.c src/util-unix/util.c
RAFT_OBJS := $(patsubst src/%.c,objs/%.o,$(RAFT_SRCS))

# Executables in project root
all: node raft

node: $(NODE_OBJS)
	$(CC) -o $@ $^

raft: $(RAFT_OBJS)
	$(CC) -o $@ $^

# Pattern rule to compile .c files from src/ to .o files in objs/
objs/%.o: src/%.c | objs objs/util-unix objs/transport-pipe
	$(CC) $(CFLAGS) -c $< -o $@

# Create objs directories
objs:
	mkdir -p objs

objs/util-unix:
	mkdir -p objs/util-unix

objs/transport-pipe:
	mkdir -p objs/transport-pipe

clean:
	rm -rf objs node raft

.PHONY: all node raft clean