// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/product/browser/seoul_settings_window.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_attributes_storage_observer.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser_view_prefs.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_service_factory.h"
#include "seoul/browser/adblock/ad_block_settings.h"
#include "seoul/browser/organization/organization_model.h"
#include "seoul/browser/organization/organization_observer.h"
#include "seoul/browser/organization/seoul_organization_service.h"
#include "seoul/browser/organization/seoul_organization_service_factory.h"
#include "seoul/browser/product/browser/seoul_runtime_service.h"
#include "seoul/browser/product/browser/seoul_runtime_service_factory.h"
#include "seoul/browser/shell/views/seoul_workspace_name_dialog.h"
#include "seoul/browser/site_layers/site_layer_types.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

#if BUILDFLAG(IS_MAC)
#include "seoul/browser/product/browser/seoul_settings_toolbar_mac.h"
#endif

namespace seoul {
namespace {

constexpr std::array<std::u16string_view, 6> kPaneNames = {
    u"General",   u"Appearance", u"Profiles",
    u"Shortcuts", u"Privacy",    u"Advanced"};

class SettingsView;
struct SettingsWindowState : SettingsWindowOwner {
  ~SettingsWindowState() override { Reset(); }
  bool MoveToProfile(Profile *profile);
  raw_ptr<Profile> owner_profile = nullptr;
  // The widget must be destroyed before its delegate and contents.
  std::unique_ptr<views::DialogDelegate> delegate;
  std::unique_ptr<views::Widget> widget;
  base::WeakPtr<SettingsView> view;
#if BUILDFLAG(IS_MAC)
  std::unique_ptr<SettingsToolbarMac> toolbar;
#endif

  void Reset() {
    view = nullptr;
#if BUILDFLAG(IS_MAC)
    toolbar.reset();
#endif
    widget.reset();
    delegate.reset();
  }
};

base::WeakPtr<SettingsView> OpenWindow() {
  // Discover the one open window through profile-owned services. No mutable
  // process-global UI registry or secondary preference store is needed.
  auto *manager = g_browser_process->profile_manager();
  if (!manager)
    return {};
  for (auto *profile : manager->GetLoadedProfiles()) {
    auto *runtime = SeoulRuntimeServiceFactory::GetForProfileIfExists(profile);
    auto *owner =
        runtime ? static_cast<SettingsWindowState *>(runtime->settings_window())
                : nullptr;
    if (owner && owner->view)
      return owner->view;
  }
  return {};
}

SettingsPane ReadPane(Profile *profile) {
  return static_cast<SettingsPane>(
      std::clamp(profile->GetPrefs()->GetInteger(kSettingsLastPanePref), 0, 5));
}

ProfileAttributesStorage *ProfileStorage() {
  auto *manager = g_browser_process->profile_manager();
  return manager ? &manager->GetProfileAttributesStorage() : nullptr;
}

bool EditableProfile(Profile *profile) {
  if (!profile || !profile->IsRegularProfile() || profile->IsOffTheRecord())
    return false;
  auto *storage = ProfileStorage();
  auto *entry = storage
                    ? storage->GetProfileAttributesWithPath(profile->GetPath())
                    : nullptr;
  return !entry || (!entry->IsSigninRequired() && !entry->IsOmitted());
}

std::u16string ProfileName(Profile *profile) {
  auto *storage = ProfileStorage();
  if (auto *entry =
          storage ? storage->GetProfileAttributesWithPath(profile->GetPath())
                  : nullptr)
    return entry->GetName();
  return base::UTF8ToUTF16(profile->GetPrefs()->GetString(prefs::kProfileName));
}

views::Label *Label(views::View *parent, const std::u16string &text,
                    bool secondary = false) {
  auto *label = parent->AddChildView(std::make_unique<views::Label>(text));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetMultiLine(true);
  label->SetFontList(gfx::FontList().DeriveWithSizeDelta(secondary ? -1 : 0));
  label->SetEnabledColor(secondary ? ui::kColorLabelForegroundSecondary
                                   : ui::kColorLabelForeground);
  return label;
}

std::unique_ptr<views::BoxLayoutView> Column(int spacing = 8) {
  auto view = std::make_unique<views::BoxLayoutView>();
  view->SetOrientation(views::BoxLayout::Orientation::kVertical);
  view->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  view->SetBetweenChildSpacing(spacing);
  return view;
}

bool Dark(const views::View *view) {
  return view->GetColorProvider() &&
         color_utils::IsDark(
             view->GetColorProvider()->GetColor(ui::kColorDialogBackground));
}
SkColor Accent(const views::View *view) {
  return Dark(view) ? SkColorSetRGB(128, 205, 250)
                    : SkColorSetRGB(16, 109, 164);
}
SkColor Selection(const views::View *view) {
  return Dark(view) ? SkColorSetRGB(44, 72, 94) : SkColorSetRGB(229, 241, 255);
}
SkColor Outline(const views::View *view) {
  return Dark(view) ? SkColorSetRGB(83, 83, 85) : SkColorSetRGB(216, 216, 218);
}

std::unique_ptr<views::Separator> Divider() {
  auto separator = std::make_unique<views::Separator>();
  separator->SetColorId(ui::kColorSysNeutralOutline);
  return separator;
}

class SettingsToggle : public views::ToggleButton {
  METADATA_HEADER(SettingsToggle, views::ToggleButton)
public:
  explicit SettingsToggle(PressedCallback callback)
      : views::ToggleButton(std::move(callback)) {
    SetThumbOnColor(SK_ColorWHITE);
    SetThumbOffColor(SK_ColorWHITE);
    SetInnerBorderEnabled(false);
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds &) const override {
    return gfx::Size(38, 24);
  }

protected:
  gfx::Rect GetTrackBounds() const override {
    auto bounds = GetContentsBounds();
    bounds.ClampToCenteredSize(gfx::Size(38, 22));
    return bounds;
  }
  gfx::Rect GetThumbBounds() const override {
    auto bounds = GetTrackBounds();
    bounds.Inset(1);
    bounds.set_x(bounds.x() +
                 GetAnimationProgress() * (bounds.width() - bounds.height()));
    bounds.set_width(bounds.height());
    return bounds;
  }
};
BEGIN_METADATA(SettingsToggle)
END_METADATA

// Ordinary settings use compact neutral buttons. Accent is reserved for state.
class SettingsButton : public views::MdTextButton {
  METADATA_HEADER(SettingsButton, views::MdTextButton)
public:
  SettingsButton(base::RepeatingClosure callback, const std::u16string &text)
      : views::MdTextButton(std::move(callback), text) {
    SetCornerRadius(5);
    SetMinSize(gfx::Size(0, 24));
    SetCustomPadding(gfx::Insets::VH(2, 12));
    label()->SetFontList(gfx::FontList());
  }
  void OnThemeChanged() override {
    views::MdTextButton::OnThemeChanged();
    SetEnabledTextColors(ui::kColorLabelForeground);
    SetBgColorOverrideDeprecated(Dark(this) ? SkColorSetRGB(70, 70, 72)
                                            : SK_ColorWHITE);
    SetStrokeColorOverrideDeprecated(GetStyle() == ui::ButtonStyle::kText
                                         ? SK_ColorTRANSPARENT
                                         : Outline(this));
  }
};
BEGIN_METADATA(SettingsButton)
END_METADATA

views::MdTextButton *Button(views::View *parent, std::u16string text,
                            base::RepeatingClosure callback) {
  return parent->AddChildView(
      std::make_unique<SettingsButton>(std::move(callback), text));
}

// Form sections have no cards. Alignment and separators establish grouping.
class SettingsSection : public views::BoxLayoutView {
  METADATA_HEADER(SettingsSection, views::BoxLayoutView)
public:
  SettingsSection() {
    SetOrientation(views::BoxLayout::Orientation::kVertical);
    SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  }
};
BEGIN_METADATA(SettingsSection)
END_METADATA

class ProfileListView : public views::BoxLayoutView {
  METADATA_HEADER(ProfileListView, views::BoxLayoutView)
public:
  ProfileListView() {
    SetOrientation(views::BoxLayout::Orientation::kVertical);
    SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
    SetBetweenChildSpacing(6);
    SetInsideBorderInsets(gfx::Insets(12));
  }
  void OnThemeChanged() override {
    views::BoxLayoutView::OnThemeChanged();
    SetBackground(views::CreateRoundedRectBackground(
        Dark(this) ? SkColorSetRGB(37, 37, 39) : SK_ColorWHITE, 8));
    SetBorder(views::CreateRoundedRectBorder(1, 8, Outline(this)));
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds &) const override {
    // Constrain the width without erasing the height needed by real rows.
    auto size = views::BoxLayoutView::CalculatePreferredSize(
        views::SizeBounds(240, views::SizeBound()));
    size.set_width(240);
    size.set_height(std::max(330, size.height()));
    return size;
  }
};
BEGIN_METADATA(ProfileListView)
END_METADATA

class CategoryButton : public views::Button {
  METADATA_HEADER(CategoryButton, views::Button)
public:
  CategoryButton(const std::u16string &text, const gfx::VectorIcon &icon,
                 base::RepeatingClosure callback,
                 base::RepeatingCallback<void(ui::KeyboardCode)> navigate)
      : views::Button(std::move(callback)), icon_(icon),
        navigate_(std::move(navigate)) {
    auto *layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(7, 8), 5));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    image_ = AddChildView(std::make_unique<views::ImageView>());
    label_ = Label(this, text);
    label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    GetViewAccessibility().SetName(text);
    GetViewAccessibility().SetRole(ax::mojom::Role::kTab);
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetPreferredSize(gfx::Size(104, 68));
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(), 8);
  }
  void Select(bool selected) {
    selected_ = selected;
    SetFocusBehavior(selected ? FocusBehavior::ALWAYS : FocusBehavior::NEVER);
    GetViewAccessibility().SetIsSelected(selected);
    UpdateColors();
  }
  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    UpdateColors();
  }
  bool OnKeyPressed(const ui::KeyEvent &event) override {
    if (!event.IsAltDown() && !event.IsControlDown() &&
        !event.IsCommandDown()) {
      switch (event.key_code()) {
      case ui::VKEY_LEFT:
      case ui::VKEY_RIGHT:
      case ui::VKEY_HOME:
      case ui::VKEY_END:
        navigate_.Run(event.key_code());
        return true;
      default:
        break;
      }
    }
    return views::Button::OnKeyPressed(event);
  }

