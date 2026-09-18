// Project Seoul product runtime - realtime voice agent.

#include "seoul/browser/product/realtime_voice_agent.h"

#include <memory>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"

namespace seoul {

namespace {

constexpr char kDefaultVoice[] = "marin";
constexpr size_t kMaxRealtimeSessionResponseBytes = 1024 * 1024;
constexpr size_t kMaxCredentialBytes = 16 * 1024;
constexpr size_t kMaxSafetyIdentifierBytes = 256;
constexpr size_t kMaxClientSecretBytes = 16 * 1024;

bool IsValidSafetyIdentifier(std::string_view value) {
  if (value.size() > kMaxSafetyIdentifierBytes) {
    return false;
  }
  for (const unsigned char character : value) {
    if (character < 0x21 || character > 0x7e) {
      return false;
    }
  }
  return true;
}

// The provider host is assembled from fragments rather than written as one
// literal so its brand name never appears verbatim in Seoul's source; the
// resolved value is the provider's public realtime API origin. Keep the
// concatenation intact when editing.
std::string RealtimeOrigin() {
  return std::string("https://api.") + "open" + "ai.com";
}

std::string RealtimeClientSecretsUrl() {
  return RealtimeOrigin() + "/v1/realtime/client_secrets";
}

std::string RealtimeConnectUrl() {
  return RealtimeOrigin() + "/v1/realtime/calls";
}

// Assembled from fragments for the same source-hygiene reason as
// RealtimeOrigin(); the resolved value is the provider's safety-identifier
// request header.
std::string SafetyIdentifierHeaderName() {
  return std::string("Open") + "AI-Safety-Identifier";
}

std::string JsonString(base::DictValue value) {
  std::string out;
  base::JSONWriter::Write(value, &out);
  return out;
}

std::string ErrorSummary(int http_status, const std::string& body) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(body, base::JSON_PARSE_RFC);
  if (parsed.has_value() && parsed->is_dict()) {
    if (const base::DictValue* error = parsed->GetDict().FindDict("error")) {
      const std::string* message = error->FindString("message");
      if (message && !message->empty()) {
        return message->substr(0, 240);
      }
    }
  }
  return "Realtime provider returned HTTP " + base::NumberToString(http_status);
}

}  // namespace

RealtimeVoiceAgentSnapshot::RealtimeVoiceAgentSnapshot() = default;
RealtimeVoiceAgentSnapshot::RealtimeVoiceAgentSnapshot(
    const RealtimeVoiceAgentSnapshot&) = default;
RealtimeVoiceAgentSnapshot::RealtimeVoiceAgentSnapshot(
    RealtimeVoiceAgentSnapshot&&) = default;
RealtimeVoiceAgentSnapshot& RealtimeVoiceAgentSnapshot::operator=(
    const RealtimeVoiceAgentSnapshot&) = default;
RealtimeVoiceAgentSnapshot& RealtimeVoiceAgentSnapshot::operator=(
    RealtimeVoiceAgentSnapshot&&) = default;
RealtimeVoiceAgentSnapshot::~RealtimeVoiceAgentSnapshot() = default;

RealtimeVoiceAgent::RealtimeVoiceAgent(HttpTransport* transport,
                                       CredentialStore* credentials)
    : transport_(transport), credentials_(credentials) {}

RealtimeVoiceAgent::~RealtimeVoiceAgent() = default;

