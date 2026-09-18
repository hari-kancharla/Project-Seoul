# Filter and scriptlet provenance

Updated September 7, 2026 against the actual catalogue and vendored resources.
This records dependencies and release work; it is not a legal clearance for
binary distribution. The earlier document's claims that uBlock/Brave lists were
off by default and that runtime delivery removed licensing obligations were
incorrect and have been removed.

## Current delivery

| Content | Delivery and activation | Recorded upstream terms |
| --- | --- | --- |
| Seoul baseline | Bundled, enabled by default; network, cosmetic and narrow YouTube scriptlet rules | MPL-2.0 |
| EasyList and EasyPrivacy | Runtime download, enabled by default | GPL-3.0-or-later OR CC-BY-SA-3.0 |
| uBlock Origin filter set, including quick fixes and unbreak | Runtime download, enabled by default in the default engine | GPL-3.0-only in the catalogue |
| Brave's own `brave-unbreak.txt` | Runtime download, enabled by default in the additional engine | MPL-2.0 in the catalogue |
| Regional EasyList-family lists | Runtime download, activated by configured profile languages | Per-entry terms and attribution recorded in the catalogue |
| uBlock scriptlet implementations from Brave's fork | Vendored source and generated resource bundle | GPL-3.0-or-later, retained source notices and full license |

The catalogue is `native/seoul/browser/adblock/ad_block_filter_catalog.cc`.
Runtime rule files are distinct from the JavaScript resource implementations:
the latter are now bundled in the browser. The Seoul baseline includes
first-party YouTube cosmetic/scriptlet rules; it is no longer accurate to
describe the entire baseline as third-party-only.

## Scriptlet source and build record

`native/seoul/third_party/ublock_scriptlets/source.json` pins
`brave/uBlock` at `1fd62ee6c5a3eb89cf88e11b448eb20875ca7790`. The manifest contains a
SHA-256 for every imported source file. `generate.mjs --check` verifies those
inputs and the generated resource output. Original notices and `LICENSE.txt`
are retained. Runtime subscriptions receive no trusted-scriptlet permissions;
vendoring an implementation does not grant arbitrary filter feeds access to it.

## Distribution gate

Before distributing builds, inventory the actual bundled output, preserve its
notices, and provide the source/license materials required by the selected
upstream terms. Confirm compatibility with the intended Seoul distribution
model, including the generated JavaScript resource bundle. Runtime downloads
do not themselves establish an exemption from applicable terms. This revision
has not completed a distribution compliance review.

## Primary sources

- [EasyList licensing](https://easylist.to/pages/licence.html)
- [uAssets license](https://github.com/uBlockOrigin/uAssets/blob/master/LICENSE)
- [Brave filter repository](https://github.com/brave/adblock-lists)
- [Pinned scriptlet source](https://github.com/brave/uBlock/tree/1fd62ee6c5a3eb89cf88e11b448eb20875ca7790)
