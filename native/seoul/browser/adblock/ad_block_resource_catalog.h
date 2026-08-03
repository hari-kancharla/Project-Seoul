// Project Seoul browser-vetted ad-block replacement resources.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_RESOURCE_CATALOG_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_RESOURCE_CATALOG_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"

namespace seoul::adblock {

enum class AdBlockResourceType {
  kMime,
  kScriptletTemplate,
};

// A replacement resource compiled into the browser. Filter lists can select a
// resource by its stable name or an alias, but cannot provide its MIME type or
// implementation.
struct AdBlockResource {
  AdBlockResource();
  AdBlockResource(const AdBlockResource&);
  AdBlockResource& operator=(const AdBlockResource&);
  AdBlockResource(AdBlockResource&&);
  AdBlockResource& operator=(AdBlockResource&&);
  ~AdBlockResource();

  std::string name;
  std::vector<std::string> aliases;
  AdBlockResourceType type = AdBlockResourceType::kMime;
  std::string mime_type;
  std::string body;
  std::string version;
  std::string sha256;
  std::string data_url;
};

// Returns the small immutable-value catalog after verifying every bundled body
// against its declared SHA-256 digest.
std::vector<AdBlockResource> GetAdBlockResourceCatalog();

// Produces the adblock-rust Resource JSON for the immutable catalog.
std::string SerializeAdBlockResourceCatalog();

// Resolves an engine-produced data URL only when it exactly matches one of the
// browser-vetted catalog entries.
std::optional<AdBlockResource> FindAdBlockResourceByDataUrl(
    std::string_view data_url);

// MIME resources accept no parameters. Scriptlet templates apply their own
// narrow argument schema before touching the isolated-world document.
bool ValidateAdBlockResourceArguments(std::string_view name_or_alias,
                                      base::span<const std::string> arguments);

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_RESOURCE_CATALOG_H_
