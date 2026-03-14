import subprocess
import json
from pathlib import Path
import sys

HOSTS_FILE = "hosts.json"  # your hosts file
REMOTE_DIR = "~"       # directory where Makefile lives

def run(cmd):
    """Run a local command and exit if SSH fails"""
    print("Running:", " ".join(cmd))
    result = subprocess.run(cmd)
    if result.returncode != 0:
        print(f"⚠️  Warning: command failed (ignored)")

def clean_host(host):
    """Run make clean on a remote host, ignore errors if no Makefile"""
    cmd = ["ssh", host, f"cd {REMOTE_DIR} && make clean && make || true"]
    run(cmd)
    print(f"✅ Cleaned {host}")

def main():
    if not Path(HOSTS_FILE).exists():
        print(f"❌ {HOSTS_FILE} not found")
        sys.exit(1)

    with open(HOSTS_FILE) as f:
        data = json.load(f)

    for cluster in data.get("clusters", []):
        print(f"Cleaning cluster: {cluster['name']}")
        nodes = cluster.get("nodes", [])

        for n in nodes:
            host = n["ssh"]
            print(f"--- Cleaning {host} ---")
            clean_host(host)

    print("✅ Remote clean completed for all clusters")

if __name__ == "__main__":
    main()