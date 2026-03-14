from sshutil import ssh, ssh_bg

import subprocess

def detect_leader(nodes):
    """
    nodes: list of dicts like
    {"ssh": "node0", "id": 0}

    returns the node dict corresponding to the current leader
    """

    best_term = -1
    leader = None

    for n in nodes:
        cmd = f"grep became_leader telemetry_node_{n['id']}.csv | tail -n 1"

        result = subprocess.run(
            ["ssh", n["ssh"], cmd],
            capture_output=True,
            text=True
        )

        if not result.stdout.strip():
            continue

        parts = result.stdout.strip().split(",")
        term = int(parts[3])

        if term > best_term:
            best_term = term
            leader = n

    return leader

def peer_string(nodes):
    return ",".join(f"{n['internal']}:{n['port']}" for n in nodes)

def start_nodes(nodes, spec = ""):
    peers = peer_string(nodes)

    for i, n in enumerate(nodes):
        cmd = f"./raft-node --id {i} --peers {peers}"
        if len(spec) > 0:
            cmd += " " + spec
        ssh_bg(n["ssh"], cmd)

def stop_nodes(nodes):
    for n in nodes:
        ssh(n["ssh"], "pkill raft-node || true")

def clear_state(nodes):
    for n in nodes:
        ssh(n["ssh"], "rm -f raft-*")

def start_client(client, nodes, interval_ms, req):

    peers = peer_string(nodes)

    cmd = (
        f"./benchmark-client {interval_ms} 0 {req} {peers} "
        f"{client['internal']}:{client['port']}"
    )

    ssh_bg(client["ssh"], cmd)

def stop_client(client):
    ssh(client["ssh"], "pkill benchmark-client || true")
