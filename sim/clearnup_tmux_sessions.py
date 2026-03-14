import json
import subprocess
from pathlib import Path

HOSTS_FILE = "hosts.json"

def run(cmd):
    print("Running:", " ".join(cmd))
    subprocess.run(cmd, check=False)

def cleanup_host(host):
    print(f"🧹 Cleaning host: {host}")

    # Kill all tmux sessions
    run(["ssh", host, "tmux kill-server 2>/dev/null || true"])

    # Safety: kill leftover processes
    run(["ssh", host, "pkill raft-node 2>/dev/null || true"])
    run(["ssh", host, "pkill benchmark-client 2>/dev/null || true"])

def main():
    if not Path(HOSTS_FILE).exists():
        print("❌ hosts.json not found")
        return

    with open(HOSTS_FILE) as f:
        data = json.load(f)

    seen_hosts = set()

    for cluster in data["clusters"]:
        for node in cluster["nodes"]:
            seen_hosts.add(node["ssh"])

        seen_hosts.add(cluster["client"]["ssh"])

    print(f"Cleaning {len(seen_hosts)} hosts...")

    for host in seen_hosts:
        cleanup_host(host)

    print("✅ Cleanup complete.")

if __name__ == "__main__":
    main()