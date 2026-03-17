# Geo-Replicated Raft Consensus Algorithm in C

An implementation of the Raft consensus algorithm in C, optimized for geo-replicated environments through adaptive failure detection. This project was developed as part of a [research study](https://www.scs.stanford.edu/26wi-cs244c/proj/georeplicated_raft.pdf) on the performance of Raft across multiple geographical regions.

## Overview

The project provides a complete Raft implementation with:
- **`raft-node`**: A Raft node that manages leader elections, log replication, state machine application, and persistence.
- **`raft-client`**: A benchmarking client (`benchmark-client`) that sends commands to the cluster, tracks latencies, and logs results to CSV for analysis.

## Key Features

- **Socket Transport**: TCP communication between nodes using a custom packet format.
- **Persistence**: Reliable persistence of term, vote, and log entries (`raft_<id>.state`, `raft_<id>.log`).
- **Phi Accrual Failure Detector**: An adaptive failure detection mechanism (instead of fixed timeouts) that uses heartbeats to build a probability distribution of expected arrival times, making it resilient to the high variance of geo-replicated networks.
- **Benchmarking & Analysis**: Built-in instrumentation for logging state transitions and RPC latencies, with automated scripts for analyzing cluster behavior under various network conditions.

## Getting Started

### Prerequisites

- GCC (or any C compiler)
- Make

### Building the Project

To compile the `raft-node` and `raft-client`, run:

```bash
make
```

### Running a Cluster

A cluster node is started with the following command:

```bash
./raft-node --id <n> --peers <ip:port>,... [options]
```

#### Core Arguments:
- `--id <n>`: The zero-indexed identifier for this node.
- `--peers <list>`: A comma-separated list of "IP:PORT" for all nodes in the cluster.

#### Failure Detection Options:
- **Randomized Timeout (Default)**: `--t <lb> <ub>`
  - Uses a fixed random timeout between `lb` and `ub` milliseconds.
  - Example: `--t 1000 2000`
- **Phi Accrual**: `--a <threshold> <window> <ramp>`
  - Uses the Phi Accrual failure detector.
  - `threshold`: The $\phi$ value at which to trigger an election (e.g., `1.0` to `3.0`).
  - `window`: Number of samples to keep in the moving window (e.g., `32`).
  - `ramp`: Number of initial heartbeats before accrual logic kicks in (defaults to randomized timeout until then).

#### Testing Options:
- `--crash_after_heartbeats <n>`: Automatically stops the node after receiving `n` heartbeats to simulate failures.

### Example: Local 3-Node Cluster

**Terminal 0:** `./raft-node --id 0 --peers 127.0.0.1:8000,127.0.0.1:8001,127.0.0.1:8002 --a 1.0 32 8`

**Terminal 1:** `./raft-node --id 1 --peers 127.0.0.1:8000,127.0.0.1:8001,127.0.0.1:8002 --a 1.0 32 8`

**Terminal 2:** `./raft-node --id 2 --peers 127.0.0.1:8000,127.0.0.1:8001,127.0.0.1:8002 --a 1.0 32 8`


### Using the Client

The `raft-client` (built from `src/benchmark-client.c`) is used to benchmark the cluster:

```bash
./raft-client <peers> <client_addr> [throttle_ms]
```

- `peers`: Comma-separated list of cluster nodes.
- `client_addr`: The address the client should bind to for receiving responses.
- `throttle_ms`: Optional delay between requests.

Example:

```bash
./raft-client 127.0.0.1:8000,127.0.0.1:8001,127.0.0.1:8002 127.0.0.1:9000 100
```
The client will log all request latencies and results to `client.csv`.

## Geo-Replication & Benchmarking

- `raft-node-wrapper.sh`: A shell script to ensure nodes stay alive during long-running experiments.
- `analysis/`: Contains tools for analyzing performance traces and detecting crash recovery times.

Each node logs its internal state transitions to a CSV file named `node_<id>_<scheme>_...csv` for offline analysis.

## Project Structure

- `src/raft.c`: Core Raft state machine and consensus logic.
- `src/accrual.c`: Phi Accrual failure detector implementation.
- `src/transport-socket/`: TCP socket transport layer.
- `src/persist-unix/`: Disk-based persistence (log and state fields).
- `src/util-unix/node.c`: Entry point for `raft-node`.
- `src/util-unix/benchmark-client.c`: Entry point for `raft-client`.
- `src/rpc.c`: RPC packet serialization and handling.

## Acknowledgments

This codebase was developed as part of the final projects for CS140e (Operating systems design and implementation) and CS244c (Advanced Networking and Distributed Systems).

The following online resources were incredibly valuable as guides for how to safely develop Raft:

- Project descriptions for [Project 1](https://web.stanford.edu/~ouster/cgi-bin/cs190-winter20/raft1.php) and [Project 2](https://web.stanford.edu/~ouster/cgi-bin/cs190-winter20/raft2.php) in Prof John Ousterhout's Winter 2020 CS190 (Software Design Studio);
- An MIT distributed systems class [writeup](https://thesquareplanet.com/blog/students-guide-to-raft/) sharing tips and tricks based on past experience.

Additionally, AI tools (Gemini CLI, Copilot) were used to develop the following components:

- `README.md` description to introduce project and describe usage;
- Extend debug messages for higher fidelity runtime logging;
- Compose the Makefile for ease of compilation over changing codebase structure;
- Implement a socket-based transport layer (see `srd/transport-socket/*`);
- Implement an example client functionality in `srd/util-unix/client.c`;
- Fill in the method bodies in `persist-unix/log.c` and `persist-unix/persistent-fields.c`;
- Refactor the `apply_to_state_machine` method in `src/raft.c`;
- Add outstanding client request caching in `src/raft.*`;
- Create and style the plots in `analysis/*.ipynb`;
- 

Beyond this list, any commits marked with `[AI]` were developed using AI coding tools. Where applicable, inline comments document attributions for code that came from external sources not discussed here.
