// Chromium registry-backed domain resolver for adblock-rust.

#include "seoul/browser/adblock/ad_block_domain_resolver.h"

#include <cstdint>
#include <string>

#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "seoul/browser/adblock/rs/src/lib.rs.h"

namespace seoul::adblock_rs {

DomainPosition resolve_domain_position(const std::string& host) {
  const std::string domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          host,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  const size_t match =
      domain.empty() ? std::string::npos : host.rfind(domain);

  DomainPosition position;
  if (match == std::string::npos) {
    position.start = 0;
    position.end = static_cast<uint32_t>(host.size());
  } else {
    position.start = static_cast<uint32_t>(match);
    position.end = static_cast<uint32_t>(match + domain.size());
  }
  return position;
}

}  // namespace seoul::adblock_rs
