// Project Seoul Chromium Preview host service and bubble.

#include "seoul/browser/preview/preview_host_service.h"

#include <algorithm>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_tab_helper_factory.h"
// nogncheck: //chrome/browser/ui reaches this target through the side-panel
// Canvas registration, so a declared dep would be a dependency cycle; the
// symbols link through //chrome/browser like the other circular includes.
#include "chrome/browser/ui/browser.h" // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
// nogncheck: //chrome/browser/ui reaches this target through the side-panel
// Canvas registration, so a declared dep would be a dependency cycle; the
// symbols link through //chrome/browser like the other circular includes.
#include "chrome/browser/ui/views/frame/browser_view.h" // nogncheck
#include "components/sessions/content/session_tab_helper.h"
#include "components/sessions/core/session_id.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "seoul/browser/lifecycle/lifecycle_coordinator.h"
#include "seoul/browser/preview/preview_manager.h"
#include "seoul/browser/preview/seoul_preview_web_view.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/animation/animation.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/widget/widget.h"

namespace seoul {

namespace {

BrowserWindowInterface *ResolveBrowser(Profile *profile, LiveWindowKey window) {
  if (!profile || !window.is_valid()) {
    return nullptr;
  }
  ProfileBrowserCollection *collection =
      ProfileBrowserCollection::GetForProfile(profile);
  BrowserWindowInterface *browser =
      collection ? collection->FindBrowserWithID(
                       SessionID::FromSerializedValue(window.session_id()))
                 : nullptr;
  return browser && browser->GetProfile() == profile &&
                 !browser->IsDeleteScheduled() &&
                 browser->GetType() == BrowserWindowInterface::TYPE_NORMAL
             ? browser
             : nullptr;
}

int FindTabIndex(TabStripModel *model, LiveTabKey tab) {
  if (!model || !tab.is_valid()) {
    return TabStripModel::kNoTab;
  }
  const SessionID target = SessionID::FromSerializedValue(tab.session_id());
  for (int index = 0; index < model->count(); ++index) {
    content::WebContents *contents = model->GetWebContentsAt(index);
    if (contents && sessions::SessionTabHelper::IdForTab(contents) == target) {
      return index;
    }
  }
  return TabStripModel::kNoTab;
}

bool IsSafePreviewUrl(const GURL &url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && url.has_host() &&
         !url.has_username() && !url.has_password();
}

} // namespace

PreviewPromotionRoute::PreviewPromotionRoute() = default;
PreviewPromotionRoute::PreviewPromotionRoute(const PreviewPromotionRoute &) =
    default;
PreviewPromotionRoute::PreviewPromotionRoute(PreviewPromotionRoute &&) =
    default;
PreviewPromotionRoute &
PreviewPromotionRoute::operator=(const PreviewPromotionRoute &) = default;
PreviewPromotionRoute &
PreviewPromotionRoute::operator=(PreviewPromotionRoute &&) = default;
PreviewPromotionRoute::~PreviewPromotionRoute() = default;

class SeoulPreviewBubbleView : public views::BubbleDialogDelegateView,
                               public content::WebContentsObserver {
public:
  using ClosedCallback =
      base::RepeatingCallback<void(PreviewId, LiveWindowKey)>;

  SeoulPreviewBubbleView(views::View *anchor, Profile *profile,
                         LifecycleCoordinator *lifecycle,
                         PreviewPromotionRouteResolver route_resolver,
                         PreviewWorkspaceSwitcher workspace_switcher,
                         PreviewManager *manager, PreviewRecord record,
                         std::unique_ptr<content::WebContents> contents,
                         ClosedCallback closed_callback)
      : views::BubbleDialogDelegateView(anchor, views::BubbleBorder::NONE,
                                        views::BubbleBorder::DIALOG_SHADOW,
                                        /*autosize=*/false),
        profile_(profile), lifecycle_(lifecycle),
        route_resolver_(std::move(route_resolver)),
        workspace_switcher_(std::move(workspace_switcher)), manager_(manager),
        record_(std::move(record)), contents_(std::move(contents)),
        closed_callback_(std::move(closed_callback)) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    SetShowCloseButton(false);
    set_close_on_deactivate(false);
    SetAccessibleWindowRole(ax::mojom::Role::kDialog);
    SetTitle(u"Link preview");
  }

