# Raft Consensus Algorithm in C

A basic, practical implementation of the Raft consensus algorithm in C. This project was built as part of a research project examining the effects of different election timeout schemes on system availability.

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

## Acknowledgments

This codebase was developed as part of the final projects for CS140e (Operating systems design and implementation) and CS244c (Advanced Networking and Distributed Systems).

The following online resources were incredibly valuable as guides for how to safely develop Raft:

- Project descriptions for [Project 1](https://web.stanford.edu/~ouster/cgi-bin/cs190-winter20/raft1.php) and [Project 2](https://web.stanford.edu/~ouster/cgi-bin/cs190-winter20/raft2.php) in Prof John Ousterhout's Winter 2020 CS190 (Software Design Studio);
- An MIT distributed systems class [writeup](https://thesquareplanet.com/blog/students-guide-to-raft/) sharing tips and tricks based on past experience.

Additionally, AI tools (Gemini CLI, Copilot) were used to develop the following componenets:

- `README.md` description to introduce project and describe usage;
- Extend debug messages for higher fidelity runtime logging;
- Compose the Makefile for ease of compilation over changing codebase structure.

Where applicable, inline comments document attributions for code that came from external sources not discussed here.