private:
  void UpdateColors() {
    if (!GetColorProvider())
      return;
    const auto foreground =
        selected_
            ? Accent(this)
            : GetColorProvider()->GetColor(ui::kColorLabelForegroundSecondary);
    image_->SetImage(ui::ImageModel::FromVectorIcon(*icon_, foreground, 28));
    label_->SetEnabledColor(foreground);
    SetBackground(selected_ ? views::CreateRoundedRectBackground(
                                  Dark(this) ? SkColorSetRGB(60, 60, 62)
                                             : SkColorSetRGB(232, 231, 233),
                                  8)
                            : nullptr);
  }
  const raw_ref<const gfx::VectorIcon> icon_;
  base::RepeatingCallback<void(ui::KeyboardCode)> navigate_;
  raw_ptr<views::ImageView> image_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
  bool selected_ = false;
};
BEGIN_METADATA(CategoryButton)
END_METADATA

class SettingsView : public views::BoxLayoutView,
                     public ProfileObserver,
                     public ProfileAttributesStorageObserver,
                     public OrganizationModelObserver,
                     public ui::SelectFileDialog::Listener {
  METADATA_HEADER(SettingsView, views::BoxLayoutView)
public:
  SettingsView(views::DialogDelegate *delegate, SettingsWindowState *owner)
      : delegate_(delegate), owner_(owner) {
    SetOrientation(views::BoxLayout::Orientation::kVertical);
    SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
    SetPreferredSize(gfx::Size(680, 470));
#if !BUILDFLAG(IS_MAC)
    auto *categories = AddChildView(std::make_unique<views::BoxLayoutView>());
    categories->SetInsideBorderInsets(gfx::Insets::VH(8, 18));
    categories->SetBetweenChildSpacing(4);
    categories->SetMainAxisAlignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    categories->GetViewAccessibility().SetRole(ax::mojom::Role::kTabList);
    categories->GetViewAccessibility().SetName(u"Settings categories");
    const std::array<const gfx::VectorIcon *, 6> icons = {
        &vector_icons::kSettingsOutlineIcon, &vector_icons::kDesktopWindowsIcon,
        &vector_icons::kPersonTextIcon,      &vector_icons::kKeyboardIcon,
        &vector_icons::kShieldIcon,          &vector_icons::kCodeIcon};
    for (size_t i = 0; i < kPaneNames.size(); ++i) {
      categories_.push_back(
          categories->AddChildView(std::make_unique<CategoryButton>(
              std::u16string(kPaneNames[i]), *icons[i],
              base::BindRepeating(&SettingsView::ShowPane,
                                  base::Unretained(this),
                                  static_cast<SettingsPane>(i)),
              base::BindRepeating(&SettingsView::MoveCategory,
                                  base::Unretained(this), i))));
    }
    AddChildView(Divider());
#endif
    scroll_ = AddChildView(std::make_unique<views::ScrollView>());
    // Recompute the pane's height at the constrained viewport width. The
    // legacy ScrollView mode retains its old content width after shrinking.
    scroll_->SetUseContentsPreferredSize(true);
    scroll_->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    SetFlexForView(scroll_, 1);
    status_ = Label(this, u"", true);
    status_->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::TLBR(8, 28, 12, 28)));
    if (auto *storage = ProfileStorage())
      storage_observation_.Observe(storage);
    if (auto *local_state = g_browser_process->local_state()) {
      local_state_registrar_.Init(local_state);
      for (const auto *pref : {prefs::kBrowserAddPersonEnabled,
                               prefs::kAllowFileSelectionDialogs}) {
        if (local_state->FindPreference(pref))
          local_state_registrar_.Add(
              pref, base::BindRepeating(&SettingsView::SyncControls,
                                        weak_factory_.GetWeakPtr()));
      }
    }
  }
  ~SettingsView() override {
    if (file_dialog_)
      file_dialog_->ListenerDestroyed();
    UnbindProfile();
  }
  void OnThemeChanged() override {
    views::BoxLayoutView::OnThemeChanged();
    const auto background =
        Dark(this) ? SkColorSetRGB(45, 45, 47) : SkColorSetRGB(245, 244, 244);
    SetBackground(views::CreateSolidBackground(background));
    if (scroll_)
      scroll_->SetBackgroundColor(background);
  }
  gfx::Size GetMinimumSize() const override { return gfx::Size(680, 480); }
  base::WeakPtr<SettingsView> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }
  Profile *profile() const { return profile_; }
  bool loading() const { return loading_; }
  SettingsPane pane() const { return pane_; }

  bool BindProfile(Profile *profile, std::optional<SettingsPane> pane) {
    if (!EditableProfile(profile))
      return false;
    auto keep_alive = ScopedProfileKeepAlive::TryAcquire(
        profile, ProfileKeepAliveOrigin::kSeoulSettingsWindow);
    if (!keep_alive)
      return false;
    if (!owner_->MoveToProfile(profile))
      return false;
    ++selection_generation_;
    UnbindProfile();
    profile_ = profile;
    keep_alive_ = std::move(keep_alive);
    profile_observation_.Observe(profile);
    prefs_registrar_.Init(profile->GetPrefs());
    const char *observed[] = {kSeoulLayoutModePref,
                              kSeoulUrlbarShowDomainOnlyInSidebarPref,
                              kSeoulDownloadAnimationEnabledPref,
                              prefs::kBrowserColorScheme,
                              prefs::kPromptForDownload,
                              prefs::kDownloadDefaultDirectory,
                              prefs::kProfileName,
                              prefs::kRestoreOnStartup,
                              adblock::kDefaultAdBlockModePref,
                              adblock::kDefaultFingerprintModePref,
                              kSeoulBoostsEnabledPref,
                              kSeoulBoostJavaScriptEnabledPref};
    for (const auto *pref : observed) {
      prefs_registrar_.Add(pref,
                           base::BindRepeating(&SettingsView::SyncControls,
                                               weak_factory_.GetWeakPtr()));
    }
    auto *organization =
        SeoulOrganizationServiceFactory::GetForProfile(profile);
    if (organization) {
      organization_model_ = &organization->model();
      organization_model_->AddObserver(this);
    }
    loading_ = false;
    ShowPane(pane.value_or(ReadPane(profile)));
    SyncControls();
    return true;
  }

  void ShowPane(SettingsPane pane) {
    if (!profile_ || loading_)
      return;
    pane_ = pane;
    profile_->GetPrefs()->SetInteger(kSettingsLastPanePref,
                                     static_cast<int>(pane));
    bindings_.clear();
    profile_buttons_.clear();
    containers_ = nullptr;
    profile_name_ = nullptr;
    download_path_ = nullptr;
    rename_button_ = nullptr;
    download_folder_button_ = nullptr;
    add_profile_button_ = nullptr;
    auto contents = Column(22);
    contents->SetInsideBorderInsets(gfx::Insets::VH(32, 32));
    auto *page = contents.get();
    page->GetViewAccessibility().SetRole(ax::mojom::Role::kTabPanel);
    page->GetViewAccessibility().SetName(
        std::u16string(kPaneNames[static_cast<int>(pane)]));
    for (size_t i = 0; i < categories_.size(); ++i)
      categories_[i]->Select(i == static_cast<size_t>(pane));
#if BUILDFLAG(IS_MAC)
    if (owner_->toolbar)
      owner_->toolbar->Select(pane);
#endif
    delegate_->SetTitle(std::u16string(kPaneNames[static_cast<int>(pane)]));
    if (GetWidget())
      GetWidget()->UpdateWindowTitle();
    switch (pane) {
    case SettingsPane::kGeneral:
      BuildGeneral(page);
      break;
    case SettingsPane::kAppearance:
      BuildAppearance(page);
      break;
    case SettingsPane::kProfiles:
      BuildProfiles(page);
      break;
    case SettingsPane::kShortcuts:
      BuildShortcuts(page);
      break;
    case SettingsPane::kPrivacy:
      BuildPrivacy(page);
      break;
    case SettingsPane::kAdvanced:
      BuildAdvanced(page);
      break;
    }
    scroll_->SetContents(std::move(contents));
    SyncControls();
    PreferredSizeChanged();
  }

  void OnProfileWillBeDestroyed(Profile *profile) override {
    if (profile_ != profile)
      return;
    UnbindProfile();
    if (GetWidget())
      GetWidget()->Close();
  }
  void OnProfileAdded(const base::FilePath &) override {
    ScheduleProfilesRefresh();
  }
  void OnProfileWasRemoved(const base::FilePath &path,
                           const std::u16string &) override {
    if (profile_ && profile_->GetPath() == path) {
      UnbindProfile();
      if (GetWidget())
        GetWidget()->Close();
    } else
      ScheduleProfilesRefresh();
  }
  void OnProfileNameChanged(const base::FilePath &,
                            const std::u16string &) override {
    SyncControls();
  }
  void OnProfileSigninRequiredChanged(const base::FilePath &path) override {
    if (profile_ && profile_->GetPath() == path && !EditableProfile(profile_)) {
      UnbindProfile();
      if (GetWidget())
        GetWidget()->Close();
    } else
      ScheduleProfilesRefresh();
  }
  void OnProfileIsOmittedChanged(const base::FilePath &path) override {
    OnProfileSigninRequiredChanged(path);
  }
  void OnOrganizationChanged(const OrganizationChange &) override {
    if (!containers_refresh_pending_) {
      containers_refresh_pending_ = true;
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&SettingsView::RefreshContainers,
                                    weak_factory_.GetWeakPtr()));
    }
  }
  void FileSelected(const ui::SelectedFileInfo &file, int) override {
    if (profile_ && profile_->GetPath() == folder_profile_ &&
        CanEdit(prefs::kDownloadDefaultDirectory) &&
        ChromeSelectFilePolicy::FileSelectDialogsAllowed() &&
        file.path().IsAbsolute()) {
      DownloadPrefs::FromBrowserContext(profile_)->SetDownloadPath(file.path());
    }
    file_dialog_.reset();
  }
  void FileSelectionCanceled() override { file_dialog_.reset(); }
  bool AcceleratorPressed(const ui::Accelerator &accelerator) override {
    if (accelerator.key_code() == ui::VKEY_W) {
      GetWidget()->Close();
      return true;
    }
    if (accelerator.key_code() == ui::VKEY_OEM_COMMA) {
      GetWidget()->Activate();
      return true;
    }
    return views::BoxLayoutView::AcceleratorPressed(accelerator);
  }
  void AddedToWidget() override {
    views::BoxLayoutView::AddedToWidget();
    AddAccelerator(ui::Accelerator(ui::VKEY_W, ui::EF_PLATFORM_ACCELERATOR));
    AddAccelerator(
        ui::Accelerator(ui::VKEY_OEM_COMMA, ui::EF_PLATFORM_ACCELERATOR));
    SyncControls();
  }