  ~SeoulPreviewBubbleView() override {
    Observe(nullptr);
    if (web_view_) {
      web_view_->SetWebContents(nullptr);
    }
    if (contents_) {
      contents_->SetDelegate(nullptr);
    }
  }

  void Init() override {
    auto *layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
    layout->SetOrientation(views::LayoutOrientation::kVertical);

    auto header = std::make_unique<views::View>();
    header->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(8, 10), 8));
    auto *title =
        header->AddChildView(std::make_unique<views::Label>(u"Preview"));
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                                 views::MaximumFlexSizeRule::kUnbounded));
    status_label_ = header->AddChildView(std::make_unique<views::Label>(
        u"Loading · not saved as a tab", views::style::CONTEXT_LABEL,
        views::style::STYLE_SECONDARY));
    status_label_->GetViewAccessibility().SetLiveRegionContainer(
        views::ViewAccessibility::LiveRegionStatus::kPolite);
    tab_button_ = header->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&SeoulPreviewBubbleView::PromoteToTab,
                            weak_factory_.GetWeakPtr()),
        u"Open as tab"));
    split_button_ = header->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&SeoulPreviewBubbleView::PromoteToSplit,
                            weak_factory_.GetWeakPtr()),
        u"Move to split"));
    SetPromotionActionsEnabled(false);
    header->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&SeoulPreviewBubbleView::Dismiss,
                            weak_factory_.GetWeakPtr()),
        u"Close"));
    AddChildView(std::move(header));

    auto web_view = std::make_unique<SeoulPreviewWebView>(profile_);
    web_view_ = web_view.get();
    const gfx::Size anchor_size = GetAnchorView()->size();
    web_view_->SetPreferredSize(
        gfx::Size(std::clamp(anchor_size.width() * 4 / 5, 480, 1100),
                  std::clamp(anchor_size.height() * 4 / 5, 360, 780)));
    contents_->SetDelegate(web_view_);
    web_view_->SetWebContents(contents_.get());
    web_view_->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                                 views::MaximumFlexSizeRule::kUnbounded));
    AddChildView(std::move(web_view));
    Observe(contents_.get());

    content::NavigationController::LoadURLParams params(record_.initial_url);
    params.transition_type = ui::PAGE_TRANSITION_LINK;
    contents_->GetController().LoadURLWithParams(params);
  }

  void CloseSuperseded() {
    terminal_ = true;
    CloseBubble();
  }

  void DismissWithReason(PreviewDismissReason reason) {
    if (terminal_) {
      return;
    }
    if (manager_->Find(record_.id)) {
      const PreviewResult<PreviewRecord> dismissed =
          manager_->Dismiss(record_.id, reason);
      // Promotion owns the record while native ownership is transferring.
      // A concurrent parent/window notification must not close the bubble and
      // strand the manager in kPromoting; the promotion path will finish or
      // abort it on this same UI sequence.
      if (!dismissed.has_value()) {
        return;
      }
    }
    terminal_ = true;
    if (reason != PreviewDismissReason::kParentTabRemoved &&
        reason != PreviewDismissReason::kWindowClosed) {
      RestoreParentFocus();
    }
    CloseBubble();
  }

  void WindowClosing() override {
    if (!terminal_ && manager_->Find(record_.id)) {
      std::ignore =
          manager_->Dismiss(record_.id, PreviewDismissReason::kUserDismissed);
      RestoreParentFocus();
    }
    terminal_ = true;
    closed_callback_.Run(record_.id, record_.window);
    views::BubbleDialogDelegateView::WindowClosing();
  }

  void
  DidFinishNavigation(content::NavigationHandle *navigation_handle) override {
    if (!navigation_handle->HasCommitted() ||
        !navigation_handle->IsInPrimaryMainFrame() || terminal_) {
      return;
    }
    const PreviewRecord *current = manager_->Find(record_.id);
    if (!current) {
      return;
    }
    if (navigation_handle->GetURL() != current->current_url) {
      if (!manager_->DidNavigate(record_.id, navigation_handle->GetURL())
               .has_value()) {
        DismissWithReason(PreviewDismissReason::kUserDismissed);
        return;
      }
    }
    if (navigation_handle->IsErrorPage()) {
      std::ignore = manager_->MarkFailed(record_.id);
      SetPreviewStatus(u"Could not load this page", false);
      return;
    }
    const PreviewRecord *navigated = manager_->Find(record_.id);
    if (navigated && navigated->state == PreviewState::kLoading &&
        manager_->MarkReady(record_.id).has_value()) {
      SetPreviewStatus(u"Ready · not saved as a tab", true);
    }
  }

  void
  DidStartNavigation(content::NavigationHandle *navigation_handle) override {
    if (!navigation_handle->IsInPrimaryMainFrame() || terminal_) {
      return;
    }
    if (!IsSafePreviewUrl(navigation_handle->GetURL())) {
      contents_->Stop();
      DismissWithReason(PreviewDismissReason::kUserDismissed);
      return;
    }
    if (manager_->MarkLoading(record_.id).has_value()) {
      SetPreviewStatus(u"Loading · not saved as a tab", false);
    }
  }

  void
  PrimaryMainFrameRenderProcessGone(base::TerminationStatus status) override {
    DismissWithReason(PreviewDismissReason::kCrashed);
  }

  PreviewStatusResult Promote(PreviewPromotionTarget requested_target);

