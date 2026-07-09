#!/usr/bin/env bash
#
# Configure, build, and launch the Pong game.
# Assumes llama-server is already running (see scripts/run_server.sh).
#
# Usage:
#   ./scripts/build_and_run.sh                 # default cadence 100ms
#   ./scripts/build_and_run.sh --cadence 60    # harder AI
#   ./scripts/build_and_run.sh --mouse
#   NO_RUN=1 ./scripts/build_and_run.sh        # build only
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j

if [[ "${NO_RUN:-0}" == "1" ]]; then
  echo ">> Built: $BUILD_DIR/pong  (NO_RUN set, not launching)"
  exit 0
fi

echo ">> Launching pong (Ctrl-C or Esc to quit)"
exec "$BUILD_DIR/pong" "$@"