private:
  void MoveCategory(size_t from, ui::KeyboardCode key) {
    if (!profile_ || loading_)
      return;
    size_t target = from;
    if (key == ui::VKEY_HOME)
      target = 0;
    else if (key == ui::VKEY_END)
      target = categories_.size() - 1;
    else {
      const bool forward = (key == ui::VKEY_RIGHT) != base::i18n::IsRTL();
      target =
          (from + (forward ? 1 : categories_.size() - 1)) % categories_.size();
    }
    ShowPane(static_cast<SettingsPane>(target));
    categories_[target]->RequestFocus();
  }
  struct Binding {
    std::string pref;
    raw_ptr<views::ToggleButton> toggle = nullptr;
    raw_ptr<views::Combobox> choice = nullptr;
    std::vector<int> values;
    raw_ptr<views::Label> policy = nullptr;
  };
  void UnbindProfile() {
    prefs_registrar_.RemoveAll();
    profile_observation_.Reset();
    if (organization_model_)
      organization_model_->RemoveObserver(this);
    organization_model_ = nullptr;
    profile_ = nullptr;
    keep_alive_.reset();
  }
  bool CanEdit(const std::string &name) const {
    if (!profile_ || loading_)
      return false;
    const auto *pref = profile_->GetPrefs()->FindPreference(name);
    return pref && pref->IsUserModifiable();
  }
  views::BoxLayoutView *Row(views::View *parent, const std::u16string &title,
                            const std::u16string &detail = u"") {
    auto *row = parent->AddChildView(std::make_unique<views::BoxLayoutView>());
    row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    row->SetBetweenChildSpacing(20);
    row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    row->SetInsideBorderInsets(gfx::Insets::VH(9, 0));
    auto *labels = row->AddChildView(Column(4));
    if (!title.empty())
      Label(labels, title);
    if (!detail.empty())
      Label(labels, detail, true);
    row->SetFlexForView(labels, 1);
    return row;
  }
  void Toggle(views::View *parent, const char *pref,
              const std::u16string &title, const std::u16string &detail = u"") {
    auto *row = Row(parent, title, detail);
    auto *toggle = row->AddChildView(std::make_unique<SettingsToggle>(
        base::BindRepeating(&SettingsView::TogglePref,
                            weak_factory_.GetWeakPtr(), std::string(pref))));
    toggle->GetViewAccessibility().SetName(title);
    auto *policy = Label(parent, u"Managed by your organization", true);
    policy->SetVisible(false);
    bindings_.push_back({pref, toggle, nullptr, {}, policy});
  }
  void Choice(views::View *parent, const char *pref,
              const std::u16string &title, std::vector<std::u16string> labels,
              std::vector<int> values, const std::u16string &detail = u"") {
    auto *row = Row(parent, title, detail);
    std::vector<ui::SimpleComboboxModel::Item> items;
    for (auto &label : labels)
      items.emplace_back(std::move(label));
    auto *choice = row->AddChildView(std::make_unique<views::Combobox>(
        std::make_unique<ui::SimpleComboboxModel>(std::move(items))));
    choice->SetPreferredSize(gfx::Size(208, 32));
    choice->SetTooltipTextAndAccessibleName(title);
    choice->SetCallback(base::BindRepeating(&SettingsView::ChoosePref,
                                            weak_factory_.GetWeakPtr(),
                                            std::string(pref)));
    auto *policy = Label(parent, u"Managed by your organization", true);
    policy->SetVisible(false);
    bindings_.push_back({pref, nullptr, choice, std::move(values), policy});
  }
  void TogglePref(std::string pref) {
    if (!CanEdit(pref)) {
      SyncControls();
      return;
    }
    profile_->GetPrefs()->SetBoolean(pref,
                                     !profile_->GetPrefs()->GetBoolean(pref));
    SyncControls();
  }
  void ChoosePref(std::string pref) {
    if (!CanEdit(pref)) {
      SyncControls();
      return;
    }
    auto it = std::ranges::find(bindings_, pref, &Binding::pref);
    if (it == bindings_.end() || !it->choice->GetSelectedIndex())
      return;
    const size_t index = *it->choice->GetSelectedIndex();
    if (index >= it->values.size())
      return;
    ChooseValue(pref, it->values[index]);
  }
  void ChooseValue(std::string pref, int value) {
    if (!CanEdit(pref)) {
      SyncControls();
      return;
    }
    if (pref == prefs::kBrowserColorScheme) {
      ThemeServiceFactory::GetForProfile(profile_)->SetBrowserColorScheme(
          static_cast<ThemeService::BrowserColorScheme>(value));
    } else if (pref == adblock::kDefaultAdBlockModePref) {
      adblock::AdBlockServiceFactory::GetForProfile(profile_)->SetDefaultMode(
          static_cast<adblock::AdBlockMode>(value));
    } else if (pref == adblock::kDefaultFingerprintModePref) {
      adblock::AdBlockServiceFactory::GetForProfile(profile_)
          ->SetDefaultFingerprintMode(
              static_cast<adblock::FingerprintMode>(value));
    } else {
      profile_->GetPrefs()->SetInteger(pref, value);
    }
    SyncControls();
  }
  void SyncControls() {
    if (!profile_)
      return;
    if (GetWidget()) {
      const int mode =
          profile_->GetPrefs()->GetInteger(prefs::kBrowserColorScheme);
      GetWidget()->SetColorModeOverride(
          mode == 1   ? ui::ColorProviderKey::ColorMode::kLight
          : mode == 2 ? std::optional(ui::ColorProviderKey::ColorMode::kDark)
                      : std::nullopt);
    }
    for (auto &binding : bindings_) {
      const bool can_edit = CanEdit(binding.pref);
      if (binding.toggle) {
        const bool enabled = profile_->GetPrefs()->GetBoolean(binding.pref);
        // A pointer click already started the native switch animation. Only
        // overwrite state when an external preference change disagrees.
        if (binding.toggle->GetIsOn() != enabled)
          binding.toggle->SetIsOn(enabled);
        binding.toggle->SetEnabled(can_edit);
      } else if (binding.choice) {
        const int value = profile_->GetPrefs()->GetInteger(binding.pref);
        const auto found = std::ranges::find(binding.values, value);
        binding.choice->SetSelectedIndex(
            found == binding.values.end()
                ? std::nullopt
                : std::optional<size_t>(found - binding.values.begin()));
        binding.choice->SetEnabled(can_edit);
      }
      binding.policy->SetVisible(!loading_ && !can_edit);
      if (!can_edit) {
        const auto *pref = profile_->GetPrefs()->FindPreference(binding.pref);
        binding.policy->SetText(pref && pref->IsExtensionControlled()
                                    ? u"Controlled by an extension"
                                    : u"Managed by your organization");
      }
    }
    const auto name = ProfileName(profile_);
    if (profile_name_)
      profile_name_->SetText(name);
    if (rename_button_)
      rename_button_->SetEnabled(CanEdit(prefs::kProfileName));
    if (download_folder_button_)
      download_folder_button_->SetEnabled(
          CanEdit(prefs::kDownloadDefaultDirectory) &&
          ChromeSelectFilePolicy::FileSelectDialogsAllowed());
    if (add_profile_button_)
      add_profile_button_->SetEnabled(!loading_ &&
                                      profiles::IsProfileCreationAllowed());
    if (scroll_->contents())
      scroll_->contents()->SetEnabled(!loading_);
    if (download_path_)
      download_path_->SetText(
          profile_->GetPrefs()
              ->GetFilePath(prefs::kDownloadDefaultDirectory)
              .LossyDisplayName());
    if (auto *storage = ProfileStorage()) {
      for (auto &[path, button] : profile_buttons_) {
        if (auto *entry = storage->GetProfileAttributesWithPath(path)) {
          button->SetText(entry->GetName());
          button->GetViewAccessibility().SetIsSelected(path ==
                                                       profile_->GetPath());
          button->SetBgColorOverrideDeprecated(path == profile_->GetPath()
                                                   ? Selection(this)
                                                   : SK_ColorTRANSPARENT);
          button->SetEnabledTextColors(
              path == profile_->GetPath()
                  ? ui::ColorVariant(Accent(this))
                  : ui::ColorVariant(ui::kColorLabelForeground));
        }
      }
    }
    SetStatus(loading_ ? u"Loading profile…" : u"");
  }
  void SetStatus(const std::u16string &text) {
    status_->SetText(text);
    status_->SetVisible(!text.empty());
  }
  void BuildGeneral(views::View *page) {
    auto *startup = page->AddChildView(std::make_unique<SettingsSection>());
    Choice(startup, prefs::kRestoreOnStartup, u"On startup",
           {u"Open a new tab", u"Continue where you left off",
            u"Open specific pages", u"Continue and open pages"},
           {5, 1, 4, 6});
    AdvancedLink(startup, u"Startup pages",
                 u"Choose the pages to open at launch.", "onStartup");
    page->AddChildView(Divider());
    auto *browsing = page->AddChildView(std::make_unique<SettingsSection>());
    AdvancedLink(browsing, u"Default search engine",
                 u"Manage search engines and site search.", "search");
    browsing->AddChildView(Divider());
    AdvancedLink(browsing, u"Default browser",
                 u"Choose Seoul as your default browser.", "defaultBrowser");
    page->AddChildView(Divider());
    auto *access = page->AddChildView(std::make_unique<SettingsSection>());
    AdvancedLink(access, u"Languages",
                 u"Preferred website languages and translation.", "languages");
    access->AddChildView(Divider());
    AdvancedLink(access, u"Accessibility",
                 u"Text, captions, keyboard navigation, and other "
                 u"accessibility preferences.",
                 "accessibility");
  }
  void BuildAppearance(views::View *page) {
    auto *layout = page->AddChildView(std::make_unique<SettingsSection>());
    Choice(layout, kSeoulLayoutModePref, u"Browser layout",
           {u"Sidebar", u"Top toolbar", u"Compact sidebar"}, {0, 1, 2});
    Toggle(layout, kSeoulUrlbarShowDomainOnlyInSidebarPref,
           u"Show only the site in the sidebar",
           u"The full address appears when you edit it.");
    page->AddChildView(Divider());
    auto *appearance = page->AddChildView(std::make_unique<SettingsSection>());
    Choice(appearance, prefs::kBrowserColorScheme, u"Appearance",
           {u"System", u"Light", u"Dark"}, {0, 1, 2});
    AdvancedLink(appearance, u"Fonts and page zoom", u"", "appearance");
    page->AddChildView(Divider());
    Toggle(page, kSeoulDownloadAnimationEnabledPref, u"Animate new downloads",
           u"Follows your system's Reduce Motion setting.");
  }
  void BuildProfiles(views::View *page) {
    Label(page, u"Profiles keep sign-ins, history, passwords, and extensions "
                u"separate. Select a profile to manage its settings and "
                u"container spaces.");
    auto *split = page->AddChildView(std::make_unique<views::BoxLayoutView>());
    split->SetBetweenChildSpacing(32);
    split->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStart);
    auto *list = split->AddChildView(std::make_unique<ProfileListView>());
    auto *heading = Label(list, u"Your profiles", true);
    heading->SetFontList(heading->font_list().Derive(
        0, gfx::Font::NORMAL, gfx::Font::Weight::SEMIBOLD));
    heading->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(2, 4)));
    list->AddChildView(Divider());
    if (auto *storage = ProfileStorage()) {
      for (auto *entry : storage->GetAllProfilesAttributesSortedForDisplay()) {
        if (entry->IsOmitted() || entry->IsSigninRequired())
          continue;
        const auto path = entry->GetPath();
        auto *button =
            Button(list, entry->GetName(),
                   base::BindRepeating(&SettingsView::SelectProfile,
                                       weak_factory_.GetWeakPtr(), path));
        button->SetHorizontalAlignment(gfx::ALIGN_LEFT);
        button->SetStyle(ui::ButtonStyle::kText);
        button->SetStrokeColorOverrideDeprecated(SK_ColorTRANSPARENT);
        button->SetMinSize(gfx::Size(0, 34));
        button->SetElideBehavior(gfx::ELIDE_TAIL);
        button->SetTooltipText(entry->GetName());
        profile_buttons_.emplace_back(path, button);
      }
    }
    auto *spacer = list->AddChildView(std::make_unique<views::View>());
    list->SetFlexForView(spacer, 1);
    list->AddChildView(Divider());
    add_profile_button_ =
        Button(list, u"Add profile…",
               base::BindRepeating(&SettingsView::AddProfile,
                                   weak_factory_.GetWeakPtr()));
    auto *details = split->AddChildView(Column(16));
    split->SetFlexForView(details, 1);
    auto *identity = details->AddChildView(std::make_unique<SettingsSection>());
    auto *name_row = Row(identity, u"Profile name");
    profile_name_ = Label(name_row, ProfileName(profile_));
    profile_name_->SetMultiLine(false);
    profile_name_->SetElideBehavior(gfx::ELIDE_TAIL);
    profile_name_->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
    profile_name_->SetMaximumWidthSingleLine(180);
    rename_button_ = Button(Row(identity, u""), u"Rename profile…",
                            base::BindRepeating(&SettingsView::RenameProfile,
                                                weak_factory_.GetWeakPtr()));
    details->AddChildView(Divider());
    auto *downloads =
        details->AddChildView(std::make_unique<SettingsSection>());
    auto *download_row = Row(downloads, u"Download folder");
    download_folder_button_ =
        Button(download_row, u"Choose…",
               base::BindRepeating(&SettingsView::ChooseFolder,
                                   weak_factory_.GetWeakPtr()));
    download_folder_button_->GetViewAccessibility().SetName(
        u"Choose download folder…");
    download_path_ = Label(downloads, u"", true);
    download_path_->SetMultiLine(false);
    download_path_->SetElideBehavior(gfx::ELIDE_MIDDLE);
    download_folder_button_->SetEnabled(
        CanEdit(prefs::kDownloadDefaultDirectory));
    Toggle(downloads, prefs::kPromptForDownload,
           u"Ask where to save each file");
    details->AddChildView(Divider());
    auto *spaces = details->AddChildView(std::make_unique<SettingsSection>());
    auto *add_container =
        Button(Row(spaces, u"Container spaces"), u"Add…",
               base::BindRepeating(&SettingsView::AddContainer,
                                   weak_factory_.GetWeakPtr()));
    add_container->GetViewAccessibility().SetName(u"Add container space…");
    Label(spaces,
          u"Separate site sign-ins and storage. History, passwords, "
          u"bookmarks, and extensions remain shared.",
          true);
    containers_ = spaces->AddChildView(Column(6));
    containers_->SetInsideBorderInsets(gfx::Insets::VH(12, 0));
    RefreshContainers();
  }
  void BuildShortcuts(views::View *page) {
    auto *shortcuts_card =
        page->AddChildView(std::make_unique<SettingsSection>());
    const std::pair<const char16_t *, const char16_t *> shortcuts[] = {
        {u"New tab", u"⌘T"},
        {u"Close tab", u"⌘W"},
        {u"Reopen closed tab", u"⇧⌘T"},
        {u"Edit address", u"⌘L"},
        {u"Find on page", u"⌘F"},
        {u"Reload page", u"⌘R"},
        {u"Settings", u"⌘,"}};
    for (const auto &[name, keys] : shortcuts) {
      if (shortcuts_card->children().size())
        shortcuts_card->AddChildView(Divider());
      auto *key = Label(Row(shortcuts_card, name), keys);
      key->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(3, 7)));
      key->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
    }
  }
  void BuildPrivacy(views::View *page) {
    auto *protection = page->AddChildView(std::make_unique<SettingsSection>());
    Choice(protection, adblock::kDefaultAdBlockModePref, u"Ads and trackers",
           {u"Off", u"Standard", u"Aggressive"}, {0, 1, 2});
    protection->AddChildView(Divider());
    Choice(protection, adblock::kDefaultFingerprintModePref,
           u"Fingerprinting protection", {u"Off", u"Balanced", u"Strict"},
           {0, 1, 2},
           u"Strict protection can affect sites that read canvas images.");
    Label(page,
          u"These are the profile defaults. Open Site controls beside a site's "
          u"address to manage its exceptions.",
          true);
    page->AddChildView(Divider());
    auto *sites = page->AddChildView(std::make_unique<SettingsSection>());
    AdvancedLink(sites, u"Site permissions",
                 u"Camera, microphone, location, notifications, and other "
                 u"website access.",
                 "content");
    sites->AddChildView(Divider());
    AdvancedLink(sites, u"Clear browsing data",
                 u"Choose which browsing data to remove.", "clearBrowserData");
  }
  void BuildAdvanced(views::View *page) {
    auto *boosts = page->AddChildView(std::make_unique<SettingsSection>());
    Toggle(boosts, kSeoulBoostsEnabledPref, u"Enable saved Boosts",
           u"Apply your saved appearance changes to matching websites.");
    boosts->AddChildView(Divider());
    Toggle(boosts, kSeoulBoostJavaScriptEnabledPref,
           u"Allow JavaScript in Boosts",
           u"Saved scripts can read and change matching pages. Reload pages "
           u"after disabling to clear existing effects.");
    page->AddChildView(Divider());
    auto *preferences = page->AddChildView(std::make_unique<SettingsSection>());
    AdvancedLink(preferences, u"All browser preferences",
                 u"Open advanced settings, including passwords, network, "
                 u"languages, and reset options.",
                 "");
  }
  void AdvancedLink(views::View *page, const std::u16string &title,
                    const std::u16string &detail, std::string route) {
    auto *button = Button(Row(page, title, detail), u"Open…",
                          base::BindRepeating(&SettingsView::OpenAdvanced,
                                              weak_factory_.GetWeakPtr(),
                                              std::move(route)));
    button->GetViewAccessibility().SetName(u"Open " + title);
  }
  void OpenAdvanced(std::string route) {
    if (profile_ && !loading_)
      chrome::ShowSettingsSubPageForProfile(profile_, route);
  }
  void SelectProfile(base::FilePath path) {
    if (!profile_ || file_dialog_ || path == profile_->GetPath())
      return;
    auto *manager = g_browser_process->profile_manager();
    auto *entry = manager ? manager->GetProfileAttributesStorage()
                                .GetProfileAttributesWithPath(path)
                          : nullptr;
    if (!entry || entry->IsOmitted() || entry->IsSigninRequired())
      return;
    const uint64_t generation = ++selection_generation_;
    loading_ = true;
    SyncControls();
    manager->LoadProfileByPath(path, false,
                               base::BindOnce(&SettingsView::ProfileLoaded,
                                              weak_factory_.GetWeakPtr(),
                                              generation));
  }
  void ProfileLoaded(uint64_t generation, Profile *profile) {
    if (generation != selection_generation_)
      return;
    if (!BindProfile(profile, SettingsPane::kProfiles)) {
      loading_ = false;
      SyncControls();
      SetStatus(u"This profile could not be opened. Try again.");
      status_->GetViewAccessibility().AnnounceAlert(status_->GetText());
    }
  }
  void AddProfile() {
    if (!profile_ || loading_ || file_dialog_ ||
        !profiles::IsProfileCreationAllowed())
      return;
    ShowWorkspaceNameDialog(GetWidget()->GetNativeWindow(), u"New profile",
                            u"Profile name", u"",
                            base::BindOnce(&SettingsView::CreateProfile,
                                           weak_factory_.GetWeakPtr()),
                            u"Start with separate sign-ins, browsing history, "
                            u"passwords, and extensions.");
  }
  void CreateProfile(std::string name) {
    if (!profile_ || loading_ || !profiles::IsProfileCreationAllowed())
      return;
    loading_ = true;
    const auto generation = ++selection_generation_;
    SyncControls();
    ProfileManager::CreateMultiProfileAsync(
        base::UTF8ToUTF16(name), 0, false,
        base::BindOnce(&SettingsView::ProfileLoaded, weak_factory_.GetWeakPtr(),
                       generation));
  }
  void RenameProfile() {
    if (!CanEdit(prefs::kProfileName))
      return;
    const auto path = profile_->GetPath();
    ShowWorkspaceNameDialog(GetWidget()->GetNativeWindow(), u"Rename profile",
                            u"Profile name", ProfileName(profile_),
                            base::BindOnce(
                                [](base::WeakPtr<SettingsView> view,
                                   base::FilePath path, std::string name) {
                                  if (!view || !view->profile_ ||
                                      view->profile_->GetPath() != path ||
                                      !view->CanEdit(prefs::kProfileName))
                                    return;
                                  profiles::UpdateProfileName(
                                      view->profile_, base::UTF8ToUTF16(name));
                                },
                                weak_factory_.GetWeakPtr(), path));
  }
  void ChooseFolder() {
    if (!CanEdit(prefs::kDownloadDefaultDirectory) || file_dialog_)
      return;
    if (!ChromeSelectFilePolicy::FileSelectDialogsAllowed()) {
      SetStatus(u"File selection is disabled by your organization.");
      status_->GetViewAccessibility().AnnounceAlert(status_->GetText());
      return;
    }
    folder_profile_ = profile_->GetPath();
    file_dialog_ = ui::SelectFileDialog::Create(
        this, std::make_unique<ChromeSelectFilePolicy>(nullptr));
    file_dialog_->SelectFile(
        ui::SelectFileDialog::SELECT_FOLDER, u"Download folder",
        profile_->GetPrefs()->GetFilePath(prefs::kDownloadDefaultDirectory),
        nullptr, 0, {}, GetWidget()->GetNativeWindow());
  }
  void AddContainer() {
    if (!profile_ || loading_ || !organization_model_)
      return;
    const auto path = profile_->GetPath();
    ShowWorkspaceNameDialog(
        GetWidget()->GetNativeWindow(), u"New container space", u"Space name",
        u"",
        base::BindOnce(
            [](base::WeakPtr<SettingsView> view, base::FilePath path,
               std::string name) {
              if (!view || !view->profile_ ||
                  view->profile_->GetPath() != path ||
                  !view->organization_model_ || view->loading_)
                return;
              auto result =
                  view->organization_model_->CreateWorkspace(name, true);
              if (!result.has_value()) {
                view->SetStatus(u"The container space could not be created.");
                view->status_->GetViewAccessibility().AnnounceAlert(
                    view->status_->GetText());
              }
            },
            weak_factory_.GetWeakPtr(), path),
        u"Website sign-ins and site storage stay separate. Other browsing data "
        u"remains shared within this profile.");
  }
  void RefreshContainers() {
    containers_refresh_pending_ = false;
    if (!containers_ || !organization_model_)
      return;
    containers_->RemoveAllChildViews();
    bool any = false;
    for (const auto &workspace : organization_model_->ToSnapshot().workspaces) {
      if (!workspace.isolated || workspace.archived)
        continue;
      Label(containers_, base::UTF8ToUTF16(workspace.name));
      any = true;
    }
    if (!any)
      Label(containers_, u"No container spaces yet", true);
  }
  void ScheduleProfilesRefresh() {
    if (profiles_refresh_pending_)
      return;
    profiles_refresh_pending_ = true;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<SettingsView> view) {
              if (!view)
                return;
              view->profiles_refresh_pending_ = false;
              if (view->pane_ == SettingsPane::kProfiles && !view->loading_)
                view->ShowPane(SettingsPane::kProfiles);
            },
            weak_factory_.GetWeakPtr()));
  }

  raw_ptr<views::DialogDelegate> delegate_;
  raw_ptr<SettingsWindowState> owner_;
  raw_ptr<Profile> profile_ = nullptr;
  std::unique_ptr<ScopedProfileKeepAlive> keep_alive_;
  PrefChangeRegistrar prefs_registrar_;
  PrefChangeRegistrar local_state_registrar_;
  base::ScopedObservation<Profile, ProfileObserver> profile_observation_{this};
  base::ScopedObservation<ProfileAttributesStorage,
                          ProfileAttributesStorageObserver>
      storage_observation_{this};
  raw_ptr<OrganizationModel> organization_model_ = nullptr;
  std::vector<raw_ptr<CategoryButton>> categories_;
  std::vector<Binding> bindings_;
  std::vector<std::pair<base::FilePath, raw_ptr<views::MdTextButton>>>
      profile_buttons_;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  raw_ptr<views::BoxLayoutView> containers_ = nullptr;
  raw_ptr<views::Label> status_ = nullptr;
  raw_ptr<views::Label> profile_name_ = nullptr;
  raw_ptr<views::Label> download_path_ = nullptr;
  raw_ptr<views::MdTextButton> rename_button_ = nullptr;
  raw_ptr<views::MdTextButton> download_folder_button_ = nullptr;
  raw_ptr<views::MdTextButton> add_profile_button_ = nullptr;
  scoped_refptr<ui::SelectFileDialog> file_dialog_;
  base::FilePath folder_profile_;
  SettingsPane pane_ = SettingsPane::kAppearance;
  uint64_t selection_generation_ = 0;
  bool loading_ = false;
  bool profiles_refresh_pending_ = false;
  bool containers_refresh_pending_ = false;
  base::WeakPtrFactory<SettingsView> weak_factory_{this};
};
BEGIN_METADATA(SettingsView)
END_METADATA

