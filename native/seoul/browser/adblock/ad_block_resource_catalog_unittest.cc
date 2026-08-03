// Project Seoul browser-vetted replacement resource tests.

#include "seoul/browser/adblock/ad_block_resource_catalog.h"

#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul::adblock {
namespace {

TEST(AdBlockResourceCatalogTest, HasStableVettedResourcesAndAliases) {
  const std::vector<AdBlockResource> catalog = GetAdBlockResourceCatalog();
  ASSERT_EQ(4u, catalog.size());
  EXPECT_EQ("seoul-noop.js", catalog[0].name);
  EXPECT_EQ("application/javascript", catalog[0].mime_type);
  EXPECT_EQ("a6c40a75a40cb29ddd1046bf271a6a5e4480129680f4752de3b1d3f83c25bdf5",
            catalog[0].sha256);
  EXPECT_EQ("1", catalog[0].version);
  const std::optional<AdBlockResource> resolved =
      FindAdBlockResourceByDataUrl(catalog[0].data_url);
  ASSERT_TRUE(resolved);
  EXPECT_EQ(catalog[0].name, resolved->name);
  EXPECT_FALSE(FindAdBlockResourceByDataUrl(
      "data:application/javascript;base64,YXJiaXRyYXJ5"));
  EXPECT_EQ(AdBlockResourceType::kScriptletTemplate, catalog[3].type);
  EXPECT_EQ("seoul-remove-elements.js", catalog[3].name);
  EXPECT_TRUE(catalog[3].data_url.empty());
}

TEST(AdBlockResourceCatalogTest, SerializesAsAdblockRustResources) {
  const std::optional<base::Value> value = base::JSONReader::Read(
      SerializeAdBlockResourceCatalog(), base::JSON_PARSE_RFC);
  ASSERT_TRUE(value);
  ASSERT_TRUE(value->is_list());
  ASSERT_EQ(4u, value->GetList().size());
  EXPECT_EQ("seoul-noop.js", *value->GetList()[0].GetDict().FindString("name"));
  EXPECT_EQ(
      "application/javascript",
      *value->GetList()[0].GetDict().FindDict("kind")->FindString("mime"));
  EXPECT_EQ("template",
            *value->GetList()[3].GetDict().FindString("kind"));
}

TEST(AdBlockResourceCatalogTest, ArgumentsFailClosed) {
  EXPECT_TRUE(ValidateAdBlockResourceArguments("seoul-noop.js", {}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments("noopjs", {}));
  const std::vector<std::string> argument{"unexpected"};
  EXPECT_FALSE(ValidateAdBlockResourceArguments("noopjs", argument));
  EXPECT_TRUE(ValidateAdBlockResourceArguments(
      "remove-elements", std::vector<std::string>{".sponsored"}));
  EXPECT_FALSE(ValidateAdBlockResourceArguments("remove-elements", {}));
  EXPECT_FALSE(ValidateAdBlockResourceArguments(
      "remove-elements", std::vector<std::string>{std::string(513, 'x')}));
  EXPECT_FALSE(ValidateAdBlockResourceArguments("unknown.js", {}));
}

}  // namespace
}  // namespace seoul::adblock
