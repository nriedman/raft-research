import subprocess

HOST = "instance-20260313-030900.us-west1-b.cs244c-final-project-490103"
LOCAL_FILE = "../raft-node"
REMOTE_PATH = "~/raft-node"

def run(cmd):
    print("Running:", " ".join(cmd))
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.stdout:
        print(result.stdout)

    if result.stderr:
        print(result.stderr)

    return result.returncode


# 1. Copy binary
scp_cmd = ["scp", LOCAL_FILE, f"{HOST}:{REMOTE_PATH}"]
if run(scp_cmd) != 0:
    print("❌ SCP failed")
    exit(1)

print("✅ File copied")

# 2. Make executable
chmod_cmd = ["ssh", HOST, f"chmod +x {REMOTE_PATH}"]
if run(chmod_cmd) != 0:
    print("❌ chmod failed")
    exit(1)

# 3. Run the executable
run_cmd = ["ssh", HOST, f"{REMOTE_PATH}"]
run(run_cmd)