private:
  void CloseBubble() {
    // Widget::Close() is asynchronous. Detach immediately so a rapid next
    // Preview can use the browser contents anchor without two focusable
    // dialogs competing for the same anchor during the close task.
    SetAnchorView(nullptr);
    if (GetWidget()) {
      GetWidget()->Close();
    }
  }

  void Dismiss() { DismissWithReason(PreviewDismissReason::kUserDismissed); }

  void PromoteToTab() { std::ignore = Promote(PreviewPromotionTarget::kTab); }
  void PromoteToSplit() {
    std::ignore = Promote(PreviewPromotionTarget::kSplit);
  }

  void SetPromotionActionsEnabled(bool enabled) {
    if (tab_button_) {
      tab_button_->SetEnabled(enabled);
    }
    if (split_button_) {
      split_button_->SetEnabled(enabled);
    }
  }

  void SetPreviewStatus(std::u16string status, bool can_promote) {
    if (status_label_) {
      status_label_->SetText(std::move(status));
    }
    SetPromotionActionsEnabled(can_promote);
  }

  void RestoreParentFocus() {
    BrowserWindowInterface *browser = ResolveBrowser(profile_, record_.window);
    TabStripModel *model = browser ? browser->GetTabStripModel() : nullptr;
    const int parent = FindTabIndex(model, record_.parent_tab);
    if (parent != TabStripModel::kNoTab) {
      model->ActivateTabAt(parent);
      if (content::WebContents *contents = model->GetWebContentsAt(parent)) {
        contents->Focus();
      }
    }
  }

  raw_ptr<Profile> profile_;
  raw_ptr<LifecycleCoordinator> lifecycle_;
  PreviewPromotionRouteResolver route_resolver_;
  PreviewWorkspaceSwitcher workspace_switcher_;
  raw_ptr<PreviewManager> manager_;
  PreviewRecord record_;
  std::unique_ptr<content::WebContents> contents_;
  raw_ptr<SeoulPreviewWebView> web_view_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::MdTextButton> tab_button_ = nullptr;
  raw_ptr<views::MdTextButton> split_button_ = nullptr;
  ClosedCallback closed_callback_;
  bool terminal_ = false;
  base::WeakPtrFactory<SeoulPreviewBubbleView> weak_factory_{this};
};

