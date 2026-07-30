#!/usr/bin/env node
// Generates the exact current Zen Space-picker SVG paths and emoji catalog as
// native, offline Chromium data. Run from the ProjectSeoul repository root:
//   node native/scripts/generate_zen_workspace_icon_data.mjs \
//     /path/to/zen-browser/desktop

import fs from "node:fs";
import path from "node:path";

const zenRoot = path.resolve(process.argv[2] ?? "");
if (!zenRoot || !fs.existsSync(zenRoot)) {
  throw new Error("Pass the root of a zen-browser/desktop checkout");
}

const projectRoot = path.resolve(import.meta.dirname, "..", "..");
const outputRoot = path.join(
  projectRoot,
  "native/seoul/browser/shell",
);
const selectableRoot = path.join(
  zenRoot,
  "src/browser/themes/shared/zen-icons/common/selectable",
);
const emojiDataPath = path.join(
  zenRoot,
  "src/zen/common/emojis/ZenEmojisData.min.mjs",
);

// Keep this list identical to SVG_ICONS in ZenEmojiPicker.mjs. The selectable
// source directory also contains logo-github.svg, which the current picker
// intentionally does not expose.
const iconFiles = [
  "airplane.svg", "american-football.svg", "baseball.svg", "basket.svg",
  "bed.svg", "bell.svg", "bookmark.svg", "book.svg",
  "briefcase.svg", "brush.svg", "bug.svg", "build.svg",
  "cafe.svg", "call.svg", "card.svg", "chat.svg",
  "checkbox.svg", "circle.svg", "cloud.svg", "code.svg",
  "coins.svg", "construct.svg", "cutlery.svg", "egg.svg",
  "extension-puzzle.svg", "eye.svg", "fast-food.svg", "fish.svg",
  "flag.svg", "flame.svg", "flask.svg", "folder.svg",
  "game-controller.svg", "globe-1.svg", "globe.svg", "grid-2x2.svg",
  "grid-3x3.svg", "heart.svg", "ice-cream.svg", "image.svg",
  "inbox.svg", "key.svg", "layers.svg", "leaf.svg",
  "lightning.svg", "location.svg", "lock-closed.svg", "logo-rss.svg",
  "logo-usd.svg", "mail.svg", "map.svg", "megaphone.svg",
  "moon.svg", "music.svg", "navigate.svg", "nuclear.svg",
  "page.svg", "palette.svg", "paw.svg", "people.svg",
  "pizza.svg", "planet.svg", "present.svg", "rocket.svg",
  "school.svg", "shapes.svg", "shirt.svg", "skull.svg",
  "squares.svg", "square.svg", "star-1.svg", "star.svg",
  "stats-chart.svg", "sun.svg", "tada.svg", "terminal.svg",
  "ticket.svg", "time.svg", "trash.svg", "triangle.svg",
  "video.svg", "volume-high.svg", "wallet.svg", "warning.svg",
  "water.svg", "weight.svg",
];

function cppString(value) {
  return JSON.stringify(value)
    .replaceAll("\\u2028", "\\u2028")
    .replaceAll("\\u2029", "\\u2029");
}

const icons = iconFiles.map((file) => {
  const source = fs.readFileSync(path.join(selectableRoot, file), "utf8");
  const viewBox = source.match(/viewBox="([^"]+)"/)?.[1];
  const paths = [...source.matchAll(/<path\s+[^>]*d="([^"]+)"[^>]*\/?>/g)];
  if (viewBox !== "0 0 512 512" || paths.length !== 1) {
    throw new Error(`Unexpected selectable SVG structure: ${file}`);
  }
  return {
    name: file.slice(0, -".svg".length),
    path: paths[0][1],
  };
});

const emojiSource = fs.readFileSync(emojiDataPath, "utf8");
const emojiJson = emojiSource.slice(
  emojiSource.indexOf("["),
  emojiSource.lastIndexOf("]") + 1,
);
const emojis = JSON.parse(emojiJson);
if (!Array.isArray(emojis) || emojis.length < 1000) {
  throw new Error("Unexpected Zen emoji catalog");
}
for (const item of emojis) {
  if (
    typeof item.emoji !== "string" ||
    !Array.isArray(item.tags) ||
    !item.tags.every((tag) => typeof tag === "string")
  ) {
    throw new Error("Malformed Zen emoji record");
  }
}

const license = `// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from zen-browser/desktop at
// e474f1df3ab0c7e7d271011ae49f3eea494eb3f7. Do not edit by hand.
`;

const iconHeader = `${license}
#ifndef SEOUL_BROWSER_SHELL_WORKSPACE_ICON_DATA_H_
#define SEOUL_BROWSER_SHELL_WORKSPACE_ICON_DATA_H_

#include <span>
#include <string_view>

namespace seoul {

struct WorkspaceBuiltinIconData {
  std::string_view name;
  std::string_view svg_path;
};

std::span<const WorkspaceBuiltinIconData> WorkspaceBuiltinIconCatalog();
const WorkspaceBuiltinIconData* FindWorkspaceBuiltinIcon(
    std::string_view name);

}  // namespace seoul

#endif  // SEOUL_BROWSER_SHELL_WORKSPACE_ICON_DATA_H_
`;

const iconRows = icons
  .map(
    ({ name, path: svgPath }) =>
      `    {${cppString(name)}, ${cppString(svgPath)}},`,
  )
  .join("\n");
const iconSource = `${license}
#include "seoul/browser/shell/workspace_icon_data.h"

#include <array>

#include "base/containers/span.h"

namespace seoul {
namespace {

constexpr std::array<WorkspaceBuiltinIconData, ${icons.length}> kIcons = {{
${iconRows}
}};

}  // namespace

std::span<const WorkspaceBuiltinIconData> WorkspaceBuiltinIconCatalog() {
  return base::span(kIcons);
}

const WorkspaceBuiltinIconData* FindWorkspaceBuiltinIcon(
    std::string_view name) {
  for (const WorkspaceBuiltinIconData& icon : kIcons) {
    if (icon.name == name) {
      return &icon;
    }
  }
  return nullptr;
}

}  // namespace seoul
`;

const emojiHeader = `${license}
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
`;

const emojiRows = emojis
  .map(
    ({ emoji, tags }) =>
      `    {${cppString(emoji)}, ${cppString(tags.join(" ").toLowerCase())}},`,
  )
  .join("\n");
const emojiOutputSource = `${license}
#include "seoul/browser/shell/workspace_emoji_data.h"

#include <array>

#include "base/containers/span.h"

namespace seoul {
namespace {

constexpr std::array<WorkspaceEmojiData, ${emojis.length}> kEmojis = {{
${emojiRows}
}};

}  // namespace

std::span<const WorkspaceEmojiData> WorkspaceEmojiCatalog() {
  return base::span(kEmojis);
}

}  // namespace seoul
`;

fs.writeFileSync(path.join(outputRoot, "workspace_icon_data.h"), iconHeader);
fs.writeFileSync(path.join(outputRoot, "workspace_icon_data.cc"), iconSource);
fs.writeFileSync(path.join(outputRoot, "workspace_emoji_data.h"), emojiHeader);
fs.writeFileSync(
  path.join(outputRoot, "workspace_emoji_data.cc"),
  emojiOutputSource,
);

console.log(`Generated ${icons.length} selectable icons and ${emojis.length} emojis.`);
