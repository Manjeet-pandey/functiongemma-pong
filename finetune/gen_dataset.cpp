// Synthetic dataset generator for fine-tuning FunctionGemma on Pong defense.
//
// Emits (game_state -> correct move_paddle call) pairs as JSONL. It reuses the
// game's real constants (game.hpp) and the *exact* SerializeStateForModel()
// used at runtime, so the training prompts are byte-identical to what the game
// sends live -- no train/inference skew.
//
// The "correct" label comes from an analytic oracle: forward-simulate the ball
// (with wall bounces) to the AI paddle face and aim the paddle center there.
// This oracle exists ONLY to make training labels; the game itself never uses
// it -- the live AI paddle is driven purely by the model's function call.
//
// Build (see CMakeLists.txt target `gen_dataset`):
//   cmake --build build --target gen_dataset
// Run:
//   ./build/gen_dataset 4000 finetune/data/train.jsonl
//   ./build/gen_dataset 400  finetune/data/val.jsonl 123   # different seed
//
#include "game.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

namespace {

// Fold an unbounded y into the reflecting range [0, span] (triangle wave).
float FoldReflect(float y, float span) {
  if (span <= 0.f) return 0.f;
  const float period = 2.f * span;
  float m = std::fmod(y, period);
  if (m < 0.f) m += period;
  if (m > span) m = period - m;
  return m;
}

// Oracle: desired paddle-center Y for this state.
float OracleTargetY(const GameState& g) {
  const float paddle_face_x = static_cast<float>(kScreenW - kPaddleW);
  const float span = static_cast<float>(kScreenH - kBallSize);

  float predicted_ball_y;
  if (g.ball_vx > 1.f && g.ball_x < paddle_face_x) {
    const float t = (paddle_face_x - g.ball_x) / g.ball_vx;
    const float y_unbounded = g.ball_y + g.ball_vy * t;
    predicted_ball_y = FoldReflect(y_unbounded, span);
  } else {
    // Ball moving away (or already at the face): recover toward center.
    predicted_ball_y = (kScreenH - kBallSize) / 2.f;
  }

  float target = predicted_ball_y + kBallSize / 2.f;  // ball center
  const float lo = kPaddleH / 2.f;
  const float hi = static_cast<float>(kScreenH) - kPaddleH / 2.f;
  if (target < lo) target = lo;
  if (target > hi) target = hi;
  return target;
}

const char* OracleDirection(const GameState& g, float target_y) {
  const float paddle_center = g.ai_y + kPaddleH / 2.f;
  const float err = target_y - paddle_center;
  const float dead = 6.f;  // px deadband -> "stay"
  if (err < -dead) return "up";
  if (err > dead) return "down";
  return "stay";
}

std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  return out;
}

// The FunctionGemma prompt template -- kept byte-identical to
// ModelClient::BuildPrompt() in src/model_client.cpp.
std::string BuildPrompt(const std::string& state_line) {
  std::string p;
  p += "<start_of_turn>developer\n";
  p += "You can do function calling with the following functions:\n";
  p += "<start_function_declaration>";
  p += "declaration:move_paddle{";
  p += "description:<escape>Move the AI (right) paddle to intercept the ball. ";
  p += "target_y is the desired center Y of the paddle in pixels.<escape>,";
  p += "parameters:{";
  p += "properties:{";
  p += "direction:{type:<escape>STRING<escape>,description:<escape>up, down, or stay<escape>},";
  p += "target_y:{type:<escape>NUMBER<escape>,description:<escape>Paddle center Y, 0 to screen_height<escape>}";
  p += "},";
  p += "required:[direction,target_y]";
  p += "}";
  p += "}";
  p += "<end_function_declaration>\n";
  p += "<end_of_turn>\n";
  p += "<start_of_turn>user\n";
  p += "Pong defense. State: " + state_line + "\n";
  p += "Call move_paddle once to position the right paddle.\n";
  p += "<end_of_turn>\n";
  p += "<start_of_turn>model\n";
  return p;
}

std::string BuildCompletion(const std::string& direction, int target_y) {
  // Native FunctionGemma call form that ModelClient::ParseFunctionCall accepts.
  return "call:move_paddle{direction:<escape>" + direction +
         "<escape>,target_y:" + std::to_string(target_y) + "}<end_function_call>";
}

}  // namespace

int main(int argc, char** argv) {
  const int count = (argc > 1) ? std::atoi(argv[1]) : 4000;
  const std::string out_path = (argc > 2) ? argv[2] : "finetune/data/train.jsonl";
  const unsigned seed = (argc > 3) ? static_cast<unsigned>(std::atoi(argv[3])) : 42u;

  std::ofstream out(out_path);
  if (!out) {
    std::cerr << "Cannot open output: " << out_path
              << " (create the finetune/data directory first)\n";
    return 1;
  }

  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> ux(kBallSize, kScreenW - kBallSize);
  std::uniform_real_distribution<float> uy(0.f, kScreenH - kBallSize);
  std::uniform_real_distribution<float> uv(-1.f, 1.f);
  std::uniform_real_distribution<float> uspeed(kBallSpeed0, kBallSpeedMax);
  std::uniform_real_distribution<float> upaddle(0.f, kScreenH - kPaddleH);

  int written = 0;
  for (int i = 0; i < count; ++i) {
    GameState g;
    g.ball_x = ux(rng);
    g.ball_y = uy(rng);

    // Random velocity direction, biased so most samples move toward the AI
    // (that's when defense actually matters), some move away (recover-to-center).
    float angle = uv(rng) * 0.9f;  // radians off horizontal
    float dir = (rng() % 10 < 7) ? 1.f : -1.f;  // 70% toward AI (right)
    float speed = uspeed(rng);
    g.ball_vx = dir * speed * std::cos(angle);
    g.ball_vy = speed * std::sin(angle);

    g.ai_y = upaddle(rng);
    g.player_y = upaddle(rng);

    const std::string state = SerializeStateForModel(g);
    const float target_f = OracleTargetY(g);
    const int target_y = static_cast<int>(std::lround(target_f));
    const std::string direction = OracleDirection(g, target_f);

    const std::string prompt = BuildPrompt(state);
    const std::string completion = BuildCompletion(direction, target_y);

    // JSONL: prompt/completion for SFT, plus a concatenated `text` for raw-text
    // trainers, plus the bare `state` for inspection.
    out << "{\"state\":\"" << JsonEscape(state) << "\","
        << "\"prompt\":\"" << JsonEscape(prompt) << "\","
        << "\"completion\":\"" << JsonEscape(completion) << "\","
        << "\"text\":\"" << JsonEscape(prompt + completion) << "\"}\n";
    ++written;
  }

  std::cerr << "Wrote " << written << " examples to " << out_path
            << " (seed " << seed << ")\n";
  return 0;
}
