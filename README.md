# Raft Consensus Algorithm in C

A practical implementation of the Raft consensus algorithm in C, utilizing socket-based transport for real network communication between nodes.

## Overview

The project consists of:
- **`raft-node`**: An individual Raft node that manages leader elections, log replication, and state persistence. Nodes communicate over TCP sockets.
- **`raft-client`**: A simple client to send commands to the Raft cluster (work in progress).

## Features

- **Socket Transport**: Real TCP communication between nodes using a custom packet format.
- **Leader Election**: Full implementation of the Raft election safety properties.
- **Persistence**: Nodes persist their current term, voted-for status, and log entries to disk (`raft_<id>.state`, `raft_<id>.log`).
- **Log Replication**: Leaders replicate log entries to a majority of followers before committing.

## Getting Started

### Prerequisites

- GCC (or any C compiler)
- Make

### Building the Project

To compile the `raft-node` and `raft-client`, run:

```bash
make
```

### Running the Cluster

To run a cluster of 3 nodes on your local machine, open three separate terminals and execute the following commands:

**Terminal 0:**
```bash
./raft-node --id 0 --peers 127.0.0.1:8000,127.0.0.1:8001,127.0.0.1:8002
```

**Terminal 1:**
```bash
./raft-node --id 1 --peers 127.0.0.1:8000,127.0.0.1:8001,127.0.0.1:8002
```

**Terminal 2:**
```bash
./raft-node --id 2 --peers 127.0.0.1:8000,127.0.0.1:8001,127.0.0.1:8002
```

Each node will start, load any existing state from its local files, and begin the election process.

### Using the Client

**Note:** The client is currently a work in progress and may not function as expected in all scenarios.

To send a command (e.g., the value `42`) to the cluster:

```bash
./raft-client 42 127.0.0.1:8000,127.0.0.1:8001,127.0.0.1:8002
```

The client will attempt to contact a node, follow leader hints if necessary, and report if the command was successfully committed.

## Project Structure

- `src/raft.c`: Core Raft state machine and logic.
- `src/transport-socket/`: TCP socket implementation of the transport layer.
- `src/persist-unix/`: Disk-based persistence for logs and metadata.
- `src/util-unix/node.c`: Entry point for the Raft node executable.
- `src/util-unix/client.c`: Entry point for the Raft client executable.
- `src/rpc.c`: RPC packet serialization.

## Cleanup

To remove compiled objects and executables:

```bash
make clean
```

To reset the cluster state (delete all logs and state files):

```bash
rm raft_*.state raft_*.log raft_*.log.meta
```

## Acknowledgments

This codebase was developed as part of the final projects for CS140e (Operating systems design and implementation) and CS244c (Advanced Networking and Distributed Systems).

The following online resources were incredibly valuable as guides for how to safely develop Raft:

- Project descriptions for [Project 1](https://web.stanford.edu/~ouster/cgi-bin/cs190-winter20/raft1.php) and [Project 2](https://web.stanford.edu/~ouster/cgi-bin/cs190-winter20/raft2.php) in Prof John Ousterhout's Winter 2020 CS190 (Software Design Studio);
- An MIT distributed systems class [writeup](https://thesquareplanet.com/blog/students-guide-to-raft/) sharing tips and tricks based on past experience.

Additionally, AI tools (Gemini CLI, Copilot) were used to develop the following components:

- `README.md` description to introduce project and describe usage;
- Extend debug messages for higher fidelity runtime logging;
- Compose the Makefile for ease of compilation over changing codebase structure.

Where applicable, inline comments document attributions for code that came from external sources not discussed here.
