#include "ai_agent.hpp"
#include "game.hpp"
#include "hud.hpp"

#include <SDL.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

struct Config {
  std::string model_url = "http://127.0.0.1:8080";
  int cadence_ms = 100;
  bool mouse_control = false;
};

void PrintUsage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " [options]\n"
      << "  --url URL          llama.cpp server base URL (default http://127.0.0.1:8080)\n"
      << "  --cadence MS       AI decision interval in ms (default 100; lower = harder)\n"
      << "  --mouse            control player paddle with mouse Y\n"
      << "  -h, --help         show this help\n\n"
      << "Start FunctionGemma with llama-server first (see README).\n";
}

Config ParseArgs(int argc, char** argv) {
  Config c;
  for (int i = 1; i < argc; ++i) {
    if ((std::strcmp(argv[i], "--url") == 0 || std::strcmp(argv[i], "-u") == 0) &&
        i + 1 < argc) {
      c.model_url = argv[++i];
    } else if ((std::strcmp(argv[i], "--cadence") == 0 || std::strcmp(argv[i], "-c") == 0) &&
               i + 1 < argc) {
      c.cadence_ms = std::atoi(argv[++i]);
      if (c.cadence_ms < 20) c.cadence_ms = 20;
    } else if (std::strcmp(argv[i], "--mouse") == 0) {
      c.mouse_control = true;
    } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      PrintUsage(argv[0]);
      std::exit(0);
    } else {
      std::cerr << "Unknown arg: " << argv[i] << "\n";
      PrintUsage(argv[0]);
      std::exit(1);
    }
  }
  return c;
}

double NowMs() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch())
      .count();
}

void DrawRect(SDL_Renderer* r, int x, int y, int w, int h, Uint8 R, Uint8 G, Uint8 B) {
  SDL_SetRenderDrawColor(r, R, G, B, 255);
  SDL_Rect rect{x, y, w, h};
  SDL_RenderFillRect(r, &rect);
}

}  // namespace