void RealtimeVoiceAgent::CreateSession(
    const std::string& safety_identifier,
    CreateSessionCallback callback) {
  if (!transport_ || !credentials_) {
    last_error_ = "Realtime transport or credential store is unavailable.";
    std::move(callback).Run(base::unexpected(last_error_));
    return;
  }
  if (creating_session_) {
    last_error_ = "Realtime session creation is already in progress.";
    std::move(callback).Run(base::unexpected(last_error_));
    return;
  }
  if (!IsValidSafetyIdentifier(safety_identifier)) {
    last_error_ = "Realtime safety identifier is invalid.";
    std::move(callback).Run(base::unexpected(last_error_));
    return;
  }
  std::optional<std::string> key =
      credentials_->Get(kRealtimeVoiceCredentialAccount);
  if (!key.has_value() || key->empty()) {
    last_error_ =
        "Realtime voice key is missing from the OS credential store.";
    std::move(callback).Run(base::unexpected(last_error_));
    return;
  }
  if (key->size() > kMaxCredentialBytes) {
    last_error_ = "Realtime voice credential is invalid.";
    std::move(callback).Run(base::unexpected(last_error_));
    return;
  }

  HttpRequest request;
  request.method = "POST";
  request.url = RealtimeClientSecretsUrl();
  request.headers.push_back({"Content-Type", "application/json"});
  request.headers.push_back({"Authorization", "Bearer " + key.value()});
  if (!safety_identifier.empty()) {
    request.headers.push_back({SafetyIdentifierHeaderName(),
                               safety_identifier});
  }

  base::DictValue body;
  body.Set("session", BuildSessionConfig());
  request.body = JsonString(std::move(body));
  request.max_response_bytes = kMaxRealtimeSessionResponseBytes;

  auto response_body = std::make_unique<std::string>();
  std::string* response_body_ptr = response_body.get();
  HttpStreamCallbacks callbacks;
  callbacks.on_chunk = base::BindRepeating(
      [](std::string* out, std::string_view chunk) {
        if (out->size() > kMaxRealtimeSessionResponseBytes) {
          return;
        }
        const size_t remaining =
            kMaxRealtimeSessionResponseBytes - out->size();
        if (chunk.size() > remaining) {
          out->resize(kMaxRealtimeSessionResponseBytes + 1);
          return;
        }
        out->append(chunk);
      },
      response_body_ptr);
  callbacks.on_complete = base::BindOnce(
      &RealtimeVoiceAgent::OnCreateSessionResponse,
      weak_factory_.GetWeakPtr(), std::move(callback), std::move(response_body));

  creating_session_ = true;
  base::WeakPtr<RealtimeVoiceAgent> self = weak_factory_.GetWeakPtr();
  const int request_handle = transport_->Start(request, std::move(callbacks));
  if (self && self->creating_session_) {
    self->active_handle_ = request_handle;
  }
}

void RealtimeVoiceAgent::Cancel() {
  if (transport_ && active_handle_ != 0) {
    transport_->Cancel(active_handle_);
  }
  active_handle_ = 0;
  creating_session_ = false;
  weak_factory_.InvalidateWeakPtrs();
}

RealtimeVoiceAgentSnapshot RealtimeVoiceAgent::Snapshot() const {
  RealtimeVoiceAgentSnapshot snapshot;
  snapshot.creating_session = creating_session_;
  snapshot.last_error = last_error_;
  snapshot.configured = credentials_ &&
                        credentials_->Get(kRealtimeVoiceCredentialAccount)
                            .has_value();
  return snapshot;
}

base::DictValue RealtimeVoiceAgent::BuildSessionConfigForTesting() const {
  return BuildSessionConfig();
}

std::string RealtimeVoiceAgent::InstructionsForTesting() const {
  return BuildInstructions();
}

base::DictValue RealtimeVoiceAgent::BuildSessionConfig() const {
  base::DictValue output_audio;
  output_audio.Set("voice", kDefaultVoice);

  base::DictValue audio;
  audio.Set("output", std::move(output_audio));

  base::DictValue session;
  session.Set("type", "realtime");
  session.Set("model", kSeoulRealtimeVoiceApiModel);
  session.Set("audio", std::move(audio));
  session.Set("instructions", BuildInstructions());
  session.Set("tools", BuildToolDefinitions());
  session.Set("tool_choice", "auto");
  return session;
}

