// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#ifndef SEOUL_BROWSER_ADBLOCK_AD_BLOCK_BASELINE_RULES_H_
#define SEOUL_BROWSER_ADBLOCK_AD_BLOCK_BASELINE_RULES_H_

namespace seoul::adblock {

// The Seoul-authored safety floor, compiled into the binary.
//
// This is what the browser blocks with before any list arrives: on a first run
// with no network, on a profile whose cache is damaged, and in every build that
// has no release component identity configured - which is every development
// build, because `seoul_adblock_component_public_key_hash` is empty by default
// and registration is deliberately skipped rather than trusting a placeholder.
//
// It was previously a single rule against `seoul-adblock.invalid`, a domain
// that does not exist. That is a correct placeholder for a unit test and a
// browser that blocks nothing at all in the hands of a user, which is what it
// was. The list below is the smallest set that makes the shipped default
// honest.
//
// Scope, deliberately narrow. These are third-party hosts whose only purpose is
// advertising, ad auctions, or cross-site measurement. Everything here is
// `$third-party`, so a site serving its own analytics from its own domain is
// untouched. Nothing here is a CDN, a login provider, a payment processor, a
// consent manager, a comment system, or a video host: blocking one of those
// breaks pages, and a blocker that breaks pages gets turned off, at which point
// it protects nobody.
//
// This is a floor, not list parity. It is Seoul-authored precisely so that it
// carries no third-party licence obligation into the binary; EasyList,
// EasyPrivacy and friends remain the job of the signed component channel, and
// `docs/research/native-adblock-implementation.md` is explicit that Brave
// parity must not be claimed until that channel exists.
inline constexpr char kSeoulBaselineDefaultRules[] =
    "! Seoul baseline protection - Seoul-authored, MPL-2.0.\n"
    "! Third-party advertising, ad-auction and cross-site measurement hosts.\n"
    "! Broader coverage arrives through the verified component channel.\n"
    "!\n"
    "! --- Ad serving and ad exchanges ---\n"
    "||doubleclick.net^$third-party\n"
    "||googlesyndication.com^$third-party\n"
    "||googletagservices.com^$third-party\n"
    "||adservice.google.com^$third-party\n"
    "||adnxs.com^$third-party\n"
    "||rubiconproject.com^$third-party\n"
    "||pubmatic.com^$third-party\n"
    "||openx.net^$third-party\n"
    "||casalemedia.com^$third-party\n"
    "||criteo.com^$third-party\n"
    "||criteo.net^$third-party\n"
    "||amazon-adsystem.com^$third-party\n"
    "||33across.com^$third-party\n"
    "||sharethrough.com^$third-party\n"
    "||smartadserver.com^$third-party\n"
    "||adform.net^$third-party\n"
    "||teads.tv^$third-party\n"
    "!\n"
    "! --- Content recommendation widgets sold as advertising ---\n"
    "||taboola.com^$third-party\n"
    "||outbrain.com^$third-party\n"
    "!\n"
    "! --- Cross-site measurement, audience and viewability ---\n"
    "||google-analytics.com^$third-party\n"
    "||googletagmanager.com^$third-party\n"
    "||scorecardresearch.com^$third-party\n"
    "||quantserve.com^$third-party\n"
    "||moatads.com^$third-party\n"
    "||adsafeprotected.com^$third-party\n"
    "||demdex.net^$third-party\n"
    "||everesttech.net^$third-party\n"
    "||bluekai.com^$third-party\n"
    "||agkn.com^$third-party\n"
    "||crwdcntrl.net^$third-party\n"
    "||exelator.com^$third-party\n"
    "!\n"
    "! --- Session replay and behavioural capture ---\n"
    "||hotjar.com^$third-party\n"
    "||fullstory.com^$third-party\n"
    "||mouseflow.com^$third-party\n"
    "||inspectlet.com^$third-party\n"
    "!\n"
    "! --- Named endpoints rather than whole hosts, where the host also serves\n"
    "! --- functionality that must keep working.\n"
    "||facebook.com/tr^$third-party\n"
    "||linkedin.com/px^$third-party\n"
    "!\n"
    "! --- Common third-party beacon and pixel paths ---\n"
    "||*/collect?v=*$image,third-party\n"
    "||*/pixel.gif$image,third-party\n"
    "||*/beacon.js$script,third-party\n";

// The additional-engine floor. Empty of rules on purpose: the additional engine
// carries opt-in subscriptions and user rules, and shipping defaults there
// would make an opt-in surface non-empty before the user has opted into
// anything.
inline constexpr char kSeoulBaselineAdditionalRules[] =
    "! Seoul user/additional rule baseline.\n";

}  // namespace seoul::adblock

#endif  // SEOUL_BROWSER_ADBLOCK_AD_BLOCK_BASELINE_RULES_H_
