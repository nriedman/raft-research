#!/usr/bin/env python3
import subprocess
import sys

if len(sys.argv) != 3:
    print("Usage: python ssh_test.py <user> <public_ip>")
    sys.exit(1)

user = sys.argv[1]
ip = sys.argv[2]

cmd = [
    "ssh",
    "-o", "BatchMode=yes",
    "-o", "ConnectTimeout=5",
    f"{user}@{ip}",
    "echo connected"
]

try:
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode == 0 and "connected" in result.stdout:
        print("✅ SSH connection successful")
        print(result.stdout.strip())
    else:
        print("❌ SSH connection failed")
        print(result.stderr)

except Exception as e:
    print("❌ Error running ssh:", e)