// Project Seoul native browser shell V0.

#include "seoul/browser/shell/views/seoul_shell_header_view.h"

#include <algorithm>
#include <optional>
#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
// nogncheck: //chrome/browser/ui reaches this shell target through the native
// browser integration, so declaring it here would form a dependency cycle.
// The TabUIHelper symbol links through //chrome/browser.
#include "chrome/browser/ui/tab_ui_helper.h" // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/vector_icons/vector_icons.h"
#include "seoul/browser/lifecycle/tab_strip_bridge.h"
#include "seoul/browser/shell/essential_grid_layout.h"
#include "seoul/browser/shell/shell_controller.h"
#include "seoul/browser/shell/views/seoul_split_chooser_view.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace seoul {
namespace {

constexpr int kEssentialOverflowCommandBase = 1;
#if BUILDFLAG(IS_MAC)
constexpr int kEssentialCornerRadius = 12;
#else
constexpr int kEssentialCornerRadius = 8;
#endif
constexpr int kEssentialHeight = 44;
constexpr size_t kMaxVisibleEssentials = 12;

void ConfigureEssentialButton(views::LabelButton *button) {
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  button->SetMinSize(gfx::Size(0, kEssentialHeight));
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 4)));
  button->SetEnabledTextColors(kColorToolbarText);
  button->SetFocusRingCornerRadius(kEssentialCornerRadius);
  views::InkDrop::Get(button)->SetMode(views::InkDropHost::InkDropMode::ON);
  views::InkDrop::UseInkDropForFloodFillRipple(views::InkDrop::Get(button),
                                               /*highlight_on_hover=*/true,
                                               /*highlight_on_focus=*/false);
  views::InkDrop::Get(button)->SetBaseColor(kColorToolbarButtonIcon);
  views::InkDrop::Get(button)->SetVisibleOpacity(0.12f);
  views::InkDrop::Get(button)->SetHighlightOpacity(0.12f);
}

void SetEssentialButtonActive(views::LabelButton *button, bool active) {
  button->SetBackground(active ? views::CreateRoundedRectBackground(
                                     kColorToolbarBackgroundSubtleEmphasis,
                                     kEssentialCornerRadius)
                               : nullptr);
}

void SetNeutralEssentialIcon(views::LabelButton *button) {
  button->SetImageModel(views::Button::STATE_NORMAL,
                        ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                                       kColorToolbarButtonIcon,
                                                       16));
  button->SetImageModel(
      views::Button::STATE_HOVERED,
      ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                     kColorToolbarButtonIconHovered, 16));
  button->SetImageModel(
      views::Button::STATE_PRESSED,
      ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                     kColorToolbarButtonIconPressed, 16));
  button->SetImageModel(
      views::Button::STATE_DISABLED,
      ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                     kColorToolbarButtonIconDisabled, 16));
}

std::u16string EssentialLabel(const ShellEssentialItem &essential) {
  return base::UTF8ToUTF16(essential.name.empty() ? essential.root_url
                                                  : essential.name);
}

bool IsCurrentWindowActive(const ShellEssentialItem &essential) {
  return ShouldHighlightEssential(essential.is_active,
                                  essential.live_in_current_window);
}

void UpdateEssentialButton(views::LabelButton *button,
                           const ShellEssentialItem &essential) {
  const std::u16string label = EssentialLabel(essential);
  const bool active = IsCurrentWindowActive(essential);
  SetEssentialButtonActive(button, active);
  button->GetViewAccessibility().SetName(label);
  button->GetViewAccessibility().SetDescription(active ? u"Current tab"
                                                       : std::u16string());
  button->SetTooltipText(label);
  button->SetEnabled(essential.state != ShellItemState::kUnavailable);
}

