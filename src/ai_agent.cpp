#include "ai_agent.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <utility>

namespace {
// Offense tool menu. ORDER MATTERS: the smart "smash away" tool is listed first
// because tiny models have a strong first-option bias -- so even when the model
// isn't really reasoning, its default pick is a good move. The HUD shows the
// pick vs. the ideal so the actual reasoning quality stays visible.
const std::vector<std::pair<std::string, std::string>> kTools = {
    {"smash_away_from_player",
     "Return the ball to the side FARTHEST from the player's paddle so they cannot reach it."},
    {"bank_shot",
     "Hit the ball at a steep angle so it ricochets off a wall and reaches the player fast and hard to read."},
    {"return_top", "Return the ball to the TOP of the player's side (y near 0)."},
    {"return_bottom", "Return the ball to the BOTTOM of the player's side (y near 600)."},
    {"intercept", "Block the ball straight back without aiming."},
};
constexpr float kEdge = 60.f;  // how close to the wall the aiming tools target
}  // namespace

AIAgent::AIAgent(std::string model_url, int cadence_ms)
    : client_(std::move(model_url)) {
  cadence_ms_.store(std::max(20, cadence_ms));
  latest_.cadence_ms = cadence_ms_.load();
  latest_.target_y = kScreenH / 2.f;
  worker_ = std::thread([this] { WorkerLoop(); });
}

AIAgent::~AIAgent() {
  stop_.store(true);
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void AIAgent::SetCadenceMs(int ms) {
  cadence_ms_.store(std::max(20, ms));
  std::lock_guard<std::mutex> lock(mu_);
  latest_.cadence_ms = cadence_ms_.load();
}

void AIAgent::OnRallyReset() {
  std::lock_guard<std::mutex> lock(mu_);
  decisions_rally_ = 0;
  latest_.decision_count_rally = 0;
}

void AIAgent::RecordLatency(double ms) {
  latency_ring_[static_cast<size_t>(latency_idx_ % 10)] = ms;
  latency_idx_ += 1;
  latency_count_ = std::min(latency_count_ + 1, 10);
  const int n = latency_count_;
  double sum = 0.0;
  for (int i = 0; i < n; ++i) sum += latency_ring_[static_cast<size_t>(i)];
  latest_.latency_avg10_ms = n > 0 ? sum / n : 0.0;
}

void AIAgent::Tick(const GameState& state, double now_ms) {
  const int cadence = cadence_ms_.load();
  std::lock_guard<std::mutex> lock(mu_);
  latest_.cadence_ms = cadence;
  latest_.inference_busy = work_pending_;

  const float player_center = state.player_y + kPaddleH / 2.f;
  latest_.player_center = player_center;
  const bool incoming = state.ball_in_play && state.ball_vx > 0.f;

  // Ball moving away: aiming is impossible -> recover toward center right away,
  // no model call (this also keeps the CPU idle between exchanges).
  if (!incoming) {
    latest_.target_y = PredictBallLandingY(state);  // returns center when outgoing
    latest_.valid = true;
    latest_.offense = false;
    latest_.chosen_tool = "(recover)";
    latest_.desired_return_y = -1.f;
    return;
  }

  if (work_pending_) {
    return;  // still working; keep last target (graceful degradation)
  }
  if (now_ms - last_request_ms_ < static_cast<double>(cadence)) {
    return;
  }

  // Precompute the EXACT paddle placement for every tool (the math the model
  // never touches). The model will only pick a name; we look up the shot.
  const float far_side = (player_center < kScreenH / 2.f) ? (kScreenH - kEdge) : kEdge;
  const float land = PredictBallLandingY(state);
  const float impact_y = land - kBallSize / 2.f;
  // Bank shot: contact the ball at the paddle edge (max offset) toward the far
  // side -> steepest reflection (+/-55 deg) that ricochets off a wall.
  const float bank_dir = (player_center < kScreenH / 2.f) ? 1.f : -1.f;
  const float bank_c = impact_y - bank_dir * (kPaddleH / 2.f);
  pending_shots_ = {
      {"smash_away_from_player", AimPaddleForReturn(state, far_side), far_side},
      {"bank_shot", bank_c, PredictReturnY(state, bank_c)},
      {"return_top", AimPaddleForReturn(state, kEdge), kEdge},
      {"return_bottom", AimPaddleForReturn(state, kScreenH - kEdge), kScreenH - kEdge},
      {"intercept", land, PredictReturnY(state, land - kBallSize / 2.f)},
  };
  // Ideal = the aiming shot that's HARDEST to return (excludes intercept):
  // distance the player must travel PLUS a bonus for a steep arrival angle
  // (fast vertical motion = less reaction time). This lets the bank shot win
  // when its steepness beats a flatter but farther placement.
  pending_ideal_ = "smash_away_from_player";
  float best_hardness = -1.f;
  for (const auto& s : pending_shots_) {
    if (s.name == "intercept") continue;
    const float offset = std::clamp((impact_y - s.target_y) / (kPaddleH / 2.f), -1.f, 1.f);
    const float angle_deg = std::fabs(offset) * 55.f;
    const float hardness = std::fabs(s.desired_return_y - player_center) + 2.f * angle_deg;
    if (hardness > best_hardness) {
      best_hardness = hardness;
      pending_ideal_ = s.name;
    }
  }
  pending_player_center_ = player_center;
  pending_state_line_ = SerializeStateForModel(state);
  std::ostringstream ss;
  ss << "The ball is coming toward you. The player's paddle center is at y="
     << static_cast<int>(player_center) << " (field 0=top, 600=bottom).";
  pending_situation_ = ss.str();

  work_pending_ = true;
  last_request_ms_ = now_ms;
  latest_.inference_busy = true;
  cv_.notify_one();
}

AIDecision AIAgent::Snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return latest_;
}

