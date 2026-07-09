#pragma once

#include <string>

// Logical playfield size (pixels).
constexpr int kScreenW = 800;
constexpr int kScreenH = 600;
constexpr int kPaddleW = 12;
constexpr int kPaddleH = 80;
constexpr int kBallSize = 10;
constexpr float kPaddleSpeed = 380.f;   // px/s for both paddles (player + AI max)
constexpr float kBallSpeed0 = 280.f;    // initial ball speed
constexpr float kBallSpeedInc = 18.f;   // per paddle hit
constexpr float kBallSpeedMax = 620.f;

struct GameState {
  float ball_x = 0.f;
  float ball_y = 0.f;
  float ball_vx = 0.f;
  float ball_vy = 0.f;
  float player_y = 0.f;   // top of left paddle
  float ai_y = 0.f;       // top of right paddle
  int player_score = 0;
  int ai_score = 0;
  int rally_hits = 0;
  bool ball_in_play = false;
};

// Compact state string fed to FunctionGemma (exactly the fields in the brief).
std::string SerializeStateForModel(const GameState& g);

// Debug-only trajectory oracle: predicts the ball-center Y at which the ball
// will reach the AI (right) paddle face, folding in top/bottom wall bounces.
// Returns a recover-to-center value when the ball is moving away from the AI.
// This is NOT used to steer the AI paddle (the model does that via move_paddle)
// -- it exists purely for the debug overlay so you can compare the model's
// requested target_y against where the ball is truly headed.
float PredictBallLandingY(const GameState& g);

// --- Offense: aim the rebound -------------------------------------------------
// The AI can steer its return shot by where on the paddle it contacts the ball
// (see Game::BouncePaddle). These helpers do the EXACT inverse math so the model
// never has to: it only chooses a *strategy* (which tool). All operate on the
// current state's incoming ball.

// Where the ball would arrive on the PLAYER (left) side if the AI paddle center
// were placed at ai_center_y for the current incoming ball. Folds wall bounces.
// Meaningful only while the ball is heading toward the AI.
float PredictReturnY(const GameState& g, float ai_center_y);

// Inverse of the above: the AI paddle center that makes the rebound arrive at
// (approximately) desired_return_y on the player's side. Clamped so the paddle
// still actually contacts the incoming ball and stays on screen.
float AimPaddleForReturn(const GameState& g, float desired_return_y);

class Game {
 public:
  Game();

  void ResetRally(bool serve_to_ai);
  void Update(float dt, float player_dy, float ai_target_y, bool ai_has_target);
  const GameState& State() const { return state_; }
  GameState& State() { return state_; }

  // True when a point was scored this frame (left or right exit).
  bool PointScoredThisFrame() const { return point_scored_; }
  bool LastPointByPlayer() const { return last_point_by_player_; }

 private:
  void ClampPaddles();
  void BouncePaddle(bool left_paddle);

  GameState state_;
  bool point_scored_ = false;
  bool last_point_by_player_ = false;
};
