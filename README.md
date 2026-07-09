# FunctionGemma Pong (C++ / SDL2 / llama.cpp)

Desktop Pong where **you** play the left paddle and the **right paddle is driven
entirely by real-time function-calling inference** from
`google/functiongemma-270m-it`, served locally by llama.cpp.

There is no scripted tracker and no ball-trajectory predictor steering the AI
paddle at runtime. Every cycle the game serializes the world state, sends it to
FunctionGemma, and moves the paddle toward the `target_y` the model returns from
its `move_paddle(...)` function call. Inference runs on a background thread so
the render loop stays pinned at 60 fps regardless of model latency.

> The only trajectory oracle in this repo lives in `finetune/gen_dataset.cpp`
> and is used **solely to label synthetic training data** for the optional
> fine-tune. The game itself never calls it.

---

## What you get

```
src/
  main.cpp         Window, input, 60fps render loop, wiring
  game.{hpp,cpp}   Ball physics, paddle reflection, scoring, state serialization
  model_client.*   llama.cpp HTTP client: builds FunctionGemma prompts, parses calls
  ai_agent.*       Background inference thread + shared latest-decision snapshot
  hud.{hpp,cpp}    In-window debug overlay (drawn on the game canvas)
scripts/
  download_model.sh    Pull the FunctionGemma GGUF into models/
  run_server.sh        Launch llama-server on 127.0.0.1:8080
  build_and_run.sh     Configure, build, and run the game
finetune/
  gen_dataset.cpp      C++ synthetic-dataset generator (optional fine-tune)
  README.md            How to fine-tune with llama.cpp (no Python training stack)
models/  assets/        GGUF weights / optional HUD font (see their READMEs)
```

## Requirements

- A C++17 toolchain + CMake ≥ 3.16
- **SDL2** and **SDL2_ttf**
- **libcurl** (game ↔ model HTTP) and **nlohmann/json** (auto-fetched if missing)
- **llama.cpp** built with the server (`llama-server`) on your `PATH`

Install the libs:

```bash
# macOS
brew install cmake sdl2 sdl2_ttf curl nlohmann-json

# Debian/Ubuntu
sudo apt install cmake build-essential libsdl2-dev libsdl2-ttf-dev \
                 libcurl4-openssl-dev nlohmann-json3-dev
```

Build llama.cpp (once) and put its binaries on your PATH:

```bash
git clone https://github.com/ggml-org/llama.cpp && cd llama.cpp
cmake -B build -DLLAMA_CURL=ON && cmake --build build -j
export PATH="$PWD/build/bin:$PATH"     # provides llama-server
```

---

## Run it (3 terminals, or background the server)

### 1. Get the model

```bash
./scripts/download_model.sh
# -> models/functiongemma-270m-it-Q8_0.gguf
```

If no prebuilt GGUF is available for your setup, the script prints how to point
it at another repo (`GGUF_REPO=...`) or convert the HF weights once with
llama.cpp's stock converter. See `models/README.md`.

### 2. Start FunctionGemma on llama.cpp

```bash
./scripts/run_server.sh
# serves http://127.0.0.1:8080  (native /completion + OpenAI /v1 tool path)
```

### 3. Build and launch the game

```bash
./scripts/build_and_run.sh
# or manually:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
#   ./build/pong
```

If the model server isn't up yet, the game still opens and the HUD shows a
"waiting for model" line; the AI paddle starts moving as soon as the first
function call comes back.

---

## Controls

| Key            | Action                                        |
|----------------|-----------------------------------------------|
| `W` / `S`      | Move your paddle up / down                    |
| `↑` / `↓`      | Same                                          |
| `--mouse`      | (launch flag) control your paddle with mouse Y|
| `[`            | Increase cadence +25 ms (easier AI)           |
| `]`            | Decrease cadence −25 ms (harder AI)           |
| `R`            | Reset scores                                  |
| `Esc`          | Quit                                          |

## AI difficulty = decision cadence

The AI's skill is governed by how often it's allowed to re-decide. Lower cadence
→ the paddle re-targets more often → harder opponent.

```bash
./build/pong --cadence 100    # default
./build/pong --cadence 60     # harder (re-decides more often)
./build/pong --cadence 200    # easier (stale target longer)
```

You can also tune it live with `[` and `]`. Other launch flags:

```bash
./build/pong --url http://127.0.0.1:8080   # model server base URL
./build/pong --mouse                       # mouse control
./build/pong --help
```

**Graceful degradation:** if a decision takes longer than the cadence, the
paddle keeps interpolating toward the last `target_y` the model returned instead
of freezing — inference never blocks the render loop.

---

## The AI decision loop

1. **Serialize** (`SerializeStateForModel`, `game.cpp`) — one compact line:
   ```
   ball_x=.. ball_y=.. ball_vx=.. ball_vy=.. paddle_y=.. paddle_height=80 \
   screen_height=600 distance_to_paddle=..
   ```
2. **Prompt** (`ModelClient::BuildPrompt`) — FunctionGemma control-token format
   declaring exactly one tool:
   `move_paddle(direction: "up"|"down"|"stay", target_y: int)`.
3. **Infer** on a worker thread (`AIAgent`) via llama.cpp — native `/completion`
   first, OpenAI `/v1/chat/completions` tool-calling as fallback.
4. **Parse** the function call (`ModelClient::ParseFunctionCall`) — tolerant of
   FunctionGemma-native, JSON `tool_calls`, and free-form `move_paddle(...)`.
5. **Move** — the paddle interpolates toward `target_y` at a fixed max speed
   (`kPaddleSpeed`); it never teleports. The render/game thread only ever reads
   the most recent decision snapshot.

## On-screen HUD (drawn inside the window)

The overlay (`hud.cpp`) updates live with:

- **FPS (render)** — the game loop rate, separate from inference, so you can see
  whether the model or the render loop is the bottleneck
- **Inference latency** — last call ms, split into TTFT + decode (from
  llama.cpp `timings`); turns red past 200 ms
- **Avg latency (last 10)** — rolling average
- **Decode tokens/sec** for the last call
- **Cadence** and **decisions this rally** (+ total)
- **Function call** the model returned this cycle, e.g.
  `move_paddle(direction=up, target_y=214)`
- **Prompt state** — the exact state line sent this cycle
- **Inference busy/idle** + current `target_y`/`direction`
- Any model/connection **error**

A small tick on the right margin marks the model's current `target_y`.

---

## Optional: fine-tuning

If base FunctionGemma's zero-shot calls aren't reliable enough, generate a tiny
synthetic dataset (pure C++) and LoRA-tune with llama.cpp — no Python training
stack. See **`finetune/README.md`**.

## Tuning notes

- Physics/geometry constants live at the top of `src/game.hpp`
  (`kScreenW/H`, `kPaddleSpeed`, `kBallSpeed*`, …).
- If you change the tool schema or prompt in `model_client.cpp`, keep
  `BuildPrompt()` in `finetune/gen_dataset.cpp` in sync (it's intentionally a
  byte-for-byte copy) and regenerate the dataset.
