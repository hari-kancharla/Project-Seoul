// Project Seoul native browser shell: unified omnibox action results.

#include "seoul/browser/shell/views/seoul_command_launcher_view.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h" // nogncheck
#include "components/vector_icons/vector_icons.h"
#include "seoul/browser/shell/shell_controller.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/font.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/view_class_properties.h"

namespace seoul {
namespace {

constexpr int kResultsTopInset = 8;
constexpr int kResultsBottomInset = 10;
constexpr int kResultsHorizontalInset = 10;
constexpr int kResultHeight = 52;
constexpr int kResultsViewportHeight = 252;
constexpr int kRowsPerPage = 4;

const gfx::VectorIcon &EntryIcon(const CommandLauncherEntry &entry) {
  if (entry.kind == CommandLauncherEntryKind::kWorkspace) {
    return vector_icons::kDesktopWindowsIcon;
  }
  if (entry.kind == CommandLauncherEntryKind::kEssential) {
    return vector_icons::kStarIcon;
  }
  if (entry.kind == CommandLauncherEntryKind::kTab) {
    return vector_icons::kGlobeIcon;
  }
  switch (entry.action) {
  case ShellUtilityAction::kNewTemporaryTab:
    return vector_icons::kAddIcon;
  case ShellUtilityAction::kCreateSplit:
    return vector_icons::kSelectWindowIcon;
  default:
    return vector_icons::kLaunchIcon;
  }
}

class ActionRowView final : public views::Button {
public:
  METADATA_HEADER(ActionRowView, views::Button)

public:
  ActionRowView(const CommandLauncherEntry &entry, size_t index,
                base::RepeatingCallback<void(size_t)> execute_callback)
      : views::Button(base::BindRepeating(
            [](base::RepeatingCallback<void(size_t)> callback, size_t row,
               const ui::Event &) { callback.Run(row); },
            std::move(execute_callback), index)),
        icon_(EntryIcon(entry)) {
    SetFocusBehavior(FocusBehavior::NEVER);
    SetPreferredSize(gfx::Size(0, kResultHeight));
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(10, 8, 10, 8)));
    GetViewAccessibility().SetName(base::UTF8ToUTF16(entry.label));

    auto *layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    icon_view_ = AddChildView(std::make_unique<views::ImageView>());
    icon_view_->SetPreferredSize(gfx::Size(28, 28));
    icon_view_->SetImageSize(gfx::Size(16, 16));
    icon_view_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 0, 12));

    title_ = AddChildView(std::make_unique<views::Label>(
        base::UTF8ToUTF16(entry.label), views::style::CONTEXT_LABEL,
        views::style::STYLE_PRIMARY));
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_->SetElideBehavior(gfx::ELIDE_TAIL);
    title_->SetFontList(
        title_->font_list().DeriveWithWeight(gfx::Font::Weight::MEDIUM));
    layout->SetFlexForView(title_, 1);

    if (!entry.shortcut.empty()) {
      shortcut_ = AddChildView(std::make_unique<views::Label>(
          base::UTF8ToUTF16(entry.shortcut), views::style::CONTEXT_LABEL,
          views::style::STYLE_SECONDARY));
      shortcut_->SetFontList(
          shortcut_->font_list().DeriveWithSizeDelta(-2).DeriveWithWeight(
              gfx::Font::Weight::SEMIBOLD));
      shortcut_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(6, 8)));
    }
    UpdateStyle();
  }

  ActionRowView(const ActionRowView &) = delete;
  ActionRowView &operator=(const ActionRowView &) = delete;
  ~ActionRowView() override = default;

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    UpdateStyle();
  }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    UpdateStyle();
  }

private:
  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    UpdateStyle();
  }

  void UpdateStyle() {
    const bool hovered =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    const ui::ColorId background = selected_
                                       ? kColorOmniboxResultsBackgroundSelected
                                       : kColorOmniboxResultsBackgroundHovered;
    SetBackground((selected_ || hovered)
                      ? views::CreateRoundedRectBackground(background, 8)
                      : nullptr);

    const ui::ColorId foreground =
        selected_ ? kColorOmniboxResultsTextSelected : kColorOmniboxText;
    title_->SetEnabledColor(foreground);
    icon_view_->SetImage(
        ui::ImageModel::FromVectorIcon(*icon_, foreground, 16));
    if (shortcut_) {
      shortcut_->SetEnabledColor(selected_ ? kColorOmniboxResultsTextSelected
                                           : kColorOmniboxResultsTextDimmed);
      shortcut_->SetBackground(views::CreateRoundedRectBackground(
          selected_ ? kColorOmniboxResultsBackgroundHovered
                    : kColorToolbarBackgroundSubtleEmphasis,
          4));
    }
    SchedulePaint();
  }

  raw_ref<const gfx::VectorIcon> icon_;
  raw_ptr<views::ImageView> icon_view_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> shortcut_ = nullptr;
  bool selected_ = false;
};

