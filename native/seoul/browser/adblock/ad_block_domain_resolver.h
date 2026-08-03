// Chromium registry-backed domain resolver for adblock-rust.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_DOMAIN_RESOLVER_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_DOMAIN_RESOLVER_H_

#include <string>

namespace seoul::adblock_rs {

struct DomainPosition;

DomainPosition resolve_domain_position(const std::string& host);

}  // namespace seoul::adblock_rs

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_DOMAIN_RESOLVER_H_
