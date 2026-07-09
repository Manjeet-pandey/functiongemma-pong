#pragma once

#include <string>
#include <utility>
#include <vector>

// Result of one FunctionGemma inference call.
struct InferenceResult {
  bool ok = false;
  std::string error;

  // Parsed tool args (empty/0 if parse failed).
  std::string direction;  // "up" | "down" | "stay"
  int target_y = -1;
  std::string tool;       // selected function name (offense tool selection)

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

  // Synchronous: the game computes computed_target_y (the ball's intercept Y via
  // the trajectory oracle) and hands it to the model in a distraction-free prompt;
  // the model's job is only to emit move_paddle(target_y = that value). state_line
  // is the full physics state, kept for the debug HUD only. Runs inference, parses.
  InferenceResult Decide(const std::string& state_line, int computed_target_y);

  // Offense: given a menu of {tool_name, description} and a one-line situation
  // (the player's position), have the model pick ONE tool. The game maps the
  // chosen tool to an exact paddle placement -- the model only reasons about
  // which strategy, never the math. Returns the picked name in result.tool.
  InferenceResult ChooseTool(
      const std::vector<std::pair<std::string, std::string>>& tools,
      const std::string& situation) const;

  void SetBaseUrl(std::string url) { base_url_ = std::move(url); }
  const std::string& BaseUrl() const { return base_url_; }

 private:
  std::string BuildPrompt(int computed_target_y) const;
  // parse_prefix is the assistant-turn text we prefilled into the prompt (the
  // call skeleton); it is prepended to the model's continuation before parsing.
  InferenceResult PostCompletion(const std::string& prompt,
                                 const std::string& parse_prefix) const;
  InferenceResult PostChat(int computed_target_y) const;
  static void ParseFunctionCall(const std::string& text, InferenceResult& out);

  std::string base_url_;
};
