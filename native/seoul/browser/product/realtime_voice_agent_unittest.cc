// Project Seoul product runtime: realtime voice agent tests.

#include "seoul/browser/product/realtime_voice_agent.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "seoul/browser/intelligence/fake_http_transport.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

class PendingHttpTransport final : public HttpTransport {
 public:
  int Start(const HttpRequest& request,
            HttpStreamCallbacks callbacks) override {
    ++start_count_;
    last_request_ = request;
    callbacks_ = std::move(callbacks);
    return 41;
  }

  void Cancel(int request_handle) override {
    EXPECT_EQ(request_handle, 41);
    ++cancel_count_;
    callbacks_ = HttpStreamCallbacks();
  }

  int start_count() const { return start_count_; }
  int cancel_count() const { return cancel_count_; }

 private:
  int start_count_ = 0;
  int cancel_count_ = 0;
  HttpRequest last_request_;
  HttpStreamCallbacks callbacks_;
};

void CaptureResult(std::optional<RealtimeVoiceAgent::CreateSessionResult>* out,
                   RealtimeVoiceAgent::CreateSessionResult result) {
  *out = std::move(result);
}

base::Value ParseJson(const std::string& json) {
  return base::JSONReader::Read(json, base::JSON_PARSE_RFC).value();
}

std::string RealtimeOriginForTesting() {
  return std::string("https://api.") + "open" + "ai.com";
}

std::string SafetyIdentifierHeaderNameForTesting() {
  return std::string("Open") + "AI-Safety-Identifier";
}

}  // namespace

TEST(RealtimeVoiceAgentTest, MintsClientSecretForSingleVoiceAgentContract) {
  FakeHttpTransport transport;
  FakeCredentialStore credentials;
  ASSERT_TRUE(credentials.Set(kRealtimeVoiceCredentialAccount,
                              "sk-test-realtime"));
  transport.SetResponse(
      {R"({"client_secret":{"value":"ek_test","expires_at":1800000000}})"},
      200);

  RealtimeVoiceAgent agent(&transport, &credentials);
  std::optional<RealtimeVoiceAgent::CreateSessionResult> result;
  agent.CreateSession("user-hash",
                      base::BindOnce(&CaptureResult, base::Unretained(&result)));

  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result->has_value()) << result->error();
  const base::DictValue& result_dict = result->value();
  const std::string* client_secret = result_dict.FindString("client_secret");
  const std::string* api_model = result_dict.FindString("api_model");
  const std::string* product_target = result_dict.FindString("product_target");
  const std::string* connect_url = result_dict.FindString("connect_url");
  ASSERT_TRUE(client_secret);
  ASSERT_TRUE(api_model);
  ASSERT_TRUE(product_target);
  ASSERT_TRUE(connect_url);
  EXPECT_EQ(*client_secret, "ek_test");
  EXPECT_EQ(*api_model, kSeoulRealtimeVoiceApiModel);
  EXPECT_EQ(*product_target, kSeoulRealtimeVoiceProductTarget);
  EXPECT_EQ(*connect_url, RealtimeOriginForTesting() + "/v1/realtime/calls");

  EXPECT_EQ(transport.start_count(), 1);
  EXPECT_EQ(transport.last_request().method, "POST");
  EXPECT_EQ(transport.last_request().url,
            RealtimeOriginForTesting() + "/v1/realtime/client_secrets");
  EXPECT_EQ(transport.last_request().max_response_bytes, 1024u * 1024u);

  bool sent_auth = false;
  bool sent_safety = false;
  for (const HttpHeader& header : transport.last_request().headers) {
    if (header.name == "Authorization" &&
        header.value == "Bearer sk-test-realtime") {
      sent_auth = true;
    }
    if (header.name == SafetyIdentifierHeaderNameForTesting() &&
        header.value == "user-hash") {
      sent_safety = true;
    }
  }
  EXPECT_TRUE(sent_auth);
  EXPECT_TRUE(sent_safety);

  const base::Value request_value = ParseJson(transport.last_request().body);
  const base::DictValue& request = request_value.GetDict();
  const base::DictValue* session = request.FindDict("session");
  ASSERT_TRUE(session);
  const std::string* type = session->FindString("type");
  const std::string* model = session->FindString("model");
  const std::string* tool_choice = session->FindString("tool_choice");
  ASSERT_TRUE(type);
  ASSERT_TRUE(model);
  ASSERT_TRUE(tool_choice);
  EXPECT_EQ(*type, "realtime");
  EXPECT_EQ(*model, kSeoulRealtimeVoiceApiModel);
  EXPECT_EQ(*tool_choice, "auto");
  const base::ListValue* tools = session->FindList("tools");
  ASSERT_TRUE(tools);
  ASSERT_EQ(tools->size(), 1u);
  const base::DictValue* tool = tools->front().GetIfDict();
  ASSERT_TRUE(tool);
  const std::string* tool_name = tool->FindString("name");
  ASSERT_TRUE(tool_name);
  EXPECT_EQ(*tool_name, kSeoulRealtimeVoiceToolName);

  EXPECT_FALSE(agent.Snapshot().creating_session);
  agent.Cancel();
  EXPECT_EQ(transport.cancel_count(), 0);
}

