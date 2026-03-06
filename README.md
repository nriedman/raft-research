# Raft Consensus Algorithm in C

A basic, practical implementation of the Raft consensus algorithm in C. This project demonstrates leader election and log replication across multiple nodes using a local pipe-based orchestrator.

## Overview

The project consists of two main components:
- **`node`**: An individual Raft node that handles its own state machine, elections, and log entries.
- **`raft`**: An orchestrator that manages multiple nodes, providing a simulated network layer using UNIX pipes.

## Features

- **Leader Election**: Nodes transition between Follower, Candidate, and Leader states.
- **Log Replication**: Leaders replicate log entries to followers.
- **Simulated Network**: The orchestrator handles message passing between nodes.
- **Randomized Election Timeouts**: Uses randomized intervals (150ms - 300ms) to reduce election collisions.

## Getting Started

### Prerequisites

- GCC (or any C compiler)
- Make

### Building the Project

To compile the `node` and `raft` orchestrator, run:

```bash
make
```

### Running the Demo

To run a Raft cluster with a specific number of nodes (e.g., 5 nodes):

```bash
./raft 5
```

The orchestrator will fork 5 child processes, each running a Raft node, and will begin forwarding messages between them. You can observe the logs in `stderr` to see nodes starting up, timing out, and electing a leader.

## Project Structure

- `src/raft.c`: Core Raft logic (state transitions, RPC handlers).
- `src/unix/orchestrator.c`: Process management and message routing.
- `src/unix/node.c`: Entry point for individual Raft nodes.
- `src/log.c`: Log management and persistence (in-memory).
- `src/rpc.c`: RPC packet serialization and handling.

## Cleanup

To remove compiled objects and executables:

```bash
make clean
```
