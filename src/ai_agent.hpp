#pragma once

#include "game.hpp"
#include "model_client.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

// Latest decision shared with the render/game thread.
struct AIDecision {
  bool valid = false;
  float target_y = kScreenH / 2.f;  // paddle *center* Y
  std::string direction = "stay";

  // Debug HUD snapshot (last completed call).
  std::string raw_function_call = "(none yet)";
  std::string prompt_state = "";  // short state line, not full chat template
  std::string error = "";
  double latency_ms = 0.0;
  double ttft_ms = 0.0;
  double decode_ms = 0.0;
  double tokens_per_sec = 0.0;
  double latency_avg10_ms = 0.0;
  int decision_count_rally = 0;
  int decision_count_total = 0;
  int cadence_ms = 100;
  bool inference_busy = false;
  bool model_connected = false;
};

// Async FunctionGemma decision loop.
// - Game thread calls Tick() every frame with current state.
// - Worker thread runs inference; never blocks the 60fps loop.
// - Paddle keeps moving toward last known target_y when inference is slow.
class AIAgent {
 public:
  explicit AIAgent(std::string model_url, int cadence_ms = 100);
  ~AIAgent();

  AIAgent(const AIAgent&) = delete;
  AIAgent& operator=(const AIAgent&) = delete;

  // Non-blocking: may enqueue a new inference if cadence elapsed and not busy.
  void Tick(const GameState& state, double now_ms);

  // Call when a rally/point ends to reset per-rally counters.
  void OnRallyReset();

  AIDecision Snapshot() const;

  void SetCadenceMs(int ms);
  int CadenceMs() const { return cadence_ms_.load(); }

 private:
  void WorkerLoop();
  void RecordLatency(double ms);

  ModelClient client_;
  std::atomic<int> cadence_ms_{100};
  std::atomic<bool> stop_{false};

  mutable std::mutex mu_;
  std::condition_variable cv_;
  bool work_pending_ = false;
  std::string pending_state_line_;
  AIDecision latest_;
  double last_request_ms_ = -1e9;
  int decisions_rally_ = 0;
  int decisions_total_ = 0;

  std::array<double, 10> latency_ring_{};
  int latency_count_ = 0;
  int latency_idx_ = 0;

  std::thread worker_;
};
