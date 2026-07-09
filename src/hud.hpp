#pragma once

#include "ai_agent.hpp"

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>

// On-screen debug overlay drawn inside the game window.
class HUD {
 public:
  HUD();
  ~HUD();

  bool Init(SDL_Renderer* renderer);
  void Shutdown();

  void Draw(SDL_Renderer* renderer,
            const AIDecision& ai,
            float fps,
            int player_score,
            int ai_score);

 private:
  void DrawLine(SDL_Renderer* renderer, int x, int y, const std::string& text,
                SDL_Color color);

  TTF_Font* font_ = nullptr;
  TTF_Font* font_small_ = nullptr;
  bool ttf_ok_ = false;
};
