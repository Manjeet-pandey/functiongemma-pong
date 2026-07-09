#include "game.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

std::string SerializeStateForModel(const GameState& g) {
  // AI paddle is on the right; distance is ball → right paddle face.
  const float paddle_face_x = static_cast<float>(kScreenW - kPaddleW);
  const float distance = paddle_face_x - g.ball_x;
  std::ostringstream oss;
  oss << "ball_x=" << static_cast<int>(g.ball_x)
      << " ball_y=" << static_cast<int>(g.ball_y)
      << " ball_vx=" << static_cast<int>(g.ball_vx)
      << " ball_vy=" << static_cast<int>(g.ball_vy)
      << " paddle_y=" << static_cast<int>(g.ai_y)
      << " paddle_height=" << kPaddleH
      << " screen_height=" << kScreenH
      << " distance_to_paddle=" << static_cast<int>(distance);
  return oss.str();
}

Game::Game() {
  state_.player_y = (kScreenH - kPaddleH) / 2.f;
  state_.ai_y = (kScreenH - kPaddleH) / 2.f;
  ResetRally(true);
}

void Game::ResetRally(bool serve_to_ai) {
  state_.ball_x = kScreenW / 2.f;
  state_.ball_y = kScreenH / 2.f;
  state_.rally_hits = 0;
  state_.ball_in_play = true;
  point_scored_ = false;

  const float angle = (static_cast<float>(std::rand() % 60) - 30.f) * static_cast<float>(M_PI) / 180.f;
  const float dir = serve_to_ai ? 1.f : -1.f;
  state_.ball_vx = dir * kBallSpeed0 * std::cos(angle);
  state_.ball_vy = kBallSpeed0 * std::sin(angle);
}

void Game::ClampPaddles() {
  state_.player_y = std::clamp(state_.player_y, 0.f, static_cast<float>(kScreenH - kPaddleH));
  state_.ai_y = std::clamp(state_.ai_y, 0.f, static_cast<float>(kScreenH - kPaddleH));
}

void Game::BouncePaddle(bool left_paddle) {
  // Hit offset relative to paddle center in [-1, 1] → reflection angle.
  const float paddle_y = left_paddle ? state_.player_y : state_.ai_y;
  const float paddle_cx = paddle_y + kPaddleH / 2.f;
  const float offset = (state_.ball_y - paddle_cx) / (kPaddleH / 2.f);
  const float clamped = std::clamp(offset, -1.f, 1.f);
  const float max_angle = 55.f * static_cast<float>(M_PI) / 180.f;
  const float angle = clamped * max_angle;

  state_.rally_hits += 1;
  float speed = kBallSpeed0 + state_.rally_hits * kBallSpeedInc;
  speed = std::min(speed, kBallSpeedMax);

  const float dir = left_paddle ? 1.f : -1.f;
  state_.ball_vx = dir * speed * std::cos(angle);
  state_.ball_vy = speed * std::sin(angle);

  // Nudge ball out of paddle to avoid sticky collisions.
  if (left_paddle) {
    state_.ball_x = static_cast<float>(kPaddleW + kBallSize);
  } else {
    state_.ball_x = static_cast<float>(kScreenW - kPaddleW - kBallSize);
  }
}

void Game::Update(float dt, float player_dy, float ai_target_y, bool ai_has_target) {
  point_scored_ = false;

  // Player: direct velocity from input.
  state_.player_y += player_dy * dt;

  // AI: interpolate toward last model target at fixed max speed (never teleport).
  if (ai_has_target) {
    const float ai_center = state_.ai_y + kPaddleH / 2.f;
    const float err = ai_target_y - ai_center;
    if (std::fabs(err) > 1.f) {
      const float step = kPaddleSpeed * dt;
      if (std::fabs(err) <= step) {
        state_.ai_y = ai_target_y - kPaddleH / 2.f;
      } else {
        state_.ai_y += (err > 0.f ? step : -step);
      }
    }
  }
  ClampPaddles();

  if (!state_.ball_in_play) {
    return;
  }

  state_.ball_x += state_.ball_vx * dt;
  state_.ball_y += state_.ball_vy * dt;

  // Top/bottom walls.
  if (state_.ball_y < 0.f) {
    state_.ball_y = 0.f;
    state_.ball_vy = std::fabs(state_.ball_vy);
  } else if (state_.ball_y + kBallSize > kScreenH) {
    state_.ball_y = static_cast<float>(kScreenH - kBallSize);
    state_.ball_vy = -std::fabs(state_.ball_vy);
  }

  // Left paddle collision.
  if (state_.ball_vx < 0.f &&
      state_.ball_x <= kPaddleW &&
      state_.ball_x + kBallSize >= 0 &&
      state_.ball_y + kBallSize >= state_.player_y &&
      state_.ball_y <= state_.player_y + kPaddleH) {
    BouncePaddle(true);
  }

  // Right (AI) paddle collision.
  if (state_.ball_vx > 0.f &&
      state_.ball_x + kBallSize >= kScreenW - kPaddleW &&
      state_.ball_x <= kScreenW &&
      state_.ball_y + kBallSize >= state_.ai_y &&
      state_.ball_y <= state_.ai_y + kPaddleH) {
    BouncePaddle(false);
  }

  // Scoring.
  if (state_.ball_x + kBallSize < 0.f) {
    state_.ai_score += 1;
    state_.ball_in_play = false;
    point_scored_ = true;
    last_point_by_player_ = false;
    ResetRally(true);
  } else if (state_.ball_x > kScreenW) {
    state_.player_score += 1;
    state_.ball_in_play = false;
    point_scored_ = true;
    last_point_by_player_ = true;
    ResetRally(false);
  }
}
