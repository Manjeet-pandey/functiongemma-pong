#include "ai_agent.hpp"

#include <algorithm>
#include <numeric>

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

  // Only decide when ball is moving toward the AI (or mid-court) to save cycles,
  // but still request on cadence so model stays warm — always request.
  if (work_pending_) {
    return;  // still working; keep last target (graceful degradation)
  }
  if (now_ms - last_request_ms_ < static_cast<double>(cadence)) {
    return;
  }

  pending_state_line_ = SerializeStateForModel(state);
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
    std::string state_line;
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait(lock, [this] { return work_pending_ || stop_.load(); });
      if (stop_.load()) break;
      state_line = pending_state_line_;
    }

    InferenceResult r = client_.Decide(state_line);

    {
      std::lock_guard<std::mutex> lock(mu_);
      work_pending_ = false;
      latest_.inference_busy = false;
      latest_.prompt_state = state_line;
      latest_.latency_ms = r.latency_ms;
      latest_.ttft_ms = r.ttft_ms;
      latest_.decode_ms = r.decode_ms;
      latest_.tokens_per_sec = r.tokens_per_sec;
      latest_.error = r.error;
      latest_.model_connected = r.error.empty() || r.ok;

      if (!r.raw_function_call.empty()) {
        latest_.raw_function_call = r.raw_function_call;
      } else if (!r.error.empty()) {
        latest_.raw_function_call = "(error) " + r.error.substr(0, 80);
      }

      if (r.ok && r.target_y >= 0) {
        // Clamp target to valid paddle-center range.
        float ty = static_cast<float>(r.target_y);
        ty = std::clamp(ty, kPaddleH / 2.f, static_cast<float>(kScreenH) - kPaddleH / 2.f);
        latest_.target_y = ty;
        latest_.direction = r.direction.empty() ? "stay" : r.direction;
        latest_.valid = true;
        decisions_rally_ += 1;
        decisions_total_ += 1;
        latest_.decision_count_rally = decisions_rally_;
        latest_.decision_count_total = decisions_total_;
      }

      if (r.latency_ms > 0.0) {
        RecordLatency(r.latency_ms);
      }
    }
  }
}
