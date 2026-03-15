#!/usr/bin/env bash

RESTART_DELAY=2

while true; do

	start=$(date +%s)

	./raft-node "$@"
	exit_code=$?

	end=$(date +%s)
	runtime=$((end - start))

	echo "$(date) raft-node exited (code=$exit_code, runtime=${runtime})s"

	sleep $RESTART_DELAY

done

