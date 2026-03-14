import subprocess
import json
import time
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

import argparse

HOSTS_FILE = "hosts.json"
REMOTE_DIR = "~"
CLIENT_CMD_TEMPLATE = "./benchmark-client 50 0 42 {peers} {client_addr}"  # adjust as needed
NUM_EXPERIMENTS = 10  # number of leader crashes per cluster

def run(cmd):
    print("Running:", " ".join(cmd))
    subprocess.run(cmd)

# ---------------- Node / Client Launch ----------------

def launch_node(host, node_id, peers, extra_args=""):
    peer_str = ",".join(peers)
    session_name = f"raft_node{node_id}"
    cmd = (
        f"tmux new-session -d -s {session_name} "
        f"'cd {REMOTE_DIR} && ./raft-node --id {node_id} --peers {peer_str} --t 300 450 > raft.log 2>&1'"
    )
    subprocess.run(["ssh", host, cmd])
    print(f"✅ Node {node_id} launched on {host} in tmux session {session_name}")

def launch_nodes(nodes, extra_args=""):
    peers = [f"{n['internal']}:{n['port']}" for n in nodes]
    for n in nodes:
        launch_node(n["ssh"], n["id"], peers, extra_args)

def start_client(client, nodes):
    peers = [f"{n['internal']}:{n['port']}" for n in nodes]
    peer_str = ",".join(peers)
    client_str = f"{client['internal']}:{client['port']}"
    session_name = "raft_client"
    remote_cmd = f"cd {REMOTE_DIR} && {CLIENT_CMD_TEMPLATE.format(peers=peer_str, client_addr=client_str)} > client.log 2>&1"
    ssh_cmd = ["ssh", client["ssh"], f"tmux new-session -d -s {session_name} '{remote_cmd}'"]
    subprocess.run(ssh_cmd)
    print(f"✅ Client launched on {client['ssh']} in tmux session {session_name}")

# ---------------- Leader Detection & Crash ----------------

# Experiment timing controls
CLUSTER_STABILIZE_TIME = 5      # seconds after nodes start
MAX_ELECTION_WAIT = 10          # max time to wait for a new leader
CRASH_BACKOFF = 5               # delay between crash experiments

def get_leader(nodes):
    leader_info = []
    for n in nodes:
        host = n["ssh"]
        remote_csv = f"{REMOTE_DIR}/node_{n['id']}*.csv"
        output = subprocess.run(["ssh", host, f"cat {remote_csv} || true"],
                                capture_output=True, text=True)
        for line in output.stdout.splitlines():
            cols = line.split(",")
            if len(cols) < 5:
                continue
            node_id, ts, event, term, _, _ = cols
            if event == "became_leader":
                leader_info.append((int(term), int(node_id)))
    if not leader_info:
        return None
    return max(leader_info)[1]

def crash_leader(nodes):
    leader_id = get_leader(nodes)
    if leader_id is None:
        print("⚠️ No leader detected, skipping crash")
        return None
    leader_host = next(n["ssh"] for n in nodes if n["id"] == leader_id)
    subprocess.run(["ssh", leader_host, "pkill raft-node"])
    print(f"💥 Leader {leader_id} killed on {leader_host}")
    return leader_id, leader_host

def restart_node(host, node_id, nodes):
    peers = [f"{n['internal']}:{n['port']}" for n in nodes]
    launch_node(host, node_id, peers)
    print(f"🔄 Node {node_id} restarted on {host}")

def wait_for_new_leader(nodes, old_leader):
    start = time.time()

    while time.time() - start < MAX_ELECTION_WAIT:
        new_leader = get_leader(nodes)

        if new_leader is not None and new_leader != old_leader:
            print(f"✅ New leader {new_leader} elected")
            return new_leader

        time.sleep(0.1)

    print("⚠️ Leader election timeout")
    return None

# ---------------- Cleanup ----------------

def cleanup_cluster(nodes, client):
    for n in nodes:
        subprocess.run(["ssh", n["ssh"], f"tmux kill-session -t raft_node{n['id']} || true"])
        print(f"🧹 Killed tmux session for node {n['id']} on {n['ssh']}")
    subprocess.run(["ssh", client["ssh"], "tmux kill-session -t raft_client || true"])
    print(f"🧹 Killed tmux session for client on {client['ssh']}")
    print("✅ Cluster cleanup complete")

# ---------------- Run single cluster ----------------

def run_cluster_experiment(cluster, extra_args=""):
    nodes = cluster["nodes"]
    client = cluster["client"]
    cluster_name = cluster["name"]
    print(f"=== Running experiment on cluster {cluster_name} ===")
    
    # Launch nodes and client
    launch_nodes(nodes, extra_args=extra_args)
    start_client(client, nodes)

    print(f"⏳ Waiting {CLUSTER_STABILIZE_TIME}s for cluster to stabilize...")
    time.sleep(CLUSTER_STABILIZE_TIME)
    
    # Repeat leader crash experiments
    for i in range(NUM_EXPERIMENTS):
        print(f"--- Cluster {cluster_name} Leader crash {i+1} ---")

        crashed = crash_leader(nodes)

        if crashed is None:
            print("⚠️ No leader detected, skipping iteration")
            time.sleep(CRASH_BACKOFF)
            continue

        old_leader_id, leader_host = crashed

        new_leader = wait_for_new_leader(nodes, old_leader_id)

        if new_leader is None:
            print("⚠️ Election failed, restarting crashed node and continuing")
            restart_node(leader_host, old_leader_id, nodes)
            time.sleep(CRASH_BACKOFF)
            continue

        restart_node(leader_host, old_leader_id, nodes)

        time.sleep(CRASH_BACKOFF)
    
    # Cleanup
    cleanup_cluster(nodes, client)
    print(f"✅ Cluster {cluster_name} experiment complete")

# ---------------- Main ----------------

def main():
    parser = argparse.ArgumentParser(description="Run Raft leader fault experiment on multiple clusters")
    parser.add_argument("raft_args", nargs=argparse.REMAINDER,
                        help="Arguments to forward to ./raft-node (e.g., --t lb ub or --a th ws rs)")
    args = parser.parse_args()

    extra_args = " ".join(args.raft_args)
    print(f"Forwarding extra args to raft-node: {extra_args}")

    if not Path(HOSTS_FILE).exists():
        print("❌ hosts.json not found")
        return

    with open(HOSTS_FILE) as f:
        data = json.load(f)

    clusters = data.get("clusters", [])
    
    # Run clusters in parallel threads
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=len(clusters)) as executor:
        futures = [executor.submit(run_cluster_experiment, cluster, extra_args) for cluster in clusters]
        for future in futures:
            future.result()  # wait for all clusters

if __name__ == "__main__":
    main()