# Optional: fine-tuning FunctionGemma for Pong

This step is **optional**. The game runs against the base
`google/functiongemma-270m-it` out of the box. Fine-tune only if the base
model's zero-shot `move_paddle` calls are unreliable (malformed calls, wrong
`target_y`, refuses to call the tool).

Everything here is C++ / shell — **no Python is required to build or run the
game**. The only place Python can appear is the one-time GGUF conversion, which
is a stock llama.cpp utility, not part of this project.

## 1. Generate the synthetic dataset (C++)

`gen_dataset.cpp` reuses the game's real constants and the exact
`SerializeStateForModel()` the game sends at runtime, so training prompts are
byte-identical to live prompts. Labels come from an analytic ball-trajectory
oracle (with wall bounces). **This oracle is only used to make training data —
the live game never uses it; the paddle is always driven by the model.**

```bash
# from the repo root, after configuring CMake once:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target gen_dataset

mkdir -p finetune/data
./build/gen_dataset 4000 finetune/data/train.jsonl 42
./build/gen_dataset  400 finetune/data/val.jsonl  123
```

Each JSONL line has:

- `state`      — the compact state line (what the game serializes)
- `prompt`     — the full FunctionGemma prompt (control tokens + tool decl)
- `completion` — the target `call:move_paddle{...}` the model should emit
- `text`       — `prompt + completion` concatenated, for raw-text trainers

## 2. Train with llama.cpp

llama.cpp ships LoRA fine-tuning tooling that trains directly on GGUF weights
from a raw-text corpus — no Python training stack.

```bash
# Produce a plain-text corpus from the `text` field (one example per block):
#   (jq is optional; any tool that extracts the "text" field works)
jq -r '.text' finetune/data/train.jsonl > finetune/data/train.txt

# Then run llama.cpp's finetune example against the base GGUF, e.g.:
#   llama-finetune \
#     --model-base models/functiongemma-270m-it-Q8_0.gguf \
#     --train-data finetune/data/train.txt \
#     --lora-out models/pong-lora.gguf \
#     --epochs 3 --batch 8 --ctx 512
#
# (Exact flags vary by llama.cpp version — check `llama-finetune --help`.
#  Some builds name it `finetune`. LoRA training on GGUF is evolving upstream;
#  if your build lacks it, do LoRA the usual way and export a GGUF adapter.)
```

## 3. Serve the fine-tuned model

Point `run_server.sh` at the base model and attach the LoRA adapter:

```bash
LLAMA_SERVER=llama-server \
  llama-server --model models/functiongemma-270m-it-Q8_0.gguf \
               --lora models/pong-lora.gguf \
               --host 127.0.0.1 --port 8080 --jinja
```

The game needs no changes — it just talks to `127.0.0.1:8080` as usual.

## Notes

- Keep `temperature` low (the game uses 0.1) so `target_y` is stable.
- If you change the tool schema or the prompt template in
  `src/model_client.cpp`, regenerate the dataset — `BuildPrompt()` in
  `gen_dataset.cpp` is kept byte-identical to it and must stay in sync.