bool SettingsWindowState::MoveToProfile(Profile *profile) {
  // During construction the caller still owns us. Later, switching profiles
  // transfers this same window to the selected profile's lifetime owner.
  if (!owner_profile || owner_profile == profile)
    return true;
  auto *previous =
      SeoulRuntimeServiceFactory::GetForProfileIfExists(owner_profile);
  auto *next = SeoulRuntimeServiceFactory::GetForProfile(profile);
  if (!previous || !next || !next->CanOpenSettingsWindow() ||
      previous->settings_window() != this)
    return false;
  auto self = previous->TakeSettingsWindow();
  owner_profile = profile;
  next->SetSettingsWindow(std::move(self));
  return true;
}

} // namespace

bool ShowSeoulSettings(Profile *profile, std::optional<SettingsPane> pane) {
  if (!profile)
    return false;
  profile = profile->GetOriginalProfile();
  if (!EditableProfile(profile))
    return false;
  if (OpenWindow() && OpenWindow()->GetWidget() &&
      !OpenWindow()->GetWidget()->IsClosed()) {
    auto window = OpenWindow();
    if (window->profile() != profile || window->loading()) {
      if (!window->BindProfile(profile, pane))
        return false;
    } else if (pane)
      window->ShowPane(*pane);
    window->GetWidget()->Show();
    window->GetWidget()->Activate();
    return true;
  }
  auto *runtime = SeoulRuntimeServiceFactory::GetForProfile(profile);
  if (!runtime || !runtime->CanOpenSettingsWindow())
    return false;
  auto owner = std::make_unique<SettingsWindowState>();
  auto &state = *owner;
  auto delegate = std::make_unique<views::DialogDelegate>();
  delegate->SetTitle(u"Settings");
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetModalType(ui::mojom::ModalType::kNone);
  delegate->SetCanResize(true);
  delegate->SetCanMinimize(false);
  delegate->set_use_custom_frame(false);
  delegate->SetOwnershipOfNewWidget(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  delegate->set_margins(gfx::Insets());
  auto *view = delegate->SetContentsView(
      std::make_unique<SettingsView>(delegate.get(), &state));
  if (!view->BindProfile(profile, pane))
    return false;
  state.view = view->GetWeakPtr();
  state.delegate = std::move(delegate);
  state.widget.reset(views::DialogDelegate::CreateDialogWidget(
      state.delegate.get(), gfx::NativeWindow(), gfx::NativeView()));
  state.widget->MakeCloseSynchronous(
      base::BindOnce([](SettingsWindowState *owner,
                        views::Widget::ClosedReason) { owner->Reset(); },
                     base::Unretained(&state)));
  state.owner_profile = profile;
  runtime->SetSettingsWindow(std::move(owner));
#if BUILDFLAG(IS_MAC)
  state.toolbar = CreateSettingsToolbarMac(
      state.widget.get(), view->pane(),
      base::BindRepeating(&SettingsView::ShowPane, state.view));
#endif
  state.widget->Show();
  return true;
}

bool ActivateSeoulSettingsPaneForTesting(SettingsPane pane) {
  auto view = OpenWindow();
  if (!view)
    return false;
#if BUILDFLAG(IS_MAC)
  auto *runtime =
      SeoulRuntimeServiceFactory::GetForProfileIfExists(view->profile());
  auto *owner = static_cast<SettingsWindowState *>(runtime->settings_window());
  return owner->toolbar && owner->toolbar->ActivateForTesting(pane);
#else
  view->ShowPane(pane);
  return true;
#endif
}

views::Widget *GetSeoulSettingsWidgetForTesting() {
  return OpenWindow() ? OpenWindow()->GetWidget() : nullptr;
}
Profile *GetSeoulSettingsProfileForTesting() {
  return OpenWindow() ? OpenWindow()->profile() : nullptr;
}

} // namespace seoul