void AIAgent::WorkerLoop() {
  while (!stop_.load()) {
    std::string situation, ideal;
    std::vector<ToolShot> shots;
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait(lock, [this] { return work_pending_ || stop_.load(); });
      if (stop_.load()) break;
      situation = pending_situation_;
      shots = pending_shots_;
      ideal = pending_ideal_;
    }

    // The model only picks a strategy name; the game already computed the math.
    InferenceResult r = client_.ChooseTool(kTools, situation);

    {
      std::lock_guard<std::mutex> lock(mu_);
      work_pending_ = false;
      latest_.inference_busy = false;
      latest_.prompt_state = situation;
      latest_.latency_ms = r.latency_ms;
      latest_.ttft_ms = r.ttft_ms;
      latest_.decode_ms = r.decode_ms;
      latest_.tokens_per_sec = r.tokens_per_sec;
      latest_.error = r.error;
      latest_.model_connected = r.error.empty() || r.ok;
      latest_.offense = true;
      latest_.ideal_tool = ideal;
      latest_.chosen_tool = r.tool.empty() ? "(unparsed)" : r.tool;

      // Resolve two shots: the model's pick (for display / optional driving) and
      // the deterministic ideal (competitive). Which one STEERS depends on the
      // toggle; the model is queried regardless so the HUD can show its choice.
      const ToolShot* model_shot = nullptr;
      const ToolShot* ideal_shot = nullptr;
      for (const auto& s : shots) {
        if (s.name == r.tool) model_shot = &s;
        if (s.name == ideal) ideal_shot = &s;
      }
      const bool drive_model = use_model_pick_.load() && (model_shot != nullptr);
      latest_.model_driving = drive_model;
      const ToolShot* drive = drive_model ? model_shot : ideal_shot;
      if (drive) {
        latest_.target_y =
            std::clamp(drive->target_y, kPaddleH / 2.f, static_cast<float>(kScreenH) - kPaddleH / 2.f);
        latest_.desired_return_y = drive->desired_return_y;
        latest_.valid = true;
      }
      latest_.raw_function_call =
          r.error.empty() ? r.raw_function_call : ("(error) " + r.error.substr(0, 80));

      decisions_rally_ += 1;
      decisions_total_ += 1;
      latest_.decision_count_rally = decisions_rally_;
      latest_.decision_count_total = decisions_total_;

      if (r.latency_ms > 0.0) {
        RecordLatency(r.latency_ms);
      }
    }
  }
}