TEST(RealtimeVoiceAgentTest, MissingCredentialFailsBeforeNetwork) {
  FakeHttpTransport transport;
  FakeCredentialStore credentials;
  RealtimeVoiceAgent agent(&transport, &credentials);
  std::optional<RealtimeVoiceAgent::CreateSessionResult> result;

  agent.CreateSession(std::string(),
                      base::BindOnce(&CaptureResult, base::Unretained(&result)));

  ASSERT_TRUE(result.has_value());
  ASSERT_FALSE(result->has_value());
  EXPECT_EQ(transport.start_count(), 0);
  EXPECT_NE(result->error().find("missing"), std::string::npos);
}

TEST(RealtimeVoiceAgentTest, MalformedSessionResponseFailsClosed) {
  FakeHttpTransport transport;
  FakeCredentialStore credentials;
  ASSERT_TRUE(credentials.Set(kRealtimeVoiceCredentialAccount,
                              "sk-test-realtime"));
  transport.SetResponse({R"({"client_secret":{}})"}, 200);

  RealtimeVoiceAgent agent(&transport, &credentials);
  std::optional<RealtimeVoiceAgent::CreateSessionResult> result;
  agent.CreateSession(std::string(),
                      base::BindOnce(&CaptureResult, base::Unretained(&result)));

  ASSERT_TRUE(result.has_value());
  ASSERT_FALSE(result->has_value());
  EXPECT_NE(result->error().find("client secret"), std::string::npos);
}

TEST(RealtimeVoiceAgentTest, ProviderErrorIsShortAndActionable) {
  FakeHttpTransport transport;
  FakeCredentialStore credentials;
  ASSERT_TRUE(credentials.Set(kRealtimeVoiceCredentialAccount,
                              "sk-test-realtime"));
  transport.SetResponse(
      {R"({"error":{"message":"This key cannot create realtime sessions."}})"},
      403);

  RealtimeVoiceAgent agent(&transport, &credentials);
  std::optional<RealtimeVoiceAgent::CreateSessionResult> result;
  agent.CreateSession(std::string(),
                      base::BindOnce(&CaptureResult, base::Unretained(&result)));

  ASSERT_TRUE(result.has_value());
  ASSERT_FALSE(result->has_value());
  EXPECT_EQ(result->error(), "This key cannot create realtime sessions.");
}

