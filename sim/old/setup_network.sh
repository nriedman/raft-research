#!/bin/bash

# Usage: sudo ./setup_network.sh <number_of_nodes>
N=$1
BRIDGE="bridge"
NETWORK="10.10.0"

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root (use sudo)"
   exit 1
fi

if [[ -z "$N" ]]; then
    echo "Usage: ./setup_network.sh <n_nodes>"
    exit 1
fi

cleanup() {
    echo "Cleaning up existing network..."
    for i in $(seq 1 254); do
        ip netns del "node$i" 2>/dev/null
        ip link del "veth-br$i" 2>/dev/null
    done
    ip link del $BRIDGE 2>/dev/null
}

setup() {
    echo "Setting up $N nodes on bridge $BRIDGE ($NETWORK.0/24)..."
    
    # Create bridge
    ip link add $BRIDGE type bridge
    ip link set $BRIDGE up

    for i in $(seq 1 $N); do
        NS="node$i"
        VETH_NS="veth-ns$i"
        VETH_BR="veth-br$i"
        IP="$NETWORK.$i"

        # Create namespace
        ip netns add $NS

        # Create veth pair
        ip link add $VETH_NS type veth peer name $VETH_BR
        
        # Connect to bridge and namespace
        ip link set $VETH_NS netns $NS
        ip link set $VETH_BR master $BRIDGE
        
        # Configure IP and interfaces
        ip -n $NS addr add "$IP/24" dev $VETH_NS
        ip -n $NS link set $VETH_NS up
        ip -n $NS link set lo up
        ip link set $VETH_BR up

        # Initialize traffic control (tc) hierarchy 
        # Create a root hierarchical token bucket (htb)
        # Handle 1: is the root. Default traffic goes to class 1:1 (no delay)
        ip netns exec $NS tc qdisc add dev $VETH_NS root handle 1: htb default 1
        ip netns exec $NS tc class add dev $VETH_NS parent 1: classid 1:1 htb rate 1gbit
        
        echo "Node $NS initialized at $IP"
    done
}

# Function to set delay from one node to another
# Usage: set_delay <src_id> <dst_id> <delay_ms>
set_delay() {
    SRC_ID=$1
    DST_ID=$2
    MS=$3
    
    NS="node$SRC_ID"
    IF="veth-ns$SRC_ID"
    DST_IP="$NETWORK.$DST_ID"
    CLASS_ID="1:$(($DST_ID + 100))" # Unique class ID for this destination
    
    # Check if the class already exists, if so, we modify it, if not, we create it
    if ip netns exec $NS tc class show dev $IF | grep -q "class htb $CLASS_ID"; then
        ip netns exec $NS tc qdisc change dev $IF parent $CLASS_ID handle $(($DST_ID + 100)): netem delay ${MS}ms
        echo "Updated delay: node$SRC_ID -> node$DST_ID to ${MS}ms"
    else
        # Create a class for this specific destination
        ip netns exec $NS tc class add dev $IF parent 1: classid $CLASS_ID htb rate 1gbit
        # Add netem delay to that class
        ip netns exec $NS tc qdisc add dev $IF parent $CLASS_ID handle $(($DST_ID + 100)): netem delay ${MS}ms
        # Add a filter to route traffic for DST_IP into this class
        ip netns exec $NS tc filter add dev $IF protocol ip parent 1: prio 1 u32 match ip dst $DST_IP flowid $CLASS_ID
        echo "Set initial delay: node$SRC_ID -> node$DST_ID to ${MS}ms"
    fi
}

if [[ "$2" == "delay" ]]; then
    set_delay $3 $4 $5
elif [[ "$1" == "clean" ]]; then
    cleanup
else
    cleanup
    setup
fi