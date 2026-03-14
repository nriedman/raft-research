import subprocess
import json
import sys
from pathlib import Path

HOSTS_FILE = "hosts.json"  # your hosts file
REMOTE_DIR = "~"       # destination on VM

HOSTS_SRC_DIR = "../src"
HOSTS_MAKEFILE = "../Makefile"

SRC_DIR = "src"             # directory
MAKEFILE = "Makefile"       # file

# Check for --force flag
FORCE = "--force" in sys.argv

def run(cmd, capture_output=False):
    """Run a command locally and exit on failure"""
    print("Running:", " ".join(cmd))
    result = subprocess.run(cmd, capture_output=capture_output, text=True)
    if result.returncode != 0:
        print(f"❌ Command failed: {' '.join(cmd)}")
        sys.exit(1)
    if capture_output:
        return result.stdout.strip()
    return result.returncode

def ensure_remote_dir(host):
    """Make REMOTE_DIR on the host if it doesn't exist"""
    cmd = ["ssh", host, f"mkdir -p {REMOTE_DIR}"]
    run(cmd)
    print(f"✅ Remote directory {REMOTE_DIR} ensured on {host}")

def exists_remote(host, path):
    """Check if a file or directory exists on remote host"""
    cmd = ["ssh", host, f"test -e {path} && echo exists || echo missing"]
    output = run(cmd, capture_output=True)
    return output.strip() == "exists"

def copy_makefile(host):
    """Copy Makefile to host"""
    remote_makefile = f"{REMOTE_DIR}/{MAKEFILE}"
    if not FORCE or not exists_remote(host, remote_makefile):
        # Normal copy
        cmd = ["scp", HOSTS_MAKEFILE, f"{host}:{remote_makefile}"]
        run(cmd)
        print(f"✅ Makefile copied to {host}")
    else:
        # Remove existing Makefile first
        run(["ssh", host, f"rm {remote_makefile}"])
        cmd = ["scp", HOSTS_MAKEFILE, f"{host}:{remote_makefile}"]
        run(cmd)
        print(f"✅ Makefile copied to {host}")

def copy_src(host):
    """Copy src/ directory to host, overwrite if --force"""
    remote_src = f"{REMOTE_DIR}/{SRC_DIR}"
    if not FORCE or not exists_remote(host, remote_src):
        # Normal copy
        cmd = ["scp", "-r", HOSTS_SRC_DIR, f"{host}:{remote_src}"]
        run(cmd)
        print(f"✅ src/ copied to {host}")
    else:
        # Force overwrite: remove remote src first
        run(["ssh", host, f"rm -rf {remote_src}"])
        cmd = ["scp", "-r", HOSTS_SRC_DIR, f"{host}:{remote_src}"]
        run(cmd)
        print(f"✅ src/ forcibly copied to {host}")

def build_on_vm(host):
    """Run make clean && make on the host"""
    cmd = ["ssh", host, f"cd {REMOTE_DIR} && make clean && make"]
    run(cmd)
    print(f"✅ Build completed on {host}")

def main():
    if not Path(HOSTS_FILE).exists():
        print(f"❌ {HOSTS_FILE} not found")
        sys.exit(1)

    with open(HOSTS_FILE) as f:
        data = json.load(f)

    for cluster in data.get("clusters", []):
        print(f"Preparing cluster: {cluster['name']}")
        nodes = cluster.get("nodes", [])
        client = cluster.get("client", [])

        for n in nodes + [client]:
            host = n["ssh"]
            print(f"--- Deploying to {host} ---")
            ensure_remote_dir(host)
            copy_makefile(host)
            copy_src(host)
            build_on_vm(host)

    print("✅ Deployment complete for all clusters")

if __name__ == "__main__":
    main()


"""
Rest of hosts.json:

,
    {
      "name": "clusterB",
      "nodes": [
        {"ssh": "instance-20260313-121124.europe-north2-b.cs244c-final-project-490103", "internal": "10.226.0.2", "port": 8080, "id": 0},
        {"ssh": "instance-20260313-121242.australia-southeast1-a.cs244c-final-project-490103", "internal": "10.152.0.2", "port": 8081, "id": 1},
        {"ssh": "instance-20260313-121439.us-central1-c.cs244c-final-project-490103", "internal": "10.128.0.3", "port": 8082, "id": 2}
      ],
      "client": {"ssh": "instance-20260313-121612.asia-southeast1-a.cs244c-final-project-490103", "internal": "10.148.0.2", "port": 9000}
    },
    {
      "name": "clusterC",
      "nodes": [
        {"ssh": "instance-20260313-121303.southamerica-west1-b.cs244c-final-project-490103", "internal": "10.194.0.2", "port": 8080, "id": 0},
        {"ssh": "instance-20260313-121319.asia-south1-a.cs244c-final-project-490103", "internal": "10.160.0.2", "port": 8081, "id": 1},
        {"ssh": "instance-20260313-121341.europe-west10-c.cs244c-final-project-490103", "internal": "10.214.0.2", "port": 8082, , "id": 2}
      ],
      "client": {"ssh": "instance-20260313-121453.asia-southeast2-a.cs244c-final-project-490103", "internal": "10.184.0.2", "port": 9000}
    }

"""
