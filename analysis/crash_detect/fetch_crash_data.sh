#!/usr/bin/env bash
 
# Remote hosts to pull from
HOSTS=(
    "instance-20260315-152927.asia-east1-a.cs244c-final-project-490103"
    "instance-20260315-152857.us-west1-b.cs244c-final-project-490103"
    "instance-20260315-152949.europe-north1-c.cs244c-final-project-490103"
    "instance-20260315-153025.australia-southeast1-a.cs244c-final-project-490103"
    "instance-20260315-153044.asia-south1-a.cs244c-final-project-490103"
)

CLIENT="instance-20260315-153004.us-central1-a.cs244c-final-project-490103"

NAME="$1"
 
# Destination directory for downloaded files
DEST="./data"
 
mkdir -p "$DEST"
 
for i in "${!HOSTS[@]}"; do
    HOST="${HOSTS[$i]}"
    NODE_ID=$((i))
    REMOTE_PATH="${HOST}:~/crash-detect/node_${NODE_ID}_${NAME}.csv"
    LOCAL_PATH="${DEST}/node_${NODE_ID}_${NAME}.csv"
 
    echo "Fetching node ${NODE_ID} from ${HOST}..."
    if scp "$REMOTE_PATH" "$LOCAL_PATH"; then
        echo "  -> Saved to ${LOCAL_PATH}"
    else
        echo "  -> ERROR: Failed to fetch from ${HOST}" >&2
    fi
done

echo "Fetching client from ${CLIENT}..."
REMOTE_PATH="${CLIENT}:~/crash-detect/client_${NAME}.csv"
LOCAL_PATH="${DEST}/client_${NAME}.csv"
if scp "$REMOTE_PATH" "$LOCAL_PATH"; then
    echo "  -> Saved to ${LOCAL_PATH}"
else
    echo "  -> ERROR: Failed to fetch from ${HOST}" >&2
fi
 
echo "Done."