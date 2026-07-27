// Copyright 2026 The Project Seoul Authors

#include "seoul/browser/shell/essential_grid_layout.h"

#include <array>

#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

TEST(EssentialGridLayoutTest, ExpandedColumnsMatchZenWrapping) {
  constexpr std::array<size_t, 13> kExpectedColumns = {
      0, 1, 2, 3, 4, 3, 3, 4, 4, 3, 4, 4, 4,
  };
  for (size_t count = 0; count < kExpectedColumns.size(); ++count) {
    EXPECT_EQ(EssentialColumnsForVisibleCount(count, false),
              kExpectedColumns.at(count))
        << "count " << count;
  }
}

TEST(EssentialGridLayoutTest, CollapsedDeckIsOneColumn) {
  EXPECT_EQ(EssentialColumnsForVisibleCount(0, true), 0u);
  for (size_t count = 1; count <= 12; ++count) {
    EXPECT_EQ(EssentialColumnsForVisibleCount(count, true), 1u)
        << "count " << count;
  }
}

TEST(EssentialGridLayoutTest, LegacyOverflowKeepsFourColumnVisibleDeck) {
  EXPECT_EQ(EssentialColumnsForVisibleCount(13, false), 4u);
  EXPECT_EQ(EssentialColumnsForVisibleCount(100, false), 4u);
}

TEST(EssentialGridLayoutTest, HighlightRequiresActiveTabInCurrentWindow) {
  EXPECT_TRUE(ShouldHighlightEssential(true, true));
  EXPECT_FALSE(ShouldHighlightEssential(true, false));
  EXPECT_FALSE(ShouldHighlightEssential(false, true));
  EXPECT_FALSE(ShouldHighlightEssential(false, false));
}

}  // namespace
}  // namespace seoul
