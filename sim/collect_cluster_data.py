import json
import subprocess
from pathlib import Path

HOSTS_FILE = "hosts.json"
REMOTE_DIR = "~"
LOCAL_DIR = Path(".")  # save in project root

def run(cmd):
    """Run a subprocess command and print it."""
    print("Running:", " ".join(cmd))
    subprocess.run(cmd, check=True)

def fetch_file(remote_host, remote_path, local_path):
    """Fetch a file from a remote host to a local path."""
    local_path.parent.mkdir(parents=True, exist_ok=True)
    scp_cmd = ["scp", f"{remote_host}:{remote_path}", str(local_path)]
    run(scp_cmd)

def collect_cluster_data(cluster):
    cluster_name = cluster["name"]
    nodes = cluster["nodes"]
    client = cluster["client"]

    print(f"=== Collecting data for cluster {cluster_name} ===")

    # Ensure archive directory exists on remote host
    run(["ssh", client["ssh"], f"mkdir -p {REMOTE_DIR}/archive"])

    # Fetch client benchmark file
    client_csv_remote = f"{REMOTE_DIR}/client_benchmark.csv"
    client_csv_local = LOCAL_DIR / f"{cluster_name}_client_benchmark.csv"
    fetch_file(client["ssh"], client_csv_remote, client_csv_local)
    print(f"✅ Retrieved client CSV for {cluster_name}")

    # Move the file to archive on remote
    run(["ssh", client["ssh"], f"mv {client_csv_remote} {REMOTE_DIR}/archive/"])
    print(f"✅ Retrieved and archived client CSV for {cluster_name}")

    # Fetch node logs
    for n in nodes:
        # Pattern matching all node CSVs
        remote_pattern = f"{REMOTE_DIR}/node_{n['id']}_*.csv"

        # Use scp with wildcard; will need quotes so shell expands on remote host
        local_pattern_dir = LOCAL_DIR / f"{cluster_name}_node_{n['id']}"
        local_pattern_dir.mkdir(exist_ok=True)
        scp_cmd = f"scp {n['ssh']}:{remote_pattern} {local_pattern_dir}/"
        print(f"Running: {scp_cmd}")
        subprocess.run(scp_cmd, shell=True, check=False)  # shell needed for wildcard

        # Move remote CSVs into archive
        run(["ssh", n["ssh"], f"mv {remote_pattern} {REMOTE_DIR}/archive/"])
        print(f"✅ Retrieved and archived node {n['id']} CSVs for {cluster_name}")

def main():
    if not Path(HOSTS_FILE).exists():
        print("❌ hosts.json not found")
        return

    with open(HOSTS_FILE) as f:
        data = json.load(f)

    clusters = data.get("clusters", [])
    for cluster in clusters:
        collect_cluster_data(cluster)

    print("✅ All cluster data collected!")

if __name__ == "__main__":
    main()