CC = gcc
CFLAGS = -Wall -Wextra -g -Isrc

# Source files from src directory
NODE_SRCS := src/unix/node.c src/unix/raft-util.c src/unix/transport-pipe.c src/raft.c src/rpc.c src/log.c src/unix/util.c
NODE_OBJS := $(patsubst src/%.c,objs/%.o,$(NODE_SRCS))

RAFT_SRCS := src/unix/orchestrator.c src/rpc.c src/log.c src/unix/util.c
RAFT_OBJS := $(patsubst src/%.c,objs/%.o,$(RAFT_SRCS))

# Executables in project root
all: node raft

node: $(NODE_OBJS)
	$(CC) -o $@ $^

raft: $(RAFT_OBJS)
	$(CC) -o $@ $^

# Pattern rule to compile .c files from src/ to .o files in objs/
objs/%.o: src/%.c | objs objs/unix
	$(CC) $(CFLAGS) -c $< -o $@

# Create objs directories
objs:
	mkdir -p objs

objs/unix:
	mkdir -p objs/unix

clean:
	rm -rf objs node raft

.PHONY: all node raft clean