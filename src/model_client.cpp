#include "model_client.hpp"

#include <chrono>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>

using json = nlohmann::json;

namespace {

size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

std::string HttpPost(const std::string& url, const std::string& body, long& http_code) {
  std::string response;
  CURL* curl = curl_easy_init();
  if (!curl) {
    http_code = 0;
    return {};
  }
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
  const CURLcode rc = curl_easy_perform(curl);
  if (rc == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  } else {
    http_code = -static_cast<long>(rc);
    response = curl_easy_strerror(rc);
  }
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return response;
}

}  // namespace

ModelClient::ModelClient(std::string base_url) : base_url_(std::move(base_url)) {
  // Trim trailing slash.
  while (!base_url_.empty() && base_url_.back() == '/') {
    base_url_.pop_back();
  }
}

std::string ModelClient::BuildPrompt(const std::string& state_line) const {
  // FunctionGemma native control-token format (google/functiongemma-270m-it).
  std::ostringstream oss;
  oss << "<start_of_turn>developer\n"
      << "You can do function calling with the following functions:\n"
      << "<start_function_declaration>"
      << "declaration:move_paddle{"
      << "description:<escape>Move the AI (right) paddle to intercept the ball. "
      << "target_y is the desired center Y of the paddle in pixels.<escape>,"
      << "parameters:{"
      << "properties:{"
      << "direction:{type:<escape>STRING<escape>,description:<escape>up, down, or stay<escape>},"
      << "target_y:{type:<escape>NUMBER<escape>,description:<escape>Paddle center Y, 0 to screen_height<escape>}"
      << "},"
      << "required:[direction,target_y]"
      << "}"
      << "}"
      << "<end_function_declaration>\n"
      << "<end_of_turn>\n"
      << "<start_of_turn>user\n"
      << "Pong defense. State: " << state_line << "\n"
      << "Call move_paddle once to position the right paddle.\n"
      << "<end_of_turn>\n"
      << "<start_of_turn>model\n";
  return oss.str();
}

void ModelClient::ParseFunctionCall(const std::string& text, InferenceResult& out) {
  out.raw_output = text;

  // 1) FunctionGemma: call:move_paddle{direction:<escape>up<escape>,target_y:214}
  //    or call:move_paddle{direction:"up",target_y:214}
  {
    static const std::regex re_fg(
        R"(call:move_paddle\s*\{([^}]*)\})",
        std::regex::icase);
    std::smatch m;
    if (std::regex_search(text, m, re_fg)) {
      const std::string body = m[1].str();
      std::smatch dm;
      if (std::regex_search(body, dm, std::regex(R"(direction\s*[:=]\s*(?:<escape>)?\"?([a-zA-Z]+)\"?(?:<escape>)?)",
                                                 std::regex::icase))) {
        out.direction = dm[1].str();
        for (char& c : out.direction) c = static_cast<char>(std::tolower(c));
      }
      std::smatch ym;
      if (std::regex_search(body, ym, std::regex(R"(target_y\s*[:=]\s*(-?\d+))", std::regex::icase))) {
        out.target_y = std::stoi(ym[1].str());
      }
      out.raw_function_call = "move_paddle(direction=" +
          (out.direction.empty() ? "?" : out.direction) +
          ", target_y=" + std::to_string(out.target_y) + ")";
      out.ok = (out.target_y >= 0);
      return;
    }
  }

  // 2) JSON tool_calls style: {"name":"move_paddle","arguments":"{...}"}
  {
    static const std::regex re_json(
        R"rx("name"\s*:\s*"move_paddle"[^}]*"arguments"\s*:\s*"((?:\\.|[^"\\])*)")rx",
        std::regex::icase);
    std::smatch m;
    if (std::regex_search(text, m, re_json)) {
      std::string args = m[1].str();
      // Unescape common JSON escapes.
      std::string unesc;
      for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == '\\' && i + 1 < args.size()) {
          unesc.push_back(args[++i] == 'n' ? ' ' : args[i]);
        } else {
          unesc.push_back(args[i]);
        }
      }
      try {
        auto j = json::parse(unesc);
        if (j.contains("direction")) out.direction = j["direction"].get<std::string>();
        if (j.contains("target_y")) out.target_y = j["target_y"].get<int>();
      } catch (...) {
        // fall through to free-form
      }
      for (char& c : out.direction) c = static_cast<char>(std::tolower(c));
      out.raw_function_call = "move_paddle(direction=" +
          (out.direction.empty() ? "?" : out.direction) +
          ", target_y=" + std::to_string(out.target_y) + ")";
      out.ok = (out.target_y >= 0);
      if (out.ok) return;
    }
  }

  // 3) Free-form: move_paddle(direction=up, target_y=214) or similar.
  {
    static const std::regex re_free(
        R"(move_paddle\s*\(\s*direction\s*=\s*\"?([a-zA-Z]+)\"?\s*,\s*target_y\s*=\s*(-?\d+)\s*\))",
        std::regex::icase);
    std::smatch m;
    if (std::regex_search(text, m, re_free)) {
      out.direction = m[1].str();
      for (char& c : out.direction) c = static_cast<char>(std::tolower(c));
      out.target_y = std::stoi(m[2].str());
      out.raw_function_call = "move_paddle(direction=" + out.direction +
                              ", target_y=" + std::to_string(out.target_y) + ")";
      out.ok = true;
      return;
    }
  }

  // 4) Bare target_y=N somewhere.
  {
    static const std::regex re_y(R"(target_y\s*[:=]\s*(-?\d+))", std::regex::icase);
    std::smatch m;
    if (std::regex_search(text, m, re_y)) {
      out.target_y = std::stoi(m[1].str());
      out.direction = "stay";
      out.raw_function_call = "move_paddle(direction=stay, target_y=" +
                              std::to_string(out.target_y) + ") [partial parse]";
      out.ok = true;
      return;
    }
  }

  out.raw_function_call = "(unparsed) " + text.substr(0, 120);
  out.ok = false;
}

