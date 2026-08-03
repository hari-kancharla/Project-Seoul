// Project Seoul native blocker engine.

#include "seoul/browser/adblock/ad_block_engine.h"

#include <utility>

#include "base/check.h"
#include "seoul/browser/adblock/ad_block_resource_catalog.h"
#include "seoul/browser/adblock/rs/src/lib.rs.h"

namespace seoul::adblock {
namespace {

std::string ToString(const rust::String& value) {
  return std::string(value.data(), value.size());
}

std::optional<std::string> ToOptionalString(
    const adblock_rs::OptionalString& value) {
  if (!value.has_value) {
    return std::nullopt;
  }
  return ToString(value.value);
}

std::vector<std::string> ToStringVector(const rust::Vec<rust::String>& values) {
  std::vector<std::string> result;
  result.reserve(values.size());
  for (const auto& value : values) {
    result.push_back(ToString(value));
  }
  return result;
}

}  // namespace

AdBlockMatchResult::AdBlockMatchResult() = default;
AdBlockMatchResult::AdBlockMatchResult(const AdBlockMatchResult&) = default;
AdBlockMatchResult& AdBlockMatchResult::operator=(const AdBlockMatchResult&) =
    default;
AdBlockMatchResult::AdBlockMatchResult(AdBlockMatchResult&&) = default;
AdBlockMatchResult& AdBlockMatchResult::operator=(AdBlockMatchResult&&) =
    default;

AdBlockMatchResult::AdBlockMatchResult(
    bool matched,
    bool important,
    bool has_exception,
    std::optional<std::string> matched_rule,
    std::optional<std::string> exception_rule,
    std::optional<std::string> redirect,
    std::optional<std::string> rewritten_url)
    : matched(matched),
      important(important),
      has_exception(has_exception),
      matched_rule(std::move(matched_rule)),
      exception_rule(std::move(exception_rule)),
      redirect(std::move(redirect)),
      rewritten_url(std::move(rewritten_url)) {}

AdBlockMatchResult::~AdBlockMatchResult() = default;

AdBlockCosmeticEngineResources::AdBlockCosmeticEngineResources() = default;
AdBlockCosmeticEngineResources::AdBlockCosmeticEngineResources(
    const AdBlockCosmeticEngineResources&) = default;
AdBlockCosmeticEngineResources& AdBlockCosmeticEngineResources::operator=(
    const AdBlockCosmeticEngineResources&) = default;
AdBlockCosmeticEngineResources::AdBlockCosmeticEngineResources(
    AdBlockCosmeticEngineResources&&) = default;
AdBlockCosmeticEngineResources& AdBlockCosmeticEngineResources::operator=(
    AdBlockCosmeticEngineResources&&) = default;
AdBlockCosmeticEngineResources::~AdBlockCosmeticEngineResources() = default;

class AdBlockEngine::Impl {
 public:
  explicit Impl(rust::Box<adblock_rs::Engine> engine)
      : engine_(std::move(engine)) {}

  rust::Box<adblock_rs::Engine> engine_;
};

// static
std::unique_ptr<AdBlockEngine> AdBlockEngine::Create(
    base::span<const uint8_t> rules,
    std::string* error) {
  CHECK(error);
  error->clear();

  std::vector<uint8_t> owned_rules(rules.begin(), rules.end());
  const std::string resources_json = SerializeAdBlockResourceCatalog();
  adblock_rs::EngineBuildResult result =
      adblock_rs::build_engine(owned_rules, resources_json);
  if (result.status != adblock_rs::BuildStatus::Success) {
    *error = ToString(result.error_message);
    return nullptr;
  }

  return std::unique_ptr<AdBlockEngine>(
      new AdBlockEngine(std::make_unique<Impl>(std::move(result.value))));
}

AdBlockEngine::AdBlockEngine(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

AdBlockEngine::~AdBlockEngine() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

AdBlockMatchResult AdBlockEngine::Evaluate(
    const AdBlockRequest& request,
    bool previously_matched_rule,
    bool previously_matched_exception) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const adblock_rs::MatchResult result = impl_->engine_->matches(
      request.url, request.hostname, request.source_hostname,
      request.request_type, request.is_third_party,
      previously_matched_rule || previously_matched_exception,
      !previously_matched_exception);
  return AdBlockMatchResult(result.matched, result.important,
                            result.has_exception,
                            ToOptionalString(result.matched_rule),
                            ToOptionalString(result.exception_rule),
                            ToOptionalString(result.redirect),
                            ToOptionalString(result.rewritten_url));
}

AdBlockCosmeticEngineResources AdBlockEngine::GetUrlCosmeticResources(
    const std::string& url) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const adblock_rs::CosmeticResources resources =
      impl_->engine_->url_cosmetic_resources(url);
  AdBlockCosmeticEngineResources result;
  result.hide_selectors = ToStringVector(resources.hide_selectors);
  result.exceptions = ToStringVector(resources.exceptions);
  result.isolated_script = ToString(resources.isolated_script);
  result.procedural_actions = ToStringVector(resources.procedural_actions);
  result.generichide = resources.generichide;
  return result;
}

std::vector<std::string> AdBlockEngine::GetHiddenClassIdSelectors(
    const std::vector<std::string>& classes,
    const std::vector<std::string>& ids,
    const std::vector<std::string>& exceptions) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return ToStringVector(
      impl_->engine_->hidden_class_id_selectors(classes, ids, exceptions));
}

std::vector<uint8_t> AdBlockEngine::Serialize() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  rust::Vec<uint8_t> serialized = impl_->engine_->serialize();
  return std::vector<uint8_t>(serialized.begin(), serialized.end());
}

bool AdBlockEngine::Deserialize(base::span<const uint8_t> serialized) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<uint8_t> owned_serialized(serialized.begin(), serialized.end());
  return impl_->engine_->deserialize(owned_serialized);
}

}  // namespace seoul::adblock
