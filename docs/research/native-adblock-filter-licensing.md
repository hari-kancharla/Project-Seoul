# Filter-list licensing and delivery decisions

Every licence below was read from the project's own published terms on
2026-08-07, not inferred from the fact that another browser ships the list.
The catalogue that these decisions govern is
`native/seoul/browser/adblock/ad_block_filter_catalog.cc`.

**No filter-list contents are vendored into this repository.** Seoul ships only
rules it wrote itself; everything else is fetched from the upstream project at
runtime.

## Sources consulted

| List | Source consulted | Licence found |
| --- | --- | --- |
| EasyList, EasyPrivacy | <https://easylist.to/pages/licence.html> | Dual: GPL-3.0-or-later **or** CC-BY-SA-3.0 |
| uBlock Origin filters | <https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/LICENSE> | GNU GPL v3, 29 June 2007 |
| Brave site compatibility | <https://github.com/brave/adblock-lists> | MPL-2.0 (repository) |

## Decisions

### EasyList / EasyPrivacy — runtime download, enabled by default

Dual-licensed, so redistribution *is* permitted under either licence, subject to
crediting "The EasyList authors" and to CC-BY-SA's share-alike condition.

Seoul fetches these at runtime rather than bundling them. This is a
**conservative choice, not a licensing impossibility**: bundling would be
permitted with compliance, but runtime delivery keeps the share-alike and
attribution obligations off the shipped binary and leaves the upstream project
as the authoritative source. Attribution is recorded in the catalogue so the UI
can display it wherever the lists are surfaced.

### uBlock Origin filters — runtime download, off by default

GPL-3.0-only. Redistribution is permitted under GPLv3 terms. Off by default
because it overlaps EasyList heavily; it is a user opt-in and therefore lands in
the *additional* engine, where it cannot silently alter vetted default
protection.

### Brave site compatibility — runtime download, off by default

`brave/adblock-lists` is MPL-2.0, but the repository states that individual
lists it contains may carry their own upstream licences. Only Brave's own
`brave-unbreak.txt` is catalogued; the aggregated feeds that repository
republishes are deliberately **not** catalogued, because their licensing is that
of their original authors and has not been individually verified here.

### Lists deliberately not used

Any list whose redistribution terms were not verifiable from the project's own
published terms is absent from the catalogue. Absence here means "not
verified", not "not permitted" — adding one requires reading its licence first
and recording it in the table above.

## Seoul baseline — bundled

`native/seoul/browser/adblock/filters/seoul-baseline.txt` is authored by Project
Seoul and licensed MPL-2.0 with the rest of the tree. It is the only rule
content shipped inside the signed component. It exists so that a first run with
no network still blocks something, and so a rejected or failed update always has
a known-good floor to fall back to. It is deliberately small and limited to
third-party matches so it cannot break first-party site functionality.
