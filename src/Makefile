CC = gcc
CFLAGS = -Wall -Wextra -g -I..

NODE_SRCS := unix/node.c unix/raft-util.c unix/transport-pipe.c raft.c rpc.c log.c unix/util.c
NODE_OBJS := $(NODE_SRCS:.c=.o)

RAFT_SRCS := unix/orchestrator.c rpc.c log.c unix/util.c
RAFT_OBJS := $(RAFT_SRCS:.c=.o)

all: node raft

node: $(NODE_OBJS)
	$(CC) -o $@ $^

raft: $(RAFT_OBJS)
	$(CC) -o $@ $^

clean:
	rm -f $(NODE_OBJS) $(RAFT_OBJS) node raft

.PHONY: all node raft clean