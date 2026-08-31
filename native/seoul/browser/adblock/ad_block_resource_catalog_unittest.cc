// Project Seoul browser-vetted replacement resource tests.

#include "seoul/browser/adblock/ad_block_resource_catalog.h"

#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include <set>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace seoul::adblock {
namespace {

TEST(AdBlockResourceCatalogTest, HasStableVettedResourcesAndAliases) {
  const std::vector<AdBlockResource> catalog = GetAdBlockResourceCatalog();
  ASSERT_EQ(20u, catalog.size());
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
  ASSERT_EQ(20u, value->GetList().size());
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

  // The DOM scriptlets: each argument list is vetted per scriptlet.
  EXPECT_TRUE(ValidateAdBlockResourceArguments(
      "ra.js", std::vector<std::string>{"data-track"}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments(
      "remove-attr.js", std::vector<std::string>{"data-track", ".card"}));
  EXPECT_FALSE(ValidateAdBlockResourceArguments("ra.js", {}));
  EXPECT_FALSE(ValidateAdBlockResourceArguments(
      "ra.js", std::vector<std::string>{std::string(129, 'a')}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments(
      "rc.js", std::vector<std::string>{"promoted"}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments(
      "set-attr.js", std::vector<std::string>{".player", "autoplay"}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments(
      "set-attr.js", std::vector<std::string>{".player", "muted", ""}));
  EXPECT_FALSE(ValidateAdBlockResourceArguments(
      "set-attr.js", std::vector<std::string>{".player"}));

  // The canonical spellings the upstream lists actually write. A rule naming a
  // resource that does not resolve is not a no-op: the engine reports a match
  // with no redirect and the request is hard-blocked, so an unregistered alias
  // breaks the page the stub exists to keep working.
  EXPECT_TRUE(ValidateAdBlockResourceArguments("googletagservices_gpt.js", {}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments("amazon_apstag.js", {}));
  EXPECT_TRUE(
      ValidateAdBlockResourceArguments("googlesyndication_adsbygoogle.js", {}));
  EXPECT_TRUE(
      ValidateAdBlockResourceArguments("google-analytics_analytics.js", {}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments("google-analytics_ga.js", {}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments("1x1-transparent.gif", {}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments("noopcss", {}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments("googletagmanager_gtm.js", {}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments("google-ima.js", {}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments("noop.json", {}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments("2x2.png", {}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments("32x32.png", {}));

  // Redirect stubs accept no arguments, exactly like the other MIME bodies.
  EXPECT_TRUE(ValidateAdBlockResourceArguments("ga.js", {}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments("gpt.js", {}));
  EXPECT_TRUE(ValidateAdBlockResourceArguments("adsbygoogle.js", {}));
  EXPECT_FALSE(ValidateAdBlockResourceArguments(
      "ga.js", std::vector<std::string>{"arg"}));
}

// Every bundled body must decode, hash-verify, and carry a resolvable alias
// set - the CHECKs in BuildCatalog enforce the hashes, this pins the shape.
TEST(AdBlockResourceCatalogTest, EveryResourceIsWellFormed) {
  const std::vector<AdBlockResource> catalog = GetAdBlockResourceCatalog();
  for (const AdBlockResource& resource : catalog) {
    EXPECT_FALSE(resource.name.empty());
    EXPECT_FALSE(resource.body.empty() && resource.mime_type != "text/plain")
        << resource.name;
    EXPECT_GE(resource.aliases.size(), 2u) << resource.name;
    if (resource.type == AdBlockResourceType::kMime) {
      EXPECT_FALSE(resource.mime_type.empty()) << resource.name;
      EXPECT_FALSE(resource.data_url.empty()) << resource.name;
    } else {
      EXPECT_TRUE(resource.data_url.empty()) << resource.name;
    }
  }
}

// Every name and alias in the catalogue must be unique across the whole
// catalogue. The engine's resource storage rejects a resource whose identifier
// is already taken and the failure is swallowed, so a single duplicated alias
// would silently delete an entire stub with no signal anywhere - the resource
// would simply stop resolving and its rules would start hard-blocking.
TEST(AdBlockResourceCatalogTest, EveryIdentifierIsUniqueAcrossTheCatalog) {
  std::set<std::string> seen;
  for (const AdBlockResource& resource : GetAdBlockResourceCatalog()) {
    EXPECT_TRUE(seen.insert(resource.name).second)
        << "duplicate resource name: " << resource.name;
    for (const std::string& alias : resource.aliases) {
      EXPECT_FALSE(alias.empty()) << resource.name;
      EXPECT_TRUE(seen.insert(alias).second)
          << "duplicate alias '" << alias << "' on " << resource.name;
    }
  }
}

}  // namespace
}  // namespace seoul::adblock
