// Project Seoul product runtime: the provider registry.

#include "seoul/browser/product/provider_registry.h"

#include <optional>
#include <utility>

#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "seoul/browser/intelligence/fake_http_transport.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

class ProviderRegistryTest : public testing::Test {
 protected:
  base::test::TaskEnvironment environment_;
  FakeHttpTransport local_transport_;
  FakeHttpTransport cloud_transport_;
  FakeCredentialStore credentials_;
};

TEST_F(ProviderRegistryTest, LocalModeRejectsNonLoopbackEndpoints) {
  ProviderRegistry registry(&local_transport_, &cloud_transport_,
                            &credentials_);
  EXPECT_FALSE(
      registry.ConfigureLocal("https://model.example.com/v1", "some-model"));
  EXPECT_TRUE(
      registry.ConfigureLocal("http://127.0.0.1:8080/v1", "some-model"));
  EXPECT_TRUE(
      registry.ConfigureLocal("http://localhost:8080/v1", "some-model"));
  const ProviderStateSnapshot snapshot = registry.Snapshot();
  EXPECT_TRUE(snapshot.local_configured);
  // Configured but not yet health-checked: not usable.
  EXPECT_FALSE(snapshot.local_healthy);
  EXPECT_FALSE(registry.local_available());
}

TEST_F(ProviderRegistryTest, HealthCheckDiscoversModels) {
  ProviderRegistry registry(&local_transport_, &cloud_transport_,
                            &credentials_);
  ASSERT_TRUE(
      registry.ConfigureLocal("http://127.0.0.1:8080/v1", "model-a"));
  local_transport_.SetResponse(
      {R"({"data": [{"id": "model-a"}, {"id": "model-b"}]})"}, 200);
  base::test::TestFuture<bool> future;
  registry.CheckLocalHealth(future.GetCallback());
  EXPECT_TRUE(future.Get());
  const ProviderStateSnapshot snapshot = registry.Snapshot();
  EXPECT_TRUE(snapshot.local_healthy);
  ASSERT_EQ(snapshot.local_models_discovered.size(), 2u);
  EXPECT_EQ(snapshot.local_models_discovered[0], "model-a");
  EXPECT_TRUE(registry.local_available());
  // The health request hit the models endpoint on the loopback host.
  EXPECT_EQ(local_transport_.last_request().url,
            "http://127.0.0.1:8080/v1/models");
}

TEST_F(ProviderRegistryTest, UnhealthyEndpointReportsOfflineState) {
  ProviderRegistry registry(&local_transport_, &cloud_transport_,
                            &credentials_);
  ASSERT_TRUE(
      registry.ConfigureLocal("http://127.0.0.1:8080/v1", "some-model"));
  local_transport_.SetResponse({}, 0, "connection refused");
  base::test::TestFuture<bool> future;
  registry.CheckLocalHealth(future.GetCallback());
  EXPECT_FALSE(future.Get());
  EXPECT_FALSE(registry.local_available());
  EXPECT_FALSE(registry.Snapshot().last_error.empty());
}

TEST_F(ProviderRegistryTest, SuccessfulHttpResponseMustListTheSelectedModel) {
  ProviderRegistry registry(&local_transport_, &cloud_transport_, &credentials_);
  ASSERT_TRUE(registry.ConfigureLocal("http://localhost:8080/v1", "selected-model"));
  for (const std::string& response : {std::string("<html>Not an API</html>"),
      std::string(R"({"data":[{"id":"different-model"}]})")}) {
    local_transport_.SetResponse({response}, 200);
    base::test::TestFuture<bool> health;
    registry.CheckLocalHealth(health.GetCallback());
    EXPECT_FALSE(health.Get());
    EXPECT_FALSE(registry.local_available());
    EXPECT_FALSE(registry.Snapshot().last_error.empty());
  }
  local_transport_.SetResponse({R"({"data":[{"id":"selected-model"}]})"}, 200);
  base::test::TestFuture<bool> health;
  registry.CheckLocalHealth(health.GetCallback());
  EXPECT_TRUE(health.Get());
  EXPECT_TRUE(registry.Snapshot().last_error.empty());
}

