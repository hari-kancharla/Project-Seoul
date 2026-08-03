// Project Seoul browser-vetted ad-block replacement resources.

#include "seoul/browser/adblock/ad_block_resource_catalog.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include "base/base64.h"
#include "base/check.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "crypto/sha2.h"

namespace seoul::adblock {
namespace {

struct BundledResource {
  const char* name;
  std::array<const char*, 2> aliases;
  AdBlockResourceType type;
  const char* mime_type;
  const char* base64_body;
  const char* version;
  const char* sha256;
};

constexpr BundledResource kBundledResources[] = {
    {
        "seoul-noop.js",
        {"noop.js", "noopjs"},
        AdBlockResourceType::kMime,
        "application/javascript",
        "KGZ1bmN0aW9uKCkgewogICd1c2Ugc3RyaWN0JzsKfSkoKTsK",
        "1",
        "a6c40a75a40cb29ddd1046bf271a6a5e4480129680f4752de3b1d3f83c25bdf5",
    },
    {
        "seoul-empty.css",
        {"noop.css", "empty.css"},
        AdBlockResourceType::kMime,
        "text/css",
        "LyogU2VvdWwgdmV0dGVkIGVtcHR5IHN0eWxlc2hlZXQuICovCg==",
        "1",
        "e1148969b8cbba7ef848db964624ac620ad402186460097e47c04dd4c1975b2f",
    },
    {
        "seoul-transparent.gif",
        {"1x1.gif", "transparent.gif"},
        AdBlockResourceType::kMime,
        "image/gif",
        "R0lGODlhAQABAAD/ACwAAAAAAQABAAACADs=",
        "1",
        "3b7b8a4b411ddf8db9bacc2f3aabf406f8e4c0c087829b336ca331c40adfdff1",
    },
    {
        "seoul-remove-elements.js",
        {"remove-elements.js", "remove-elements"},
        AdBlockResourceType::kScriptletTemplate,
        "",
        "ZnVuY3Rpb24gc2VvdWxSZW1vdmVFbGVtZW50cyhyYXdTZWxlY3RvcikgewogIGlmICh0eXBlb2YgcmF3U2VsZWN0b3IgIT09ICJzdHJpbmciIHx8IHJhd1NlbGVjdG9yLmxlbmd0aCA9PT0gMCB8fAogICAgICByYXdTZWxlY3Rvci5sZW5ndGggPiA1MTIgfHwgcmF3U2VsZWN0b3IuaW5jbHVkZXMoIlwwIikpIHsKICAgIHJldHVybjsKICB9CiAgbGV0IG5vZGVzOwogIHRyeSB7CiAgICBub2RlcyA9IGRvY3VtZW50LnF1ZXJ5U2VsZWN0b3JBbGwocmF3U2VsZWN0b3IpOwogIH0gY2F0Y2ggewogICAgcmV0dXJuOwogIH0KICBjb25zdCBsaW1pdCA9IE1hdGgubWluKG5vZGVzLmxlbmd0aCwgMjU2KTsKICBmb3IgKGxldCBpbmRleCA9IDA7IGluZGV4IDwgbGltaXQ7ICsraW5kZXgpIHsKICAgIG5vZGVzW2luZGV4XS5yZW1vdmUoKTsKICB9Cn0K",
        "1",
        "ee9cb53227f1bc23ae02ba2d590ea45a0c8b3d9679fdb150b783240f8d2daf6b",
    },
};

std::vector<AdBlockResource> BuildCatalog() {
  std::vector<AdBlockResource> catalog;
  catalog.reserve(std::size(kBundledResources));
  for (const BundledResource& bundled : kBundledResources) {
    std::string body;
    CHECK(base::Base64Decode(bundled.base64_body, &body));
    const std::string digest =
        base::ToLowerASCII(base::HexEncode(crypto::SHA256HashString(body)));
    CHECK_EQ(digest, bundled.sha256);

    AdBlockResource resource;
    resource.name = bundled.name;
    resource.aliases.assign(bundled.aliases.begin(), bundled.aliases.end());
    resource.type = bundled.type;
    resource.mime_type = bundled.mime_type;
    resource.body = std::move(body);
    resource.version = bundled.version;
    resource.sha256 = bundled.sha256;
    if (resource.type == AdBlockResourceType::kMime) {
      resource.data_url = "data:" + resource.mime_type + ";base64," +
                          std::string(bundled.base64_body);
    }
    catalog.push_back(std::move(resource));
  }
  return catalog;
}

std::optional<AdBlockResource> FindByNameOrAlias(
    std::string_view name_or_alias) {
  for (const AdBlockResource& resource : GetAdBlockResourceCatalog()) {
    if (resource.name == name_or_alias ||
        std::ranges::find(resource.aliases, name_or_alias) !=
            resource.aliases.end()) {
      return resource;
    }
  }
  return std::nullopt;
}

}  // namespace

AdBlockResource::AdBlockResource() = default;
AdBlockResource::AdBlockResource(const AdBlockResource&) = default;
AdBlockResource& AdBlockResource::operator=(const AdBlockResource&) = default;
AdBlockResource::AdBlockResource(AdBlockResource&&) = default;
AdBlockResource& AdBlockResource::operator=(AdBlockResource&&) = default;
AdBlockResource::~AdBlockResource() = default;

std::vector<AdBlockResource> GetAdBlockResourceCatalog() {
  return BuildCatalog();
}

std::string SerializeAdBlockResourceCatalog() {
  base::ListValue resources;
  for (const AdBlockResource& resource : GetAdBlockResourceCatalog()) {
    base::DictValue descriptor;
    descriptor.Set("name", resource.name);
    base::ListValue aliases;
    for (const std::string& alias : resource.aliases) {
      aliases.Append(alias);
    }
    descriptor.Set("aliases", std::move(aliases));
    if (resource.type == AdBlockResourceType::kScriptletTemplate) {
      descriptor.Set("kind", "template");
    } else {
      base::DictValue kind;
      kind.Set("mime", resource.mime_type);
      descriptor.Set("kind", std::move(kind));
    }
    descriptor.Set("content", base::Base64Encode(resource.body));
    resources.Append(std::move(descriptor));
  }

  std::optional<std::string> json = base::WriteJson(resources);
  CHECK(json);
  return std::move(*json);
}

std::optional<AdBlockResource> FindAdBlockResourceByDataUrl(
    std::string_view data_url) {
  for (const AdBlockResource& resource : GetAdBlockResourceCatalog()) {
    if (!resource.data_url.empty() && resource.data_url == data_url) {
      return resource;
    }
  }
  return std::nullopt;
}

bool ValidateAdBlockResourceArguments(std::string_view name_or_alias,
                                      base::span<const std::string> arguments) {
  const std::optional<AdBlockResource> resource =
      FindByNameOrAlias(name_or_alias);
  if (!resource) {
    return false;
  }
  if (resource->type == AdBlockResourceType::kMime) {
    return arguments.empty();
  }
  if (resource->name != "seoul-remove-elements.js" ||
      arguments.size() != 1u) {
    return false;
  }
  const std::string& selector = arguments.front();
  return !selector.empty() && selector.size() <= 512 &&
         selector.find('\0') == std::string::npos;
}

}  // namespace seoul::adblock