class EssentialsOverflowMenuModel final : public ui::SimpleMenuModel,
                                          public ui::SimpleMenuModel::Delegate {
public:
  EssentialsOverflowMenuModel(ShellController *controller,
                              std::vector<ShellEssentialItem> essentials)
      : ui::SimpleMenuModel(this), controller_(controller),
        essentials_(std::move(essentials)) {
    for (size_t index = 0; index < essentials_.size(); ++index) {
      AddItem(kEssentialOverflowCommandBase + static_cast<int>(index),
              EssentialLabel(essentials_[index]));
    }
  }

  bool IsCommandIdEnabled(int command_id) const override {
    const int index = command_id - kEssentialOverflowCommandBase;
    return index >= 0 && static_cast<size_t>(index) < essentials_.size() &&
           essentials_[index].state != ShellItemState::kUnavailable;
  }

  void ExecuteCommand(int command_id, int event_flags) override {
    (void)event_flags;
    const int index = command_id - kEssentialOverflowCommandBase;
    if (!controller_ || index < 0 ||
        static_cast<size_t>(index) >= essentials_.size() ||
        essentials_[index].state == ShellItemState::kUnavailable) {
      return;
    }
    std::ignore = controller_->OpenEssential(essentials_[index].id);
  }

private:
  raw_ptr<ShellController> controller_;
  std::vector<ShellEssentialItem> essentials_;
};

} // namespace

SeoulShellHeaderView::EssentialIconBinding::EssentialIconBinding() = default;
SeoulShellHeaderView::EssentialIconBinding::EssentialIconBinding(
    const EssentialIconBinding &) = default;
SeoulShellHeaderView::EssentialIconBinding::EssentialIconBinding(
    EssentialIconBinding &&) = default;
SeoulShellHeaderView::EssentialIconBinding &
SeoulShellHeaderView::EssentialIconBinding::operator=(
    const EssentialIconBinding &) = default;
SeoulShellHeaderView::EssentialIconBinding &
SeoulShellHeaderView::EssentialIconBinding::operator=(EssentialIconBinding &&) =
    default;
SeoulShellHeaderView::EssentialIconBinding::~EssentialIconBinding() = default;

SeoulShellHeaderView::SeoulShellHeaderView(
    ShellController *controller, BrowserWindowInterface *browser_window,
    Profile *profile)
    : browser_window_(browser_window), profile_(profile) {
  auto *layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(6, 6, 2, 6),
      4));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  BindController(controller);
}

SeoulShellHeaderView::~SeoulShellHeaderView() {
  CancelAllFaviconRequestsAndInvalidateCallbacks();
  if (controller_) {
    controller_->RemoveObserver(this);
  }
}

void SeoulShellHeaderView::BindController(ShellController *controller) {
  if (controller_ == controller) {
    return;
  }
  if (controller_) {
    controller_->RemoveObserver(this);
  }
  controller_ = controller;
  split_chooser_ =
      controller_ ? std::make_unique<SeoulSplitChooserView>(this, controller_)
                  : nullptr;
  CancelAllFaviconRequestsAndInvalidateCallbacks();
  essentials_initialized_ = false;
  rendered_essentials_.clear();
  overflow_essentials_.clear();
  essential_icon_bindings_.clear();
  if (controller_) {
    controller_->AddObserver(this);
    RebuildFromSnapshot(controller_->snapshot());
  }
}

void SeoulShellHeaderView::BindBrowserContext(
    BrowserWindowInterface *browser_window, Profile *profile) {
  if (browser_window_ == browser_window && profile_ == profile) {
    return;
  }
  CancelAllFaviconRequestsAndInvalidateCallbacks();
  browser_window_ = browser_window;
  profile_ = profile;
  for (EssentialIconBinding &binding : essential_icon_bindings_) {
    binding.source = EssentialIconSource::kFallback;
    binding.task_id = base::CancelableTaskTracker::kBadTaskId;
    ++binding.generation;
    ApplyDefaultEssentialIcon(binding.button);
  }
  if (controller_) {
    RebuildFromSnapshot(controller_->snapshot());
  }
}

void SeoulShellHeaderView::SetPresentationCollapsed(bool collapsed) {
  if (presentation_collapsed_ == collapsed) {
    return;
  }
  presentation_collapsed_ = collapsed;
  essentials_initialized_ = false;
  if (controller_) {
    RebuildFromSnapshot(controller_->snapshot());
  }
  InvalidateLayout();
  PreferredSizeChanged();
}

void SeoulShellHeaderView::OnShellSnapshotChanged(
    const ShellChange &change, const ShellSnapshot &snapshot) {
  (void)change;
  RebuildFromSnapshot(snapshot);
}