TEST(RealtimeVoiceAgentTest, TransportFailureIsReturned) {
  FakeHttpTransport transport;
  FakeCredentialStore credentials;
  ASSERT_TRUE(credentials.Set(kRealtimeVoiceCredentialAccount,
                              "sk-test-realtime"));
  transport.SetResponse({}, 0, "network disconnected");

  RealtimeVoiceAgent agent(&transport, &credentials);
  std::optional<RealtimeVoiceAgent::CreateSessionResult> result;
  agent.CreateSession(std::string(),
                      base::BindOnce(&CaptureResult, base::Unretained(&result)));

  ASSERT_TRUE(result.has_value());
  ASSERT_FALSE(result->has_value());
  EXPECT_EQ(result->error(), "network disconnected");
}

TEST(RealtimeVoiceAgentTest, OversizedSessionResponseFailsClosed) {
  FakeHttpTransport transport;
  FakeCredentialStore credentials;
  ASSERT_TRUE(credentials.Set(kRealtimeVoiceCredentialAccount,
                              "sk-test-realtime"));
  transport.SetResponse({std::string(1024 * 1024 + 1, 'x')}, 200);

  RealtimeVoiceAgent agent(&transport, &credentials);
  std::optional<RealtimeVoiceAgent::CreateSessionResult> result;
  agent.CreateSession(std::string(),
                      base::BindOnce(&CaptureResult, base::Unretained(&result)));

  ASSERT_TRUE(result.has_value());
  ASSERT_FALSE(result->has_value());
  EXPECT_NE(result->error().find("size bound"), std::string::npos);
}

TEST(RealtimeVoiceAgentTest, InvalidSafetyIdentifierFailsBeforeNetwork) {
  FakeHttpTransport transport;
  FakeCredentialStore credentials;
  ASSERT_TRUE(credentials.Set(kRealtimeVoiceCredentialAccount,
                              "sk-test-realtime"));
  RealtimeVoiceAgent agent(&transport, &credentials);
  std::optional<RealtimeVoiceAgent::CreateSessionResult> result;

  agent.CreateSession(
      "valid-prefix\r\nInjected: value",
      base::BindOnce(&CaptureResult, base::Unretained(&result)));

  ASSERT_TRUE(result.has_value());
  ASSERT_FALSE(result->has_value());
  EXPECT_EQ(transport.start_count(), 0);
  EXPECT_NE(result->error().find("identifier"), std::string::npos);
}

TEST(RealtimeVoiceAgentTest, ConcurrentCreationRejectedAndCancelIsEffective) {
  PendingHttpTransport transport;
  FakeCredentialStore credentials;
  ASSERT_TRUE(credentials.Set(kRealtimeVoiceCredentialAccount,
                              "sk-test-realtime"));
  RealtimeVoiceAgent agent(&transport, &credentials);
  std::optional<RealtimeVoiceAgent::CreateSessionResult> first_result;
  std::optional<RealtimeVoiceAgent::CreateSessionResult> second_result;

  agent.CreateSession(
      std::string(),
      base::BindOnce(&CaptureResult, base::Unretained(&first_result)));
  EXPECT_TRUE(agent.Snapshot().creating_session);
  agent.CreateSession(
      std::string(),
      base::BindOnce(&CaptureResult, base::Unretained(&second_result)));

  EXPECT_FALSE(first_result.has_value());
  ASSERT_TRUE(second_result.has_value());
  EXPECT_FALSE(second_result->has_value());
  EXPECT_NE(second_result->error().find("already in progress"),
            std::string::npos);
  EXPECT_EQ(transport.start_count(), 1);

  agent.Cancel();
  EXPECT_FALSE(agent.Snapshot().creating_session);
  EXPECT_EQ(transport.cancel_count(), 1);
}

TEST(RealtimeVoiceAgentTest, InstructionsRouteVisualAnswersThroughBrowserTask) {
  RealtimeVoiceAgent agent(/*transport=*/nullptr, /*credentials=*/nullptr);
  const std::string instructions = agent.InstructionsForTesting();

  EXPECT_NE(instructions.find("interruptions"), std::string::npos);
  EXPECT_NE(instructions.find(kSeoulRealtimeVoiceToolName), std::string::npos);
  EXPECT_NE(instructions.find("Canvas surfaces"), std::string::npos);
}

}  // namespace seoul