int main(int argc, char** argv) {
  const Config cfg = ParseArgs(argc, argv);
  std::srand(static_cast<unsigned>(std::time(nullptr)));

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
    return 1;
  }

  SDL_Window* window = SDL_CreateWindow(
      "FunctionGemma Pong",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      kScreenW, kScreenH,
      SDL_WINDOW_SHOWN);
  if (!window) {
    std::cerr << "SDL_CreateWindow: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  }
  if (!renderer) {
    std::cerr << "SDL_CreateRenderer: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  HUD hud;
  hud.Init(renderer);

  Game game;
  AIAgent ai(cfg.model_url, cfg.cadence_ms);

  std::cout << "FunctionGemma Pong\n"
            << "  Model server: " << cfg.model_url << "\n"
            << "  Decision cadence: " << cfg.cadence_ms << " ms\n"
            << "  Controls: W/S or arrows (or --mouse), [ / ] cadence, R reset, Esc quit\n";

  bool running = true;
  bool keys[SDL_NUM_SCANCODES] = {};
  Uint64 prev = SDL_GetPerformanceCounter();
  float fps = 60.f;
  float fps_accum = 0.f;
  int fps_frames = 0;

  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      if (e.type == SDL_KEYDOWN) {
        keys[e.key.keysym.scancode] = true;
        if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) running = false;
        if (e.key.keysym.scancode == SDL_SCANCODE_R) {
          game.State().player_score = 0;
          game.State().ai_score = 0;
          game.ResetRally(true);
          ai.OnRallyReset();
        }
        if (e.key.keysym.scancode == SDL_SCANCODE_LEFTBRACKET) {
          ai.SetCadenceMs(ai.CadenceMs() + 25);
          std::cout << "Cadence -> " << ai.CadenceMs() << " ms (easier)\n";
        }
        if (e.key.keysym.scancode == SDL_SCANCODE_RIGHTBRACKET) {
          ai.SetCadenceMs(ai.CadenceMs() - 25);
          std::cout << "Cadence -> " << ai.CadenceMs() << " ms (harder)\n";
        }
        if (e.key.keysym.scancode == SDL_SCANCODE_T) {
          ai.SetUseModelPick(!ai.UseModelPick());
          std::cout << "Offense driver -> "
                    << (ai.UseModelPick() ? "MODEL pick (watch it reason)"
                                          : "ideal (competitive)")
                    << "\n";
        }
      }
      if (e.type == SDL_KEYUP) {
        keys[e.key.keysym.scancode] = false;
      }
    }

    const Uint64 now = SDL_GetPerformanceCounter();
    const float dt = static_cast<float>(now - prev) / static_cast<float>(SDL_GetPerformanceFrequency());
    prev = now;
    const float clamped_dt = std::min(dt, 0.05f);

    fps_accum += dt;
    fps_frames += 1;
    if (fps_accum >= 0.5f) {
      fps = static_cast<float>(fps_frames) / fps_accum;
      fps_accum = 0.f;
      fps_frames = 0;
    }

    // Player input.
    float player_dy = 0.f;
    if (cfg.mouse_control) {
      int mx = 0, my = 0;
      SDL_GetMouseState(&mx, &my);
      const float target = static_cast<float>(my) - kPaddleH / 2.f;
      const float err = target - game.State().player_y;
      if (std::fabs(err) > 1.f) {
        player_dy = (err > 0.f ? 1.f : -1.f) * kPaddleSpeed;
        if (std::fabs(err) < kPaddleSpeed * clamped_dt) {
          player_dy = err / clamped_dt;
        }
      }
    } else {
      if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) player_dy -= kPaddleSpeed;
      if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) player_dy += kPaddleSpeed;
    }

    // AI decision request (async; does not block).
    ai.Tick(game.State(), NowMs());
    const AIDecision decision = ai.Snapshot();

    // Physics: AI paddle only moves toward model's last target_y.
    game.Update(clamped_dt, player_dy, decision.target_y, decision.valid);
    if (game.PointScoredThisFrame()) {
      ai.OnRallyReset();
    }

    // --- Render ---
    SDL_SetRenderDrawColor(renderer, 12, 14, 22, 255);
    SDL_RenderClear(renderer);

    // Center dashed line.
    for (int y = 0; y < kScreenH; y += 20) {
      DrawRect(renderer, kScreenW / 2 - 1, y, 2, 10, 40, 50, 70);
    }

    // Paddles + ball.
    DrawRect(renderer, 0, static_cast<int>(game.State().player_y), kPaddleW, kPaddleH, 90, 200, 255);
    DrawRect(renderer, kScreenW - kPaddleW, static_cast<int>(game.State().ai_y), kPaddleW, kPaddleH,
             255, 120, 100);
    DrawRect(renderer, static_cast<int>(game.State().ball_x), static_cast<int>(game.State().ball_y),
             kBallSize, kBallSize, 255, 255, 255);

    // --- Debug overlay (both markers describe the AI/right side) ------------
    // GREEN full-width line = where the ball will ACTUALLY cross the AI paddle
    //   face, computed independently of the model (ground truth about the ball).
    // ORANGE hollow paddle  = where the MODEL wants its paddle (target_y). The
    //   solid AI paddle converges onto this outline. The model is correct only
    //   when the orange outline sits on the green line; the gap is the model's
    //   trajectory error (also shown as `err` in the HUD).
    const bool ball_incoming =
        game.State().ball_in_play && game.State().ball_vx > 0.f;
    const float landing_y = PredictBallLandingY(game.State());

    if (ball_incoming) {
      const int ly = static_cast<int>(landing_y);
      DrawRect(renderer, 0, ly, kScreenW, 2, 90, 255, 150);            // ground-truth landing
      DrawRect(renderer, kScreenW - kPaddleW, ly - 1, kPaddleW, 4, 90, 255, 150);  // at paddle face
    }
    if (decision.valid) {
      const int ty = static_cast<int>(decision.target_y);
      // Hollow paddle outline on the AI (right) side: where the paddle is headed.
      SDL_SetRenderDrawColor(renderer, 255, 200, 80, 255);
      SDL_Rect want{kScreenW - kPaddleW, ty - kPaddleH / 2, kPaddleW, kPaddleH};
      SDL_RenderDrawRect(renderer, &want);
    }
    // Magenta marker on the PLAYER (left) side: where the AI is aiming its return
    // (the tool's target). Watch it sit far from the blue player paddle.
    if (decision.offense && decision.desired_return_y >= 0.f) {
      const int ry = static_cast<int>(decision.desired_return_y);
      DrawRect(renderer, 0, ry, kScreenW / 2, 1, 110, 40, 110);      // faint guide
      DrawRect(renderer, 0, ry - 3, kPaddleW, 6, 235, 90, 235);      // aim marker at player face
    }

    hud.Draw(renderer, decision, fps, game.State().player_score,
             game.State().ai_score, landing_y, ball_incoming);

    SDL_RenderPresent(renderer);
  }

  hud.Shutdown();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