void SeoulShellHeaderView::SetCachedFaviconLookupForTesting(
    CachedFaviconLookupForTesting lookup) {
  CancelAllFaviconRequestsAndInvalidateCallbacks();
  cached_favicon_lookup_for_testing_ = std::move(lookup);
  for (EssentialIconBinding &binding : essential_icon_bindings_) {
    binding.source = EssentialIconSource::kFallback;
    binding.task_id = base::CancelableTaskTracker::kBadTaskId;
    ++binding.generation;
    ApplyDefaultEssentialIcon(binding.button);
  }
  if (controller_) {
    RebuildFromSnapshot(controller_->snapshot());
  }
}

ui::ImageModel
SeoulShellHeaderView::EssentialIconForTesting(const EssentialId &id) const {
  const auto binding = std::ranges::find(essential_icon_bindings_, id,
                                         &EssentialIconBinding::id);
  if (binding == essential_icon_bindings_.end() || !binding->button) {
    return ui::ImageModel();
  }
  const std::optional<ui::ImageModel> &icon =
      binding->button->GetImageModel(views::Button::STATE_NORMAL);
  return icon.value_or(ui::ImageModel());
}

void SeoulShellHeaderView::CancelAllFaviconRequestsAndInvalidateCallbacks() {
  favicon_task_tracker_.TryCancelAll();
  weak_factory_.InvalidateWeakPtrs();
  for (EssentialIconBinding &binding : essential_icon_bindings_) {
    binding.task_id = base::CancelableTaskTracker::kBadTaskId;
    ++binding.generation;
  }
}

void SeoulShellHeaderView::CancelFaviconRequest(EssentialIconBinding &binding) {
  if (binding.task_id != base::CancelableTaskTracker::kBadTaskId) {
    favicon_task_tracker_.TryCancel(binding.task_id);
    binding.task_id = base::CancelableTaskTracker::kBadTaskId;
  }
}

ui::ImageModel
SeoulShellHeaderView::FindLiveFavicon(const LiveTabKey &live_tab) const {
  if (!live_tab.is_valid() || !browser_window_ || !profile_ ||
      browser_window_->GetProfile() != profile_ ||
      browser_window_->IsDeleteScheduled()) {
    return ui::ImageModel();
  }
  TabStripModel *tab_strip = browser_window_->GetTabStripModel();
  if (!tab_strip) {
    return ui::ImageModel();
  }
  for (int index = 0; index < tab_strip->count(); ++index) {
    tabs::TabInterface *tab = tab_strip->GetTabAtIndex(index);
    if (!tab || TabStripBridge::KeyForTab(tab) != live_tab) {
      continue;
    }
    TabUIHelper *helper = TabUIHelper::From(tab);
    return helper ? helper->GetFavicon() : ui::ImageModel();
  }
  return ui::ImageModel();
}

void SeoulShellHeaderView::ApplyEssentialIcon(views::LabelButton *button,
                                              const ui::ImageModel &icon) {
  if (!button || icon.IsEmpty()) {
    return;
  }
  button->SetText(std::u16string());
  button->SetImageModel(views::Button::STATE_NORMAL, icon);
  button->SetImageModel(views::Button::STATE_HOVERED, icon);
  button->SetImageModel(views::Button::STATE_PRESSED, icon);
  button->SetImageModel(views::Button::STATE_DISABLED, icon);
}

void SeoulShellHeaderView::ApplyDefaultEssentialIcon(
    views::LabelButton *button) {
  ui::ImageModel icon = favicon::GetDefaultFaviconModel(kColorToolbar);
  if (icon.IsEmpty()) {
    SetNeutralEssentialIcon(button);
    return;
  }
  ApplyEssentialIcon(button, icon);
}

void SeoulShellHeaderView::ResolveEssentialIcon(
    EssentialIconBinding &binding, const ShellEssentialItem &essential) {
  const GURL root_url(essential.root_url);
  const bool root_changed = binding.root_url != root_url;
  if (root_changed) {
    CancelFaviconRequest(binding);
    ++binding.generation;
    binding.root_url = root_url;
    binding.source = EssentialIconSource::kFallback;
    ApplyDefaultEssentialIcon(binding.button);
  }
  binding.live_tab = essential.live_tab;

  ui::ImageModel live_icon = FindLiveFavicon(binding.live_tab);
  if (!live_icon.IsEmpty()) {
    CancelFaviconRequest(binding);
    ++binding.generation;
    binding.source = EssentialIconSource::kLive;
    ApplyEssentialIcon(binding.button, live_icon);
    return;
  }

  if (binding.source == EssentialIconSource::kLive) {
    ++binding.generation;
    binding.source = EssentialIconSource::kFallback;
    ApplyDefaultEssentialIcon(binding.button);
  }
  if (binding.source == EssentialIconSource::kCachePending ||
      binding.source == EssentialIconSource::kCached ||
      binding.source == EssentialIconSource::kCacheMiss) {
    return;
  }
  if (!binding.root_url.is_valid() || !binding.root_url.SchemeIsHTTPOrHTTPS()) {
    binding.source = EssentialIconSource::kCacheMiss;
    return;
  }
  StartCachedFaviconLookup(binding);
}

