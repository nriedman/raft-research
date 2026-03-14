import subprocess

"""
,
    {
      "name": "clusterB",
      "nodes": [
        {"ssh": "instance-20260313-121124.europe-north2-b.cs244c-final-project-490103", "internal": "10.226.0.2", "port": 8080},
        {"ssh": "instance-20260313-121242.australia-southeast1-a.cs244c-final-project-490103", "internal": "10.152.0.2", "port": 8081},
        {"ssh": "instance-20260313-121439.us-central1-c.cs244c-final-project-490103", "internal": "10.128.0.3", "port": 8082}
      ],
      "client": {"ssh": "instance-20260313-121612.asia-southeast1-a.cs244c-final-project-490103", "internal": "10.148.0.2", "port": 9000}
    },
    {
      "name": "clusterC",
      "nodes": [
        {"ssh": "instance-20260313-121303.southamerica-west1-b.cs244c-final-project-490103", "internal": "10.194.0.2", "port": 8080},
        {"ssh": "instance-20260313-121319.asia-south1-a.cs244c-final-project-490103", "internal": "10.160.0.2", "port": 8081},
        {"ssh": "instance-20260313-121341.europe-west10-c.cs244c-final-project-490103", "internal": "10.214.0.2", "port": 8082}
      ],
      "client": {"ssh": "instance-20260313-121453.asia-southeast2-a.cs244c-final-project-490103", "internal": "10.184.0.2", "port": 9000}
    }
"""

def ssh(host, cmd):
    return subprocess.run(
        ["ssh", host, cmd],
        capture_output=True,
        text=True
    )

def ssh_bg(host, cmd):
    full = f"nohup {cmd} > process.log 2>&1 &"
    subprocess.Popen(["ssh", host, full])

def scp(src, dst):
    subprocess.run(["scp", src, dst])