PreviewStatusResult
SeoulPreviewBubbleView::Promote(PreviewPromotionTarget requested_target) {
  const PreviewRecord *current =
      manager_ ? manager_->Find(record_.id) : nullptr;
  if (terminal_ || !contents_ || !current ||
      current->state != PreviewState::kReady) {
    return base::unexpected(PreviewError::kInvalidState);
  }

  PreviewPromotionRoute route;
  route.target = requested_target;
  if (route_resolver_) {
    route = route_resolver_.Run(*current, requested_target);
  }
  if (!route.allowed) {
    SetPreviewStatus(
        base::UTF8ToUTF16(route.blocked_reason.empty()
                              ? "This Preview route requires approval."
                              : route.blocked_reason),
        /*can_promote=*/true);
    return base::unexpected(PreviewError::kRoutingRejected);
  }
  if (!manager_->BeginPromotion(record_.id, route.target).has_value()) {
    return base::unexpected(PreviewError::kInvalidState);
  }
  SetPreviewStatus(u"Opening…", /*can_promote=*/false);

  bool switched_workspace = false;
  bool insertion_expected = false;
  LiveTabKey promoted_tab;
  auto abort_before_transfer =
      [&](PreviewError error,
          const std::string &message) -> PreviewStatusResult {
    if (insertion_expected && promoted_tab.is_valid()) {
      lifecycle_->CancelExpectedTabInsertion(promoted_tab);
    }
    bool rolled_back = true;
    if (switched_workspace && route.source_workspace.is_valid()) {
      rolled_back =
          workspace_switcher_.Run(record_.window, route.source_workspace);
    }
    std::ignore = manager_->AbortPromotion(record_.id);
    SetPreviewStatus(
        base::UTF8ToUTF16(
            rolled_back ? message
                        : message + " The original Workspace could not be "
                                    "restored; choose it from the sidebar."),
        /*can_promote=*/true);
    return base::unexpected(error);
  };

  if (route.target_workspace.is_valid() &&
      route.target_workspace != route.source_workspace) {
    if (route.target == PreviewPromotionTarget::kSplit) {
      return abort_before_transfer(
          PreviewError::kRoutingRejected,
          "A split cannot cross Workspaces. Open this Preview as a tab.");
    }
    if (!workspace_switcher_.Run(record_.window, route.target_workspace)) {
      return abort_before_transfer(
          PreviewError::kPromotionFailed,
          "The destination Workspace is busy or unavailable. Try again.");
    }
    switched_workspace = true;
  }

  BrowserWindowInterface *browser = ResolveBrowser(profile_, record_.window);
  TabStripModel *model = browser ? browser->GetTabStripModel() : nullptr;
  int parent_index = FindTabIndex(model, record_.parent_tab);
  if (!model || parent_index == TabStripModel::kNoTab ||
      (route.target == PreviewPromotionTarget::kSplit &&
       model->GetSplitForTab(parent_index).has_value())) {
    return abort_before_transfer(
        PreviewError::kPromotionFailed,
        route.target == PreviewPromotionTarget::kSplit
            ? "The source tab is already split. The Preview is still open."
            : "The source tab is no longer available. The Preview is still "
              "open.");
  }

  CreateSessionServiceTabHelper(contents_.get());
  const SessionID session_id =
      sessions::SessionTabHelper::IdForTab(contents_.get());
  promoted_tab = LiveTabKey::FromSessionId(session_id.id());
  if (!session_id.is_valid() || !promoted_tab.is_valid() ||
      !lifecycle_->ExpectTabInsertion(record_.window, promoted_tab,
                                      TabRole::kRetained)) {
    return abort_before_transfer(
        PreviewError::kPromotionFailed,
        "Another tab change is still finishing. The Preview is still open.");
  }
  insertion_expected = true;

  content::WebContents *transferred_contents = contents_.get();
  Observe(nullptr);
  web_view_->SetWebContents(nullptr);
  contents_->SetDelegate(nullptr);
  if (route.target == PreviewPromotionTarget::kSplit) {
    model->ActivateTabAt(parent_index);
  }
  const int add_types =
      ADD_FORCE_INDEX |
      (route.target == PreviewPromotionTarget::kTab ? ADD_ACTIVE : ADD_NONE);
  const int inserted = model->InsertWebContentsAt(
      parent_index + 1, std::move(contents_), add_types);
  insertion_expected = false;

  bool native_postcondition =
      inserted >= 0 && inserted < model->count() &&
      model->GetWebContentsAt(inserted) == transferred_contents;
  if (native_postcondition && route.target == PreviewPromotionTarget::kSplit) {
    parent_index = FindTabIndex(model, record_.parent_tab);
    if (parent_index == TabStripModel::kNoTab) {
      native_postcondition = false;
    } else {
      model->ActivateTabAt(parent_index);
      const split_tabs::SplitTabId split_id =
          model->AddToNewSplit({inserted}, split_tabs::SplitTabVisualData(),
                               split_tabs::SplitTabCreatedSource::kLinkClick);
      const std::optional<split_tabs::SplitTabId> parent_split =
          model->GetSplitForTab(parent_index);
      const std::optional<split_tabs::SplitTabId> promoted_split =
          model->GetSplitForTab(inserted);
      native_postcondition =
          parent_split.has_value() && promoted_split.has_value() &&
          *parent_split == split_id && *promoted_split == split_id;
      if (native_postcondition) {
        model->ActivateTabAt(inserted);
      }
    }
  }

  const PreviewResult<PreviewRecord> committed =
      manager_->CommitPromotion(record_.id);
  terminal_ = true;
  CloseBubble();
  if (!committed.has_value() || !native_postcondition) {
    return base::unexpected(PreviewError::kPromotionFailed);
  }
  return base::ok();
}