TEST_F(ProviderRegistryTest, LocalDiscoveryAndGenerationUseOneNormalizedBase) {
  for (const char* endpoint : {"http://127.0.0.1:11434/v1",
                               "http://127.0.0.1:11434/v1/",
                               "http://127.0.0.1:11434/v1/chat/completions",
                               "http://127.0.0.1:11434"}) {
    SCOPED_TRACE(endpoint);
    ProviderRegistry registry(&local_transport_, &cloud_transport_, &credentials_);
    ASSERT_TRUE(registry.ConfigureLocal(endpoint, "test-model"));
    local_transport_.SetResponse({R"({"data":[{"id":"test-model"}]})"}, 200);
    base::test::TestFuture<bool> health;
    registry.CheckLocalHealth(health.GetCallback());
    ASSERT_TRUE(health.Get());
    EXPECT_EQ(local_transport_.last_request().url,
              "http://127.0.0.1:11434/v1/models");
    local_transport_.SetResponse({
        "data: {\"choices\":[{\"delta\":{\"content\":\"Ready\"}}]}\n\n",
        "data: [DONE]\n\n"}, 200);
    base::test::TestFuture<base::expected<GenerationResult, std::string>> reply;
    registry.Generate(GenerationRequest(), true, reply.GetCallback());
    ASSERT_TRUE(reply.Get().has_value());
    EXPECT_EQ(reply.Get()->text, "Ready");
    EXPECT_EQ(local_transport_.last_request().url,
              "http://127.0.0.1:11434/v1/chat/completions");
  }
}

TEST_F(ProviderRegistryTest, RejectsAmbiguousLocalBasesWithoutReplacingSettings) {
  ProviderRegistry registry(&local_transport_, &cloud_transport_, &credentials_);
  ASSERT_TRUE(registry.ConfigureLocal("http://localhost:11434/v1", "model"));
  for (const char* endpoint : {"http://localhost:11434/v1?key=secret",
                               "http://localhost:11434/v1#fragment",
                               "http://user:secret@localhost:11434/v1"}) {
    EXPECT_FALSE(registry.ConfigureLocal(endpoint, "model"));
    EXPECT_EQ(registry.Snapshot().local_endpoint, "http://localhost:11434/v1");
  }
}

TEST_F(ProviderRegistryTest, CloudGenerationUsesTheSupportedMessagesContract) {
  ProviderRegistry registry(&local_transport_, &cloud_transport_, &credentials_);
  ASSERT_TRUE(credentials_.Set(kCloudReasoningCredentialAccount, "test-secret"));
  ASSERT_TRUE(registry.ConfigureCloud("test-model", true));
  cloud_transport_.SetResponse({
      "data: {\"type\":\"content_block_delta\",\"delta\":{\"text\":\"Ready\"}}\n\n",
      "data: {\"type\":\"message_stop\"}\n\n"}, 200);
  base::test::TestFuture<base::expected<GenerationResult, std::string>> reply;
  registry.Generate(GenerationRequest(), false, reply.GetCallback());
  ASSERT_TRUE(reply.Get().has_value());
  EXPECT_EQ(cloud_transport_.last_request().url,
            "https://api.anthropic.com/v1/messages");
  EXPECT_EQ(cloud_transport_.last_request().method, "POST");
  bool version = false;
  bool authentication = false;
  for (const HttpHeader& header : cloud_transport_.last_request().headers) {
    version |= header.name == "anthropic-version" && header.value == "2023-06-01";
    authentication |= header.name == "Authorization" &&
                      header.value == "Bearer test-secret";
  }
  EXPECT_TRUE(version);
  EXPECT_TRUE(authentication);
}

TEST_F(ProviderRegistryTest, CloudRequiresCredentialAndEnabledSwitch) {
  ProviderRegistry registry(&local_transport_, &cloud_transport_,
                            &credentials_);
  ASSERT_TRUE(registry.ConfigureCloud("cloud-model", /*enabled=*/true));
  // No credential yet: not available, and not "configured".
  EXPECT_FALSE(registry.cloud_available());
  EXPECT_FALSE(registry.Snapshot().cloud_configured);

  credentials_.Set("cloud_reasoning", "secret-value");
  EXPECT_TRUE(registry.cloud_available());
  EXPECT_TRUE(registry.Snapshot().cloud_configured);

  registry.SetCloudEnabled(false);
  EXPECT_FALSE(registry.cloud_available());
}

