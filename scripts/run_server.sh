#!/usr/bin/env bash
#
# Launch FunctionGemma on a local llama.cpp server (llama-server).
# The game talks to this over HTTP on 127.0.0.1:8080 (both the native
# /completion path and the OpenAI /v1/chat/completions path work).
#
# Prereqs:
#   - llama.cpp built with the server (provides `llama-server`), on your PATH,
#     or set LLAMA_SERVER=/path/to/llama-server
#   - a GGUF in ./models (run ./scripts/download_model.sh first)
#
# Usage:
#   ./scripts/run_server.sh
#   MODEL=models/functiongemma-270m-it-Q8_0.gguf PORT=8080 ./scripts/run_server.sh
#   NGL=99 ./scripts/run_server.sh        # offload all layers to GPU (Metal/CUDA)
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODELS_DIR="$ROOT_DIR/models"

# Pick the first GGUF in ./models unless MODEL is set explicitly.
MODEL="${MODEL:-}"
if [[ -z "$MODEL" ]]; then
  MODEL="$(find "$MODELS_DIR" -maxdepth 1 -iname '*.gguf' | sort | head -n1 || true)"
fi
if [[ -z "$MODEL" || ! -f "$MODEL" ]]; then
  echo "!! No GGUF found. Run ./scripts/download_model.sh first, or set MODEL=..." >&2
  exit 1
fi

LLAMA_SERVER="${LLAMA_SERVER:-llama-server}"
if ! command -v "$LLAMA_SERVER" >/dev/null 2>&1 && [[ ! -x "$LLAMA_SERVER" ]]; then
  echo "!! '$LLAMA_SERVER' not found. Build llama.cpp and put llama-server on PATH," >&2
  echo "   or set LLAMA_SERVER=/path/to/llama-server." >&2
  exit 1
fi

PORT="${PORT:-8080}"
HOST="${HOST:-127.0.0.1}"
NGL="${NGL:-99}"                 # GPU layers; 270M fits easily, offload all
CTX="${CTX:-2048}"
THREADS="${THREADS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

echo ">> Serving: $MODEL"
echo ">> Endpoint: http://$HOST:$PORT   (ctx $CTX, ngl $NGL, threads $THREADS)"
echo ">> --jinja enables the OpenAI tool-calling path used as the game's fallback."

exec "$LLAMA_SERVER" \
  --model "$MODEL" \
  --host "$HOST" \
  --port "$PORT" \
  --ctx-size "$CTX" \
  --n-gpu-layers "$NGL" \
  --threads "$THREADS" \
  --jinja \
  --parallel 1 \
  --cont-batching
