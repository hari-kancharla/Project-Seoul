// Project Seoul product runtime: capability execution.

#include "seoul/browser/product/capability_executor.h"

#include <memory>
#include <utility>

#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

class StubExecutor : public CapabilityExecutor {
public:
  StubExecutor(const std::string &id, int version)
      : id_(ToolId::FromString(id)), version_(version) {}

  ToolId capability_id() const override { return id_; }
  int version() const override { return version_; }

  void Execute(CapabilityRequest request,
               CapabilityCallback callback) override {
    CapabilityOutcome outcome;
    outcome.step.status = StepStatus::kSucceeded;
    std::move(callback).Run(std::move(outcome));
  }

private:
  ToolId id_;
  int version_;
};

ToolDescriptor Descriptor(const std::string &id, int version) {
  ToolDescriptor descriptor;
  descriptor.id = ToolId::FromString(id);
  descriptor.version = version;
  descriptor.name = id;
  descriptor.description = "test descriptor";
  descriptor.provider = "seoul";
  return descriptor;
}

TEST(CapabilityExecutorRegistryTest, RegistersAndFindsByIdAndVersion) {
  CapabilityExecutorRegistry registry;
  EXPECT_TRUE(registry.Register(
      std::make_unique<StubExecutor>("browser.tabs.open", 1)));
  EXPECT_TRUE(registry.Register(
      std::make_unique<StubExecutor>("browser.tabs.open", 2)));
  EXPECT_TRUE(registry.Find(ToolId::FromString("browser.tabs.open"), 1));
  EXPECT_TRUE(registry.Find(ToolId::FromString("browser.tabs.open"), 2));
  EXPECT_FALSE(registry.Find(ToolId::FromString("browser.tabs.open"), 3));
  EXPECT_FALSE(registry.Find(ToolId::FromString("browser.tabs.close"), 1));
}

TEST(CapabilityExecutorRegistryTest, RejectsDuplicatesAndInvalid) {
  CapabilityExecutorRegistry registry;
  EXPECT_TRUE(registry.Register(
      std::make_unique<StubExecutor>("browser.tabs.open", 1)));
  EXPECT_FALSE(registry.Register(
      std::make_unique<StubExecutor>("browser.tabs.open", 1)));
  EXPECT_FALSE(registry.Register(std::make_unique<StubExecutor>("", 1)));
  EXPECT_FALSE(registry.Register(nullptr));
  EXPECT_EQ(registry.size(), 1u);
}

TEST(CapabilityExecutorRegistryTest, ClearDestroysRegisteredExecutors) {
  CapabilityExecutorRegistry registry;
  ASSERT_TRUE(registry.Register(
      std::make_unique<StubExecutor>("browser.tabs.open", 1)));
  ASSERT_EQ(registry.size(), 1u);
  registry.Clear();
  EXPECT_EQ(registry.size(), 0u);
  EXPECT_FALSE(registry.Find(ToolId::FromString("browser.tabs.open"), 1));
}

TEST(CapabilityExecutorRegistryTest, CompletenessReportsBothDirections) {
  CapabilityExecutorRegistry registry;
  ASSERT_TRUE(registry.Register(
      std::make_unique<StubExecutor>("browser.tabs.open", 1)));
  ASSERT_TRUE(
      registry.Register(std::make_unique<StubExecutor>("page.observe", 1)));

  // One descriptor with an executor, one without; one executor orphaned.
  std::vector<ToolDescriptor> descriptors;
  descriptors.push_back(Descriptor("browser.tabs.open", 1));
  descriptors.push_back(Descriptor("browser.tabs.close", 1));

  const CapabilityExecutorRegistry::CompletenessReport report =
      registry.CheckCompleteness(descriptors);
  ASSERT_EQ(report.descriptors_without_executor.size(), 1u);
  EXPECT_EQ(report.descriptors_without_executor[0].value(),
            "browser.tabs.close");
  ASSERT_EQ(report.executors_without_descriptor.size(), 1u);
  EXPECT_EQ(report.executors_without_descriptor[0].value(), "page.observe");
}

TEST(CapabilityExecutorRegistryTest, VersionMismatchIsIncomplete) {
  CapabilityExecutorRegistry registry;
  ASSERT_TRUE(registry.Register(
      std::make_unique<StubExecutor>("browser.tabs.open", 1)));
  std::vector<ToolDescriptor> descriptors;
  descriptors.push_back(Descriptor("browser.tabs.open", 2));
  const CapabilityExecutorRegistry::CompletenessReport report =
      registry.CheckCompleteness(descriptors);
  EXPECT_EQ(report.descriptors_without_executor.size(), 1u);
  EXPECT_EQ(report.executors_without_descriptor.size(), 1u);
}

} // namespace
} // namespace seoul