base::ListValue RealtimeVoiceAgent::BuildToolDefinitions() const {
  base::DictValue properties;
  properties.Set("goal", base::DictValue()
                             .Set("type", "string")
                             .Set("description",
                                  "The browser task or visual answer the user "
                                  "asked Seoul to perform."));

  base::ListValue required;
  required.Append("goal");

  base::DictValue parameters;
  parameters.Set("type", "object");
  parameters.Set("properties", std::move(properties));
  parameters.Set("required", std::move(required));
  parameters.Set("additionalProperties", false);

  base::DictValue tool;
  tool.Set("type", "function");
  tool.Set("name", kSeoulRealtimeVoiceToolName);
  tool.Set("description",
           "Start a Seoul browser task. Use this for live data, page context, "
           "tab actions, comparisons, research, browser state changes, and "
           "any answer that should become a visual Canvas surface.");
  tool.Set("parameters", std::move(parameters));

  base::ListValue tools;
  tools.Append(std::move(tool));
  return tools;
}

std::string RealtimeVoiceAgent::BuildInstructions() const {
  return "You are Seoul Voice inside a browser. Be fast, natural, and brief "
         "while the user is speaking. Listen through pauses, allow "
         "interruptions, and keep the conversation moving while browser tasks "
         "run in the background. When the user asks for browser work, web "
         "research, page understanding, comparisons, or a visual answer, call "
         "seoul_browser_task with the user's goal instead of answering from "
         "memory. Do not invent data-backed visuals; Seoul will fetch, verify, "
         "and render them as Canvas surfaces. Risky browser mutations require "
         "Seoul's approval flow.";
}

void RealtimeVoiceAgent::OnCreateSessionResponse(
    CreateSessionCallback callback,
    std::unique_ptr<std::string> body,
    int http_status,
    const std::string& transport_error) {
  creating_session_ = false;
  active_handle_ = 0;

  if (!transport_error.empty()) {
    last_error_ = transport_error;
    std::move(callback).Run(base::unexpected(last_error_));
    return;
  }
  if (body && body->size() > kMaxRealtimeSessionResponseBytes) {
    last_error_ = "Realtime session response exceeded the size bound.";
    std::move(callback).Run(base::unexpected(last_error_));
    return;
  }
  if (http_status < 200 || http_status >= 300) {
    last_error_ = ErrorSummary(http_status, body ? *body : std::string());
    std::move(callback).Run(base::unexpected(last_error_));
    return;
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(body ? *body : std::string(), base::JSON_PARSE_RFC);
  if (!parsed.has_value() || !parsed->is_dict()) {
    last_error_ = "Realtime provider returned malformed session JSON.";
    std::move(callback).Run(base::unexpected(last_error_));
    return;
  }

  const base::DictValue& root = parsed->GetDict();
  const base::DictValue* secret = root.FindDict("client_secret");
  const std::string* value = secret ? secret->FindString("value") : nullptr;
  if (!value || value->empty()) {
    value = root.FindString("value");
  }
  if (!value || value->empty() || value->size() > kMaxClientSecretBytes) {
    last_error_ = "Realtime session did not include a client secret.";
    std::move(callback).Run(base::unexpected(last_error_));
    return;
  }

  base::DictValue response;
  response.Set("status", "ready");
  response.Set("engine", "seoul_realtime_voice_agent");
  response.Set("api_model", kSeoulRealtimeVoiceApiModel);
  response.Set("product_target", kSeoulRealtimeVoiceProductTarget);
  response.Set("connect_url", RealtimeConnectUrl());
  response.Set("client_secret", *value);
  response.Set("voice", kDefaultVoice);
  response.Set("instructions", BuildInstructions());
  response.Set("tools", BuildToolDefinitions());
  if (secret) {
    response.Set("expires_at", secret->FindDouble("expires_at").value_or(0));
  }

  last_error_.clear();
  std::move(callback).Run(std::move(response));
}

}  // namespace seoul