BEGIN_METADATA(ActionRowView)
END_METADATA

} // namespace

SeoulOmniboxActionView::SeoulOmniboxActionView(ShellController *controller,
                                               ExecuteCallback execute_callback)
    : controller_(controller), execute_callback_(std::move(execute_callback)) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(kResultsTopInset, kResultsHorizontalInset,
                        kResultsBottomInset, kResultsHorizontalInset),
      0));

  auto scroll_view = std::make_unique<views::ScrollView>(
      views::ScrollView::ScrollWithLayers::kEnabled);
  scroll_view->SetBackgroundColor(kColorOmniboxResultsBackground);
  scroll_view->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  scroll_view->SetAllowKeyboardScrolling(false);
  scroll_view->ClipHeightTo(0, kResultsViewportHeight);

  auto rows = std::make_unique<views::View>();
  rows->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  rows_container_ = scroll_view->SetContents(std::move(rows));
  scroll_view_ = AddChildView(std::move(scroll_view));
  RebuildRows();
}

SeoulOmniboxActionView::~SeoulOmniboxActionView() = default;

void SeoulOmniboxActionView::SetQuery(std::string_view query) {
  if (query_ == query) {
    return;
  }
  query_.assign(query);
  RebuildRows();
}

bool SeoulOmniboxActionView::MoveSelection(bool forward, bool by_page) {
  if (visible_entries_.empty()) {
    return false;
  }
  const size_t count = visible_entries_.size();
  if (by_page) {
    const size_t distance = std::min<size_t>(kRowsPerPage, count - 1);
    SelectIndex(forward ? std::min(count - 1, selected_index_ + distance)
                : selected_index_ > distance ? selected_index_ - distance
                                             : 0);
  } else {
    SelectIndex(forward ? (selected_index_ + 1) % count
                        : (selected_index_ + count - 1) % count);
  }
  return true;
}

bool SeoulOmniboxActionView::ExecuteSelection() {
  if (visible_entries_.empty() || selected_index_ >= visible_entries_.size()) {
    return false;
  }
  ExecuteIndex(selected_index_);
  return true;
}

gfx::Size SeoulOmniboxActionView::CalculatePreferredSize(
    const views::SizeBounds &available_size) const {
  if (visible_entries_.empty()) {
    return gfx::Size();
  }
  const int viewport_height =
      std::min(kResultsViewportHeight,
               static_cast<int>(visible_entries_.size()) * kResultHeight);
  return gfx::Size(0, kResultsTopInset + viewport_height + kResultsBottomInset);
}

void SeoulOmniboxActionView::RebuildRows() {
  std::vector<CommandLauncherEntry> available;
  if (controller_) {
    available = controller_->CommandLauncherEntries();
  }
  std::erase_if(available, [](const CommandLauncherEntry &entry) {
    return !entry.enabled;
  });

  visible_entries_ =
      CommandLauncherCatalog::Filter(available, query_, available.size());
  rows_container_->RemoveAllChildViews();
  rows_.clear();
  selected_index_ = 0;

  rows_.reserve(visible_entries_.size());
  for (size_t i = 0; i < visible_entries_.size(); ++i) {
    auto row = std::make_unique<ActionRowView>(
        visible_entries_[i], i,
        base::BindRepeating(&SeoulOmniboxActionView::ExecuteIndex,
                            base::Unretained(this)));
    rows_.push_back(rows_container_->AddChildView(std::move(row)));
  }
  if (!rows_.empty()) {
    static_cast<ActionRowView *>(rows_.front().get())->SetSelected(true);
  }

  SetVisible(!visible_entries_.empty());
  rows_container_->InvalidateLayout();
  scroll_view_->InvalidateLayout();
  PreferredSizeChanged();
}

void SeoulOmniboxActionView::SelectIndex(size_t index) {
  if (index >= rows_.size() || index == selected_index_) {
    return;
  }
  static_cast<ActionRowView *>(rows_[selected_index_].get())
      ->SetSelected(false);
  selected_index_ = index;
  auto *selected = static_cast<ActionRowView *>(rows_[selected_index_].get());
  selected->SetSelected(true);
  selected->ScrollRectToVisible(selected->GetLocalBounds());
}

void SeoulOmniboxActionView::ExecuteIndex(size_t index) {
  if (index >= visible_entries_.size() || !execute_callback_) {
    return;
  }
  SelectIndex(index);
  execute_callback_.Run(visible_entries_[index]);
}

void SeoulCommandLauncherView::Show(gfx::NativeWindow parent,
                                    views::View *anchor,
                                    ShellController *controller,
                                    base::RepeatingClosure show_split_chooser) {
  (void)anchor;
  (void)controller;
  (void)show_split_chooser;
  if (BrowserView *browser_view =
          BrowserView::GetBrowserViewForNativeWindow(parent)) {
    browser_view->ShowSeoulOmniboxActions();
  }
}

BEGIN_METADATA(SeoulOmniboxActionView)
END_METADATA

} // namespace seoul
