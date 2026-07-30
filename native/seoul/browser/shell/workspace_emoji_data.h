// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from zen-browser/desktop at
// e474f1df3ab0c7e7d271011ae49f3eea494eb3f7. Do not edit by hand.

#ifndef SEOUL_BROWSER_SHELL_WORKSPACE_EMOJI_DATA_H_
#define SEOUL_BROWSER_SHELL_WORKSPACE_EMOJI_DATA_H_

#include <span>
#include <string_view>

namespace seoul {

struct WorkspaceEmojiData {
  std::string_view emoji;
  std::string_view search_terms;
};

std::span<const WorkspaceEmojiData> WorkspaceEmojiCatalog();

}  // namespace seoul

#endif  // SEOUL_BROWSER_SHELL_WORKSPACE_EMOJI_DATA_H_