InferenceResult ModelClient::PostCompletion(const std::string& prompt) const {
  InferenceResult result;
  result.prompt_sent = prompt;

  json body = {
      {"prompt", prompt},
      {"n_predict", 64},
      {"temperature", 0.1},
      {"top_p", 0.9},
      {"stop", json::array({"<end_function_call>", "<end_of_turn>", "<start_of_turn>"})},
      {"cache_prompt", true},
  };

  long http_code = 0;
  const auto t0 = std::chrono::steady_clock::now();
  const std::string resp = HttpPost(base_url_ + "/completion", body.dump(), http_code);
  const auto t1 = std::chrono::steady_clock::now();
  result.latency_ms =
      std::chrono::duration<double, std::milli>(t1 - t0).count();

  if (http_code <= 0) {
    result.error = "HTTP error connecting to " + base_url_ + "/completion: " + resp;
    return result;
  }
  if (http_code != 200) {
    result.error = "HTTP " + std::to_string(http_code) + ": " + resp.substr(0, 200);
    return result;
  }

  try {
    auto j = json::parse(resp);
    std::string content;
    if (j.contains("content") && j["content"].is_string()) {
      content = j["content"].get<std::string>();
    } else if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
      // Some proxies wrap completion like OpenAI.
      content = j["choices"][0].value("text", "");
    }

    if (j.contains("timings") && j["timings"].is_object()) {
      const auto& t = j["timings"];
      // llama.cpp timings: prompt_ms, predicted_ms, predicted_n, predicted_per_second
      if (t.contains("prompt_ms")) result.ttft_ms = t["prompt_ms"].get<double>();
      if (t.contains("predicted_ms")) result.decode_ms = t["predicted_ms"].get<double>();
      if (t.contains("predicted_per_second"))
        result.tokens_per_sec = t["predicted_per_second"].get<double>();
      if (t.contains("predicted_n")) result.completion_tokens = t["predicted_n"].get<int>();
    }
    if (result.tokens_per_sec <= 0.0 && result.decode_ms > 0.0 && result.completion_tokens > 0) {
      result.tokens_per_sec = 1000.0 * result.completion_tokens / result.decode_ms;
    }

    ParseFunctionCall(content, result);
  } catch (const std::exception& e) {
    result.error = std::string("JSON parse: ") + e.what();
  }
  return result;
}