void SeoulShellHeaderView::StartCachedFaviconLookup(
    EssentialIconBinding &binding) {
  CancelFaviconRequest(binding);
  binding.source = EssentialIconSource::kCachePending;
  const uint64_t generation = ++binding.generation;
  favicon_base::FaviconImageCallback callback = base::BindOnce(
      &SeoulShellHeaderView::OnCachedFaviconAvailable,
      weak_factory_.GetWeakPtr(), binding.id, binding.root_url, generation);

  if (cached_favicon_lookup_for_testing_) {
    cached_favicon_lookup_for_testing_.Run(binding.root_url,
                                           std::move(callback));
    return;
  }
  favicon::FaviconService *favicon_service =
      profile_ ? FaviconServiceFactory::GetForProfile(
                     profile_, ServiceAccessType::EXPLICIT_ACCESS)
               : nullptr;
  if (!favicon_service) {
    binding.source = EssentialIconSource::kCacheMiss;
    return;
  }
  // This asks Chromium's profile-local history/favicon database only.
  // It never performs a network request for an Essential.
  const base::CancelableTaskTracker::TaskId task_id =
      favicon_service->GetFaviconImageForPageURL(
          binding.root_url, std::move(callback), &favicon_task_tracker_);
  if (binding.source == EssentialIconSource::kCachePending &&
      binding.generation == generation) {
    binding.task_id = task_id;
  } else {
    // Be correct even if a test service fulfills the callback synchronously.
    favicon_task_tracker_.TryCancel(task_id);
  }
}

void SeoulShellHeaderView::OnCachedFaviconAvailable(
    EssentialId id, GURL requested_url, uint64_t generation,
    const favicon_base::FaviconImageResult &result) {
  auto binding = std::ranges::find(essential_icon_bindings_, id,
                                   &EssentialIconBinding::id);
  if (binding == essential_icon_bindings_.end() ||
      binding->root_url != requested_url || binding->generation != generation ||
      binding->source != EssentialIconSource::kCachePending) {
    return;
  }
  binding->task_id = base::CancelableTaskTracker::kBadTaskId;

  ui::ImageModel live_icon = FindLiveFavicon(binding->live_tab);
  if (!live_icon.IsEmpty()) {
    binding->source = EssentialIconSource::kLive;
    ApplyEssentialIcon(binding->button, live_icon);
    return;
  }
  if (result.image.IsEmpty()) {
    binding->source = EssentialIconSource::kCacheMiss;
    return;
  }
  binding->source = EssentialIconSource::kCached;
  ApplyEssentialIcon(binding->button, ui::ImageModel::FromImage(result.image));
}

