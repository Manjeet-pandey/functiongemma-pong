#pragma once

#include <string>

// Result of one FunctionGemma inference call.
struct InferenceResult {
  bool ok = false;
  std::string error;

  // Parsed tool args (empty/0 if parse failed).
  std::string direction;  // "up" | "down" | "stay"
  int target_y = -1;

  // Debug / HUD fields.
  std::string prompt_sent;
  std::string raw_output;       // full model text
  std::string raw_function_call; // e.g. move_paddle(direction=up, target_y=214)
  double latency_ms = 0.0;      // wall clock end-to-end for this call
  double ttft_ms = 0.0;         // time-to-first-token (from server timings if present)
  double decode_ms = 0.0;       // predicted / decode portion if available
  double tokens_per_sec = 0.0;
  int completion_tokens = 0;
};

// Talks to a local llama.cpp server (OpenAI-compatible or /completion).
// Builds FunctionGemma-native prompts and parses function-call output.
class ModelClient {
 public:
  // base_url e.g. "http://127.0.0.1:8080"
  explicit ModelClient(std::string base_url);

  // Synchronous: builds prompt from state_line, runs inference, parses move_paddle.
  InferenceResult Decide(const std::string& state_line);

  void SetBaseUrl(std::string url) { base_url_ = std::move(url); }
  const std::string& BaseUrl() const { return base_url_; }

 private:
  std::string BuildPrompt(const std::string& state_line) const;
  InferenceResult PostCompletion(const std::string& prompt) const;
  InferenceResult PostChat(const std::string& state_line) const;
  static void ParseFunctionCall(const std::string& text, InferenceResult& out);

  std::string base_url_;
};
