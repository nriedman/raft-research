import json
import time
import random
from sim.old.cluster import *

TRIALS = 1000

with open("hosts.json") as f:
    config = json.load(f)

clusters = config["clusters"]

def run_trial(cluster, trial):

    nodes = cluster["nodes"]
    client = cluster["client"]

    stop_nodes(nodes)
    stop_client(client)
    clear_state(nodes)

    start_nodes(nodes)

    time.sleep(5)

    start_client(client, nodes)

    time.sleep(random.uniform(5,10))

    leader = detect_leader(nodes)
    if leader:
        ssh(leader["ssh"], "pkill raft-node")
    else:
        print("No leader found for trial {trial}")

    time.sleep(5)

    stop_client(client)
    stop_nodes(nodes)

for i in range(TRIALS):

    print("trial", i)

    for c in clusters:
        run_trial(c, i)