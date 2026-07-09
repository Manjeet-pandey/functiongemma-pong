#include "hud.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

const char* kFontCandidates[] = {
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/System/Library/Fonts/Supplemental/Courier New.ttf",
    "/Library/Fonts/Arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "assets/DejaVuSansMono.ttf",
    nullptr,
};

std::string Truncate(const std::string& s, size_t max) {
  if (s.size() <= max) return s;
  return s.substr(0, max - 3) + "...";
}

}  // namespace

HUD::HUD() = default;

HUD::~HUD() { Shutdown(); }

bool HUD::Init(SDL_Renderer* /*renderer*/) {
  if (TTF_Init() != 0) {
    std::fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
    return false;
  }
  for (int i = 0; kFontCandidates[i]; ++i) {
    font_ = TTF_OpenFont(kFontCandidates[i], 14);
    font_small_ = TTF_OpenFont(kFontCandidates[i], 12);
    if (font_ && font_small_) {
      ttf_ok_ = true;
      std::fprintf(stderr, "HUD font: %s\n", kFontCandidates[i]);
      return true;
    }
    if (font_) {
      TTF_CloseFont(font_);
      font_ = nullptr;
    }
    if (font_small_) {
      TTF_CloseFont(font_small_);
      font_small_ = nullptr;
    }
  }
  std::fprintf(stderr, "HUD: no TTF font found; overlay text disabled\n");
  return false;
}

void HUD::Shutdown() {
  if (font_) {
    TTF_CloseFont(font_);
    font_ = nullptr;
  }
  if (font_small_) {
    TTF_CloseFont(font_small_);
    font_small_ = nullptr;
  }
  if (ttf_ok_) {
    TTF_Quit();
    ttf_ok_ = false;
  }
}

void HUD::DrawLine(SDL_Renderer* renderer, int x, int y, const std::string& text,
                   SDL_Color color) {
  if (!font_small_ || text.empty()) return;
  SDL_Surface* surf = TTF_RenderUTF8_Blended(font_small_, text.c_str(), color);
  if (!surf) return;
  SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
  if (tex) {
    SDL_Rect dst{x, y, surf->w, surf->h};
    // Dim background bar for readability.
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
    SDL_Rect bg{x - 2, y - 1, surf->w + 4, surf->h + 2};
    SDL_RenderFillRect(renderer, &bg);
    SDL_RenderCopy(renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
  }
  SDL_FreeSurface(surf);
}

void HUD::Draw(SDL_Renderer* renderer, const AIDecision& ai, float fps,
               int player_score, int ai_score, float predicted_landing_y,
               bool ball_incoming) {
  // Scoreboard (always, even without font — use big rects + optional text).
  char scorebuf[64];
  std::snprintf(scorebuf, sizeof(scorebuf), "%d  :  %d", player_score, ai_score);
  if (font_) {
    SDL_Color white{240, 240, 240, 255};
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font_, scorebuf, white);
    if (surf) {
      SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
      if (tex) {
        SDL_Rect dst{kScreenW / 2 - surf->w / 2, 8, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
      }
      SDL_FreeSurface(surf);
    }
  }

  if (!font_small_) return;

  const SDL_Color green{80, 255, 140, 255};
  const SDL_Color yellow{255, 230, 80, 255};
  const SDL_Color cyan{120, 220, 255, 255};
  const SDL_Color red{255, 100, 100, 255};
  const SDL_Color gray{200, 200, 200, 255};

  int y = 36;
  const int x = 8;
  const int step = 15;

  char line[512];

  std::snprintf(line, sizeof(line), "FPS (render): %.1f", static_cast<double>(fps));
  DrawLine(renderer, x, y, line, green);
  y += step;

  std::snprintf(line, sizeof(line),
                "Inference latency: %.1f ms  (TTFT %.1f + decode %.1f)",
                ai.latency_ms, ai.ttft_ms, ai.decode_ms);
  DrawLine(renderer, x, y, line, ai.latency_ms > 200.0 ? red : yellow);
  y += step;

  std::snprintf(line, sizeof(line), "Avg latency (last 10): %.1f ms",
                ai.latency_avg10_ms);
  DrawLine(renderer, x, y, line, yellow);
  y += step;

  std::snprintf(line, sizeof(line), "Decode tokens/sec: %.1f", ai.tokens_per_sec);
  DrawLine(renderer, x, y, line, cyan);
  y += step;

  std::snprintf(line, sizeof(line),
                "Cadence: %d ms  |  decisions this rally: %d  (total %d)",
                ai.cadence_ms, ai.decision_count_rally, ai.decision_count_total);
  DrawLine(renderer, x, y, line, gray);
  y += step;

  std::snprintf(line, sizeof(line), "Inference: %s  |  paddle target_y=%.0f",
                ai.inference_busy ? "BUSY" : "idle",
                static_cast<double>(ai.target_y));
  DrawLine(renderer, x, y, line, ai.inference_busy ? yellow : green);
  y += step;

  // Offense: which strategy the model picked vs. the ideal, and where it aims.
  if (ai.offense) {
    const bool matched = (ai.chosen_tool == ai.ideal_tool);
    std::snprintf(line, sizeof(line),
                  "model picked: %s | ideal: %s | driving: %s",
                  ai.chosen_tool.c_str(), ai.ideal_tool.c_str(),
                  ai.model_driving ? "MODEL" : "IDEAL");
    DrawLine(renderer, x, y, line, matched ? green : yellow);
    y += step;
    std::snprintf(line, sizeof(line),
                  "AI hits ball at y=%.0f -> aims return to player-side y=%.0f  (player at y=%.0f)",
                  static_cast<double>(predicted_landing_y),
                  static_cast<double>(ai.desired_return_y),
                  static_cast<double>(ai.player_center));
    DrawLine(renderer, x, y, line, cyan);
  } else {
    std::snprintf(line, sizeof(line),
                  "ball outgoing  |  %s -> recover to center y=%.0f",
                  ai.chosen_tool.c_str(), static_cast<double>(ai.target_y));
    DrawLine(renderer, x, y, line, gray);
  }
  y += step;

  DrawLine(renderer, x, y,
           "Function call: " + Truncate(ai.raw_function_call, 90), cyan);
  y += step;

  DrawLine(renderer, x, y, "Prompt state: " + Truncate(ai.prompt_state, 95), gray);
  y += step;

  if (!ai.error.empty()) {
    DrawLine(renderer, x, y, "Error: " + Truncate(ai.error, 90), red);
  } else if (!ai.model_connected && ai.decision_count_total == 0) {
    DrawLine(renderer, x, y,
             "Waiting for model at llama-server (see README)...", red);
  }

  // Footer controls
  DrawLine(renderer, x, kScreenH - 18,
           "W/S: move | [ / ]: cadence | T: toggle model/ideal offense | R: reset | Esc: quit",
           gray);
}