PreviewHostService::PreviewHostService(
    Profile *profile, PreviewManager *manager, LifecycleCoordinator *lifecycle,
    PreviewPromotionRouteResolver route_resolver,
    PreviewWorkspaceSwitcher workspace_switcher)
    : profile_(profile), manager_(manager), lifecycle_(lifecycle),
      route_resolver_(std::move(route_resolver)),
      workspace_switcher_(std::move(workspace_switcher)) {}

PreviewHostService::~PreviewHostService() { Shutdown(); }

PreviewResult<PreviewId> PreviewHostService::Open(LiveWindowKey window,
                                                  LiveTabKey parent_tab,
                                                  const GURL &url) {
  if (shutting_down_) {
    return base::unexpected(PreviewError::kInvalidState);
  }
  if (!manager_ || !lifecycle_ || route_resolver_.is_null() ||
      workspace_switcher_.is_null()) {
    return base::unexpected(PreviewError::kInvalidState);
  }
  BrowserWindowInterface *browser = ResolveBrowser(profile_, window);
  Browser *concrete = browser ? browser->GetBrowserForMigrationOnly() : nullptr;
  BrowserView *browser_view =
      concrete ? BrowserView::GetBrowserViewForBrowser(concrete) : nullptr;
  if (!browser || !browser_view ||
      FindTabIndex(browser->GetTabStripModel(), parent_tab) ==
          TabStripModel::kNoTab) {
    return base::unexpected(PreviewError::kInvalidParent);
  }

  std::unique_ptr<content::WebContents> contents = content::WebContents::Create(
      content::WebContents::CreateParams(profile_));
  PreviewResult<PreviewOpenResult> opened =
      manager_->Open(window, parent_tab, url);
  if (!opened.has_value()) {
    return base::unexpected(opened.error());
  }
  if (opened->replaced.has_value()) {
    auto existing = views_.find(*opened->replaced);
    if (existing != views_.end() && existing->second) {
      SeoulPreviewBubbleView *superseded = existing->second;
      views_.erase(existing);
      superseded->CloseSuperseded();
    }
  }

  const PreviewRecord *record = manager_->Find(opened->id);
  CHECK(record);
  auto *view = new SeoulPreviewBubbleView(
      browser_view->GetContentsView(), profile_, lifecycle_, route_resolver_,
      workspace_switcher_, manager_, *record, std::move(contents),
      base::BindRepeating(&PreviewHostService::OnViewClosed,
                          weak_factory_.GetWeakPtr()));
  views_.emplace(opened->id, view);
  preview_by_window_[window] = opened->id;
  views::Widget *widget = views::BubbleDialogDelegateView::CreateBubble(view);
  if (gfx::Animation::PrefersReducedMotion()) {
    widget->SetVisibilityChangedAnimationsEnabled(false);
  }
  // A Preview opened for an explicitly bound background window must never
  // steal focus from the application or window the user is working in.
  if (browser->GetWindow() && browser->GetWindow()->IsActive()) {
    widget->Show();
  } else {
    widget->ShowInactive();
  }
  return opened->id;
}

