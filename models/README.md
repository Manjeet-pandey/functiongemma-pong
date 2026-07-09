# models/

GGUF weights for FunctionGemma live here. They are **not** checked in.

Populate this directory with:

```bash
./scripts/download_model.sh
```

which produces something like:

```
models/functiongemma-270m-it-Q8_0.gguf
```

`scripts/run_server.sh` picks up the first `*.gguf` it finds here (override with
`MODEL=...`). A fine-tuned LoRA adapter (see `../finetune/README.md`) can also be
placed here, e.g. `models/pong-lora.gguf`.
