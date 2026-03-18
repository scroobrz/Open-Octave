#!/bin/bash

# laptop hosting
# Starts the Open Octave backend and frontend for Demo 3 laptop hosting.
# Keep this laptop awake and allow incoming connections if macOS asks.

set -e

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Starting Open Octave..."
echo "Repo: $REPO_DIR"

# Start backend
osascript <<EOF
tell application "Terminal"
    do script "cd \"$REPO_DIR/software/controller\" && npm run start:laptop"
end tell
EOF

sleep 2

# Start frontend
osascript <<EOF
tell application "Terminal"
    do script "cd \"$REPO_DIR/software/web\" && npm run dev:laptop"
end tell
EOF

sleep 4

# Open browser
open "http://localhost:5173"

echo "Open Octave launched."