PreviewResult<PreviewId>
PreviewHostService::OpenFromLink(content::WebContents *parent_contents,
                                 const GURL &url) {
  if (shutting_down_ || !parent_contents ||
      parent_contents->GetBrowserContext() != profile_) {
    return base::unexpected(PreviewError::kInvalidParent);
  }
  ProfileBrowserCollection *collection =
      ProfileBrowserCollection::GetForProfile(profile_);
  BrowserWindowInterface *browser =
      collection ? collection->FindBrowserWithTab(parent_contents) : nullptr;
  const SessionID tab_id =
      sessions::SessionTabHelper::IdForTab(parent_contents);
  if (!browser || browser->IsDeleteScheduled() ||
      browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
      !browser->GetSessionID().is_valid() || !tab_id.is_valid()) {
    return base::unexpected(PreviewError::kInvalidParent);
  }
  return Open(LiveWindowKey::FromSessionId(browser->GetSessionID().id()),
              LiveTabKey::FromSessionId(tab_id.id()), url);
}

PreviewStatusResult PreviewHostService::Promote(const PreviewId &id,
                                                PreviewPromotionTarget target) {
  if (shutting_down_ || !manager_ || !id.is_valid()) {
    return base::unexpected(PreviewError::kInvalidState);
  }
  const auto view = views_.find(id);
  if (view == views_.end() || !view->second || !manager_->Find(id)) {
    return base::unexpected(PreviewError::kUnknownPreview);
  }
  return view->second->Promote(target);
}

size_t PreviewHostService::DismissForParent(LiveTabKey parent_tab) {
  std::vector<PreviewId> matches;
  for (const PreviewRecord *record : manager_->List()) {
    if (record->parent_tab == parent_tab) {
      matches.push_back(record->id);
    }
  }
  for (const PreviewId &id : matches) {
    auto view = views_.find(id);
    if (view != views_.end() && view->second) {
      view->second->DismissWithReason(PreviewDismissReason::kParentTabRemoved);
    } else {
      std::ignore =
          manager_->Dismiss(id, PreviewDismissReason::kParentTabRemoved);
    }
  }
  return matches.size();
}

size_t PreviewHostService::DismissForWindow(LiveWindowKey window) {
  const PreviewRecord *record = manager_->FindForWindow(window);
  if (!record) {
    return 0;
  }
  auto view = views_.find(record->id);
  if (view != views_.end() && view->second) {
    view->second->DismissWithReason(PreviewDismissReason::kWindowClosed);
  } else {
    std::ignore =
        manager_->Dismiss(record->id, PreviewDismissReason::kWindowClosed);
  }
  return 1;
}

void PreviewHostService::Shutdown() {
  if (shutting_down_) {
    return;
  }
  shutting_down_ = true;
  weak_factory_.InvalidateWeakPtrs();
  for (auto &[id, view] : views_) {
    if (view) {
      view->CloseSuperseded();
    }
  }
  views_.clear();
  preview_by_window_.clear();
  for (const PreviewRecord *record : manager_->List()) {
    std::ignore =
        manager_->Dismiss(record->id, PreviewDismissReason::kWindowClosed);
  }
}

void PreviewHostService::OnViewClosed(PreviewId id, LiveWindowKey window) {
  views_.erase(id);
  auto it = preview_by_window_.find(window);
  if (it != preview_by_window_.end() && it->second == id) {
    preview_by_window_.erase(it);
  }
}

} // namespace seoul
