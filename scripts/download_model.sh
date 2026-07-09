#!/usr/bin/env bash
#
# Download FunctionGemma (google/functiongemma-270m-it) as a GGUF for llama.cpp.
#
# Strategy:
#   1. Try to pull a prebuilt GGUF from a community/official repo.
#   2. If no GGUF exists, fall back to downloading the HF weights and converting
#      them with llama.cpp's convert_hf_to_gguf.py (see scripts/convert_model.sh).
#
# Usage:
#   ./scripts/download_model.sh              # default: Q8_0 quant into ./models
#   MODEL_QUANT=F16 ./scripts/download_model.sh
#   GGUF_REPO=someone/functiongemma-270m-it-GGUF ./scripts/download_model.sh
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODELS_DIR="${MODELS_DIR:-$ROOT_DIR/models}"
mkdir -p "$MODELS_DIR"

# A GGUF repo that mirrors google/functiongemma-270m-it. Override with GGUF_REPO=...
# if the default 404s (community re-uploads move around).
GGUF_REPO="${GGUF_REPO:-ggml-org/functiongemma-270m-it-GGUF}"
MODEL_QUANT="${MODEL_QUANT:-Q8_0}"        # Q8_0 is tiny + high quality for a 270M model
# HF GGUF filenames are lowercase (q8_0); only use this for the remote path.
QUANT_LC="$(printf '%s' "$MODEL_QUANT" | tr '[:upper:]' '[:lower:]')"
OUT_FILE="$MODELS_DIR/functiongemma-270m-it-${MODEL_QUANT}.gguf"

echo ">> Target: $OUT_FILE"
echo ">> Source repo: $GGUF_REPO  (quant $MODEL_QUANT)"

if [[ -f "$OUT_FILE" ]]; then
  echo ">> Already present, nothing to do. Delete it to re-download."
  exit 0
fi

have() { command -v "$1" >/dev/null 2>&1; }

# Preferred: huggingface-cli (handles auth + resumable downloads).
if have huggingface-cli; then
  echo ">> Using huggingface-cli"
  if huggingface-cli download "$GGUF_REPO" \
        --include "*${MODEL_QUANT}*.gguf" \
        --local-dir "$MODELS_DIR" 2>/dev/null; then
    # Normalize whatever filename came down to our expected name.
    found="$(find "$MODELS_DIR" -maxdepth 2 -iname "*${MODEL_QUANT}*.gguf" | head -n1 || true)"
    if [[ -n "$found" && "$found" != "$OUT_FILE" ]]; then
      mv "$found" "$OUT_FILE"
    fi
  fi
fi

# Fallback: direct curl from the HF resolve endpoint.
if [[ ! -f "$OUT_FILE" ]]; then
  URL="https://huggingface.co/${GGUF_REPO}/resolve/main/functiongemma-270m-it-${QUANT_LC}.gguf"
  echo ">> huggingface-cli unavailable or failed; trying direct download:"
  echo "   $URL"
  if have curl; then
    curl -L --fail -o "$OUT_FILE" "$URL" || true
  elif have wget; then
    wget -O "$OUT_FILE" "$URL" || true
  fi
fi

if [[ -f "$OUT_FILE" && -s "$OUT_FILE" ]]; then
  echo ">> Done: $OUT_FILE"
  echo ">> Next: ./scripts/run_server.sh"
else
  rm -f "$OUT_FILE"
  cat <<EOF

!! Could not fetch a prebuilt GGUF for google/functiongemma-270m-it.
   This is expected if no community GGUF exists at the default repo.

   Option A - point at a repo you know has the GGUF:
     GGUF_REPO=<user>/<repo> ./scripts/download_model.sh

   Option B - convert the HF weights to GGUF with llama.cpp's own converter
   (part of the llama.cpp checkout, run once):
     python3 <llama.cpp>/convert_hf_to_gguf.py <hf_download_dir> \\
         --outfile "$OUT_FILE" --outtype q8_0
   (This project ships no Python; the converter is a stock llama.cpp utility
    you only ever run a single time to produce the .gguf.)
EOF
  exit 1
fi
