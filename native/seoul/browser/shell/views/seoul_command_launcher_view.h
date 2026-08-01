// Project Seoul native browser shell V0.

#ifndef SEOUL_BROWSER_SHELL_VIEWS_SEOUL_COMMAND_LAUNCHER_VIEW_H_
#define SEOUL_BROWSER_SHELL_VIEWS_SEOUL_COMMAND_LAUNCHER_VIEW_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "seoul/browser/shell/command_launcher_catalog.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/views/view.h"

namespace views {
class Button;
class ScrollView;
} // namespace views

namespace seoul {

class ShellController;

// The result portion of Seoul's unified floating omnibox. The text field
// remains Chromium's real OmniboxViewViews; this view is inserted directly
// below that field by BrowserView so both pieces share one background, shadow
// and focus lifecycle.
class SeoulOmniboxActionView final : public views::View {
public:
  METADATA_HEADER(SeoulOmniboxActionView, views::View)

public:
  using ExecuteCallback =
      base::RepeatingCallback<void(CommandLauncherEntry entry)>;

  SeoulOmniboxActionView(ShellController *controller,
                         ExecuteCallback execute_callback);
  SeoulOmniboxActionView(const SeoulOmniboxActionView &) = delete;
  SeoulOmniboxActionView &operator=(const SeoulOmniboxActionView &) = delete;
  ~SeoulOmniboxActionView() override;

  void SetQuery(std::string_view query);
  bool MoveSelection(bool forward, bool by_page);
  bool ExecuteSelection();

  size_t result_count() const { return visible_entries_.size(); }
  size_t selected_index_for_testing() const { return selected_index_; }
  SkColor selected_title_color_for_testing() const;

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds &available_size) const override;

private:
  void RebuildRows();
  void SelectIndex(size_t index);
  void ExecuteIndex(size_t index);

  raw_ptr<ShellController> controller_;
  ExecuteCallback execute_callback_;
  raw_ptr<views::ScrollView> scroll_view_ = nullptr;
  raw_ptr<views::View> rows_container_ = nullptr;
  std::vector<CommandLauncherEntry> visible_entries_;
  std::vector<raw_ptr<views::Button>> rows_;
  std::string query_;
  size_t selected_index_ = 0;
};

// Compatibility entry point used by the rail buttons. It no longer creates a
// dialog; Show() redirects the owning BrowserView into omnibox action mode.
class SeoulCommandLauncherView {
public:
  static void Show(gfx::NativeWindow parent, views::View *anchor,
                   ShellController *controller,
                   base::RepeatingClosure show_split_chooser);
};

} // namespace seoul

#endif // SEOUL_BROWSER_SHELL_VIEWS_SEOUL_COMMAND_LAUNCHER_VIEW_H_