TEST_F(ProviderRegistryTest, ClearCloudCancelsAndRemovesRouteSettings) {
  ProviderRegistry registry(&local_transport_, &cloud_transport_,
                            &credentials_);
  ASSERT_TRUE(registry.ConfigureCloud("cloud-model", /*enabled=*/true));
  ASSERT_TRUE(credentials_.Set(kCloudReasoningCredentialAccount, "secret"));
  ASSERT_TRUE(registry.cloud_available());

  registry.ClearCloud();

  const ProviderStateSnapshot snapshot = registry.Snapshot();
  EXPECT_FALSE(snapshot.cloud_enabled);
  EXPECT_TRUE(snapshot.cloud_model.empty());
  EXPECT_FALSE(registry.cloud_available());
  // Registry clearing never silently deletes the OS-owned credential. The
  // Studio runtime wrapper performs that separate, explicit operation.
  EXPECT_TRUE(snapshot.cloud_configured);
}

TEST_F(ProviderRegistryTest, PlanRequesterFallsBackToNulloptWithNoProvider) {
  ProviderRegistry registry(&local_transport_, &cloud_transport_,
                            &credentials_);
  ModelPlanRequester requester = registry.MakePlanRequester();
  base::test::TestFuture<std::optional<base::DictValue>, PlanOrigin> future;
  requester.Run("{\"goal\": \"x\"}", /*prefer_local=*/true,
                /*allow_cloud_models=*/true,
                future.GetCallback());
  EXPECT_FALSE(std::get<0>(future.Get()).has_value());
}

TEST_F(ProviderRegistryTest, CloudPolicyNeverFallsBackToCloud) {
  ProviderRegistry registry(&local_transport_, &cloud_transport_,
                            &credentials_);
  ASSERT_TRUE(registry.ConfigureCloud("cloud-model", /*enabled=*/true));
  ASSERT_TRUE(
      credentials_.Set(kCloudReasoningCredentialAccount, "secret-value"));
  ASSERT_TRUE(registry.cloud_available());
  EXPECT_TRUE(registry.HasUsableProvider(/*allow_cloud_models=*/true));
  EXPECT_FALSE(registry.HasUsableProvider(/*allow_cloud_models=*/false));

  ModelPlanRequester requester = registry.MakePlanRequester();
  base::test::TestFuture<std::optional<base::DictValue>, PlanOrigin> future;
  requester.Run("{\"goal\": \"x\"}", /*prefer_local=*/true,
                /*allow_cloud_models=*/false, future.GetCallback());
  EXPECT_FALSE(std::get<0>(future.Get()).has_value());
  EXPECT_EQ(cloud_transport_.start_count(), 0);
}

TEST_F(ProviderRegistryTest, SettingsPersistWithoutSecrets) {
  ProviderRegistry registry(&local_transport_, &cloud_transport_,
                            &credentials_);
  ASSERT_TRUE(
      registry.ConfigureLocal("http://127.0.0.1:8080/v1", "some-model"));
  ASSERT_TRUE(registry.ConfigureCloud("cloud-model", true));
  credentials_.Set("cloud_reasoning", "secret-value");

  const base::DictValue persisted = registry.TakePersistedState();
  // No secret material in the persisted settings.
  std::string serialized;
  for (const auto [key, value] : persisted) {
    if (value.is_string()) {
      serialized += value.GetString();
    }
  }
  EXPECT_EQ(serialized.find("secret-value"), std::string::npos);

  ProviderRegistry restored(&local_transport_, &cloud_transport_,
                            &credentials_);
  restored.RestorePersistedState(persisted);
  const ProviderStateSnapshot snapshot = restored.Snapshot();
  EXPECT_TRUE(snapshot.local_configured);
  EXPECT_EQ(snapshot.local_endpoint, "http://127.0.0.1:8080/v1");
  EXPECT_TRUE(snapshot.cloud_enabled);
}

}  // namespace
}  // namespace seoul
