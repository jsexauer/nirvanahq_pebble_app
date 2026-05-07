#!/bin/bash

# Default emulator platform is diorite (Time 2)
EMULATOR=${1:-diorite}

# Get directory of this script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

echo "Building Pebble app..."
cd "$DIR" && ~/.local/bin/pebble build

echo "Starting Pebble emulator ($EMULATOR) and streaming logs..."
cd "$DIR" && ~/.local/bin/pebble install --emulator $EMULATOR --logs