InferenceResult ModelClient::PostChat(const std::string& state_line) const {
  InferenceResult result;
  // OpenAI-compatible chat path (llama-server --jinja) as secondary path.
  const std::string user_msg =
      "Pong defense. State: " + state_line +
      "\nCall move_paddle once to position the right paddle.";
  result.prompt_sent = user_msg;

  json tools = json::array({
      {{"type", "function"},
       {"function",
        {{"name", "move_paddle"},
         {"description",
          "Move the AI (right) paddle to intercept the ball. target_y is desired center Y."},
         {"parameters",
          {{"type", "object"},
           {"properties",
            {{"direction",
              {{"type", "string"},
               {"enum", json::array({"up", "down", "stay"})},
               {"description", "up, down, or stay"}}},
             {"target_y",
              {{"type", "integer"},
               {"description", "Paddle center Y in pixels"}}}}},
           {"required", json::array({"direction", "target_y"})}}}}}}});

  json body = {
      {"model", "functiongemma"},
      {"temperature", 0.1},
      {"max_tokens", 64},
      {"tools", tools},
      {"tool_choice", {{"type", "function"}, {"function", {{"name", "move_paddle"}}}}},
      {"messages",
       json::array({
           {{"role", "system"},
            {"content",
             "You are the right paddle in Pong. Always call move_paddle with the best target_y."}},
           {{"role", "user"}, {"content", user_msg}},
       })},
  };

  long http_code = 0;
  const auto t0 = std::chrono::steady_clock::now();
  const std::string resp = HttpPost(base_url_ + "/v1/chat/completions", body.dump(), http_code);
  const auto t1 = std::chrono::steady_clock::now();
  result.latency_ms =
      std::chrono::duration<double, std::milli>(t1 - t0).count();

  if (http_code <= 0) {
    result.error = "HTTP error: " + resp;
    return result;
  }
  if (http_code != 200) {
    result.error = "HTTP " + std::to_string(http_code) + ": " + resp.substr(0, 200);
    return result;
  }

  try {
    auto j = json::parse(resp);
    if (j.contains("usage")) {
      // no per-token timing here; estimate from wall clock
    }
    std::string content;
    if (j.contains("choices") && !j["choices"].empty()) {
      const auto& msg = j["choices"][0]["message"];
      if (msg.contains("tool_calls") && msg["tool_calls"].is_array() &&
          !msg["tool_calls"].empty()) {
        const auto& tc = msg["tool_calls"][0];
        std::string name = tc.value("function", json::object()).value("name", "");
        std::string args = tc.value("function", json::object()).value("arguments", "");
        content = "call:" + name + args;
        // Also try parse arguments as JSON.
        try {
          auto aj = json::parse(args);
          if (aj.contains("direction")) result.direction = aj["direction"].get<std::string>();
          if (aj.contains("target_y")) result.target_y = aj["target_y"].get<int>();
          for (char& c : result.direction) c = static_cast<char>(std::tolower(c));
          result.raw_function_call =
              "move_paddle(direction=" + (result.direction.empty() ? "?" : result.direction) +
              ", target_y=" + std::to_string(result.target_y) + ")";
          result.raw_output = content;
          result.ok = (result.target_y >= 0);
          if (result.ok) {
            if (result.latency_ms > 0 && result.completion_tokens == 0) {
              // rough tok/s if usage present
              if (j.contains("usage") && j["usage"].contains("completion_tokens")) {
                result.completion_tokens = j["usage"]["completion_tokens"].get<int>();
                if (result.completion_tokens > 0)
                  result.tokens_per_sec =
                      1000.0 * result.completion_tokens / result.latency_ms;
              }
            }
            return result;
          }
        } catch (...) {
        }
      }
      if (msg.contains("content") && msg["content"].is_string()) {
        content = msg["content"].get<std::string>();
      }
    }
    ParseFunctionCall(content, result);
  } catch (const std::exception& e) {
    result.error = std::string("JSON parse: ") + e.what();
  }
  return result;
}

InferenceResult ModelClient::Decide(const std::string& state_line) {
  const std::string prompt = BuildPrompt(state_line);
  InferenceResult r = PostCompletion(prompt);
  if (r.ok || r.error.empty()) {
    // Even if unparsed, if we got HTTP 200 with content, keep that.
    if (r.ok || !r.raw_output.empty()) return r;
  }
  // Fallback: OpenAI chat tools endpoint (same server, different path).
  InferenceResult chat = PostChat(state_line);
  if (chat.ok || chat.error.empty()) return chat;
  // Prefer the more informative error.
  if (!r.error.empty()) return r;
  return chat;
}
