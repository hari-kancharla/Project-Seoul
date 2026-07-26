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
      registry.ConfigureLocal("http://127.0.0.1:8080/v1", "some-model"));
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