void SeoulShellHeaderView::RebuildFromSnapshot(const ShellSnapshot &snapshot) {
  if (!essentials_container_) {
    essentials_container_ = AddChildView(std::make_unique<views::View>());
    essentials_layout_ = essentials_container_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
    essentials_layout_->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
  }

  const bool collapsed = presentation_collapsed_;
  const size_t visible_count =
      std::min(snapshot.essentials.size(), kMaxVisibleEssentials);
  bool structure_changed =
      !essentials_initialized_ || essentials_collapsed_ != collapsed ||
      rendered_essentials_.size() != snapshot.essentials.size() ||
      essential_icon_bindings_.size() != visible_count;
  if (!structure_changed) {
    for (size_t index = 0; index < visible_count; ++index) {
      if (rendered_essentials_[index].id != snapshot.essentials[index].id) {
        structure_changed = true;
        break;
      }
    }
  }

  if (structure_changed) {
    CancelAllFaviconRequestsAndInvalidateCallbacks();
    essentials_overflow_button_ = nullptr;
    essential_icon_bindings_.clear();
    essentials_container_->RemoveAllChildViews();
    const size_t columns =
        EssentialColumnsForVisibleCount(visible_count, collapsed);
    views::View *row = nullptr;
    views::BoxLayout *row_layout = nullptr;
    for (size_t index = 0; index < visible_count; ++index) {
      if (index % columns == 0) {
        row = essentials_container_->AddChildView(
            std::make_unique<views::View>());
        row_layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
        row_layout->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kStretch);
      }
      const ShellEssentialItem &essential = snapshot.essentials[index];
      auto *button = row->AddChildView(std::make_unique<views::LabelButton>(
          base::BindRepeating(
              [](ShellController *controller, EssentialId id) {
                if (controller) {
                  std::ignore = controller->OpenEssential(id);
                }
              },
              controller_, essential.id),
          std::u16string()));
      ConfigureEssentialButton(button);
      SetEssentialButtonActive(button, false);
      ApplyDefaultEssentialIcon(button);
      row_layout->SetFlexForView(button, 1);
      EssentialIconBinding binding;
      binding.button = button;
      binding.id = essential.id;
      essential_icon_bindings_.push_back(std::move(binding));
    }
    if (snapshot.essentials.size() > visible_count) {
      if (visible_count % columns == 0) {
        row = essentials_container_->AddChildView(
            std::make_unique<views::View>());
        row_layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
        row_layout->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kStretch);
      }
      const std::u16string count =
          base::NumberToString16(snapshot.essentials.size() - visible_count);
      essentials_overflow_button_ =
          row->AddChildView(std::make_unique<views::LabelButton>(
              base::BindRepeating(
                  &SeoulShellHeaderView::OnEssentialsOverflowPressed,
                  base::Unretained(this)),
              u"＋"));
      ConfigureEssentialButton(essentials_overflow_button_);
      SetEssentialButtonActive(essentials_overflow_button_, false);
      row_layout->SetFlexForView(essentials_overflow_button_, 1);
      essentials_overflow_button_->GetViewAccessibility().SetName(
          count + u" more Essentials");
      essentials_overflow_button_->SetTooltipText(u"Show more Essentials");
    }
  }

  for (size_t index = 0; index < visible_count; ++index) {
    UpdateEssentialButton(essential_icon_bindings_[index].button,
                          snapshot.essentials[index]);
    ResolveEssentialIcon(essential_icon_bindings_[index],
                         snapshot.essentials[index]);
  }
  overflow_essentials_.assign(snapshot.essentials.begin() + visible_count,
                              snapshot.essentials.end());
  if (essentials_overflow_button_) {
    const std::u16string count =
        base::NumberToString16(overflow_essentials_.size());
    essentials_overflow_button_->GetViewAccessibility().SetName(
        count + u" more Essentials");
  }
  rendered_essentials_ = snapshot.essentials;
  essentials_collapsed_ = collapsed;
  essentials_initialized_ = true;
  const bool has_essentials = !snapshot.essentials.empty();
  essentials_container_->SetVisible(has_essentials);
  // Do not leave the header's interior insets in the flex layout when a Space
  // has no Essentials. That empty box was a second, invisible titlebar gap.
  SetVisible(has_essentials);
}

void SeoulShellHeaderView::ShowSplitChooser() {
  if (split_chooser_) {
    split_chooser_->Show();
  }
}

void SeoulShellHeaderView::OnEssentialsOverflowPressed() {
  if (!controller_ || !essentials_overflow_button_ ||
      overflow_essentials_.empty() || !GetWidget()) {
    return;
  }
  essentials_overflow_menu_model_ =
      std::make_unique<EssentialsOverflowMenuModel>(controller_,
                                                    overflow_essentials_);
  essentials_overflow_menu_runner_ = std::make_unique<views::MenuRunner>(
      essentials_overflow_menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
  essentials_overflow_menu_runner_->RunMenuAt(
      GetWidget(), nullptr,
      essentials_overflow_button_->GetAnchorBoundsInScreen(),
      views::MenuAnchorPosition::kTopLeft,
      ui::mojom::MenuSourceType::kKeyboard);
}

BEGIN_METADATA(SeoulShellHeaderView)
END_METADATA

} // namespace seoul
