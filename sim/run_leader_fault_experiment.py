import subprocess
import json
import time
from pathlib import Path

HOSTS_FILE = "hosts.json"
REMOTE_DIR = "~"
CLIENT_CMD_TEMPLATE = "./benchmark-client 100 0 42 {peers} {client_addr}"  # adjust as needed
NUM_EXPERIMENTS = 10  # number of leader crashes to perform

def run(cmd):
    print("Running:", " ".join(cmd))
    subprocess.run(cmd)

def launch_node(host, node_id, peers):
    peer_str = ",".join(peers)
    session_name = f"raft_node{node_id}"
    cmd = (
        f"tmux new-session -d -s {session_name} "
        f"'cd {REMOTE_DIR} && ./raft-node --id {node_id} --peers {peer_str} > raft.log 2>&1'"
    )
    subprocess.run(["ssh", host, cmd])
    print(f"✅ Node {node_id} launched on {host} in tmux session {session_name}")

def start_client(client, nodes):
    peers = [f"{n['internal']}:{n['port']}" for n in nodes]
    peer_str = ",".join(peers)
    client_str = f"{client['internal']}:{client['port']}"
    session_name = "raft_client"
    cmd = (
        f"tmux new-session -d -s {session_name} "
        f"'cd {REMOTE_DIR} && {CLIENT_CMD_TEMPLATE.format(peers=peer_str, client_addr=client_str)} > client.log 2>&1'"
    )
    subprocess.run(["ssh", client["ssh"], cmd])
    print(f"✅ Client launched on {client['ssh']} in tmux session {session_name}")

def launch_nodes(nodes):
    peers = [f"{n['internal']}:{n['port']}" for n in nodes]
    for n in nodes:
        launch_node(n["ssh"], n["id"], peers)

def get_leader(nodes):
    leader_info = []
    for n in nodes:
        host = n["ssh"]
        remote_csv = f"{REMOTE_DIR}/telemetry_node_{n['id']}.csv"
        output = subprocess.run(["ssh", host, f"cat {remote_csv} || true"],
                                capture_output=True, text=True)
        for line in output.stdout.splitlines():
            cols = line.split(",")
            if len(cols) < 5:
                continue
            node_id, ts, event, term, _ = cols
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
    run(["ssh", leader_host, "pkill raft-node"])
    print(f"💥 Leader {leader_id} killed on {leader_host}")
    return leader_id, leader_host

def restart_node(host, node_id, nodes):
    peers = [f"{n['internal']}:{n['port']}" for n in nodes]
    launch_node(host, node_id, peers)
    print(f"🔄 Node {node_id} restarted on {host}")

def wait_for_new_leader(nodes, old_leader):
    start = time.time()
    while True:
        new_leader = get_leader(nodes)
        if new_leader is not None and new_leader != old_leader:
            print(f"✅ New leader {new_leader} elected")
            return new_leader
        time.sleep(0.05)

def cleanup_cluster(nodes, client):
    for n in nodes:
        subprocess.run(["ssh", n["ssh"], "tmux kill-session -t raft_node{} || true".format(n["id"])])
        print(f"🧹 Killed tmux session for node {n['id']} on {n['ssh']}")
    subprocess.run(["ssh", client["ssh"], "tmux kill-session -t raft_client || true"])
    print(f"🧹 Killed tmux session for client on {client['ssh']}")

def main():
    if not Path(HOSTS_FILE).exists():
        print("❌ hosts.json not found")
        return

    with open(HOSTS_FILE) as f:
        data = json.load(f)

    for cluster in data.get("clusters", []):
        print(f"=== Running experiment on cluster: {cluster['name']} ===")
        nodes = cluster.get("nodes", [])
        client = cluster.get("client")

        # Launch nodes and client
        launch_nodes(nodes)
        start_client(client, nodes)

        # Perform repeated leader crashes
        for i in range(NUM_EXPERIMENTS):
            print(f"--- Leader crash {i+1} ---")
            crashed = crash_leader(nodes)
            if crashed is None:
                continue
            old_leader_id, leader_host = crashed
            wait_for_new_leader(nodes, old_leader_id)
            restart_node(leader_host, old_leader_id, nodes)
            time.sleep(1)

        # Cleanup at the end
        cleanup_cluster(nodes, client)

    print("✅ Leader fault experiment execution complete")

if __name__ == "__main__":
    main()