// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "seoul/browser/shell/workspace_icon_data.h"

#include <set>
#include <string>

#include "seoul/browser/shell/workspace_emoji_data.h"
#include "seoul/browser/shell/workspace_icon_painter.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/utils/SkParsePath.h"

namespace seoul {
namespace {

TEST(WorkspaceIconDataTest, MatchesCurrentZenSelectableCatalog) {
  const auto icons = WorkspaceBuiltinIconCatalog();
  EXPECT_EQ(icons.size(), 86u);
  std::set<std::string> names;
  for (const WorkspaceBuiltinIconData& icon : icons) {
    EXPECT_TRUE(names.insert(std::string(icon.name)).second);
    const auto parsed = SkParsePath::FromSVGString(icon.svg_path.data());
    ASSERT_TRUE(parsed.has_value()) << icon.name;
    EXPECT_FALSE(parsed->isEmpty()) << icon.name;
    const SkRect bounds = parsed->computeTightBounds();
    // Arc flattening can introduce sub-thousandth floating-point overshoot at
    // an exact 0/512 edge.
    EXPECT_GE(bounds.left(), -0.01f) << icon.name;
    EXPECT_GE(bounds.top(), -0.01f) << icon.name;
    EXPECT_LE(bounds.right(), 512.01f) << icon.name;
    EXPECT_LE(bounds.bottom(), 512.01f) << icon.name;
    EXPECT_EQ(FindWorkspaceBuiltinIcon(icon.name), &icon);
    EXPECT_TRUE(IsWorkspaceBuiltinIcon(WorkspaceBuiltinIconRef(icon.name)));
  }
  EXPECT_FALSE(IsWorkspaceBuiltinIcon("zen-icon:not-in-zen"));
  EXPECT_FALSE(IsWorkspaceBuiltinIcon("🌱"));
}

TEST(WorkspaceIconDataTest, MatchesCurrentZenEmojiCatalog) {
  const auto emojis = WorkspaceEmojiCatalog();
  EXPECT_EQ(emojis.size(), 1915u);
  std::set<std::string> values;
  for (const WorkspaceEmojiData& emoji : emojis) {
    EXPECT_FALSE(emoji.emoji.empty());
    EXPECT_FALSE(emoji.search_terms.empty());
    EXPECT_TRUE(values.insert(std::string(emoji.emoji)).second) << emoji.emoji;
  }
}

}  // namespace
}  // namespace seoul
