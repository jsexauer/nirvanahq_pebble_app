#!/bin/bash

# Default emulator platform is basalt (Time)
EMULATOR=${1:-basalt}

# Get directory of this script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

echo "Starting Pebble emulator ($EMULATOR) and streaming logs..."
cd "$DIR" && ~/.local/bin/pebble install --emulator $EMULATOR --logs
