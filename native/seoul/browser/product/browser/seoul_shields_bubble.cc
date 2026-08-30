// Project Seoul Shields panel.

#include "seoul/browser/product/browser/seoul_shields_bubble.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/i18n/number_formatting.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_service_factory.h"
#include "seoul/browser/adblock/ad_block_settings.h"
#include "seoul/browser/adblock/ad_block_stats_service.h"
#include "seoul/browser/product/browser/boost_entry_points.h"
#include "seoul/browser/product/browser/seoul_chip_button.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace seoul {

namespace {

// Same measure as the Boost panel: the two site panels hang off the same
// anchor and must read as siblings, not as two different products.
constexpr int kBubbleWidth = 288;

// One editing session, bound to one site in one tab. Every control writes
// through AdBlockService and re-reads GetSiteSettings, so what the panel
// shows is what the blocker will actually do - never state the panel is
// remembering on its own.
class SeoulShieldsBubble final : public views::BoxLayoutView {
  METADATA_HEADER(SeoulShieldsBubble, views::BoxLayoutView)

 public:
  SeoulShieldsBubble(adblock::AdBlockService* service,
                     base::WeakPtr<content::WebContents> web_contents,
                     const GURL& site_url,
                     uint64_t blocked_on_page)
      : service_(service),
        web_contents_(std::move(web_contents)),
        site_url_(site_url),
        blocked_on_page_(blocked_on_page) {
    SetOrientation(views::BoxLayout::Orientation::kVertical);
    SetInsideBorderInsets(gfx::Insets::TLBR(14, 16, 12, 16));
    SetBetweenChildSpacing(10);
    BuildContents();
    RefreshFromService();
  }
  SeoulShieldsBubble(const SeoulShieldsBubble&) = delete;
  SeoulShieldsBubble& operator=(const SeoulShieldsBubble&) = delete;
  ~SeoulShieldsBubble() override = default;

  // The panel holds a fixed width and asks its layout for the height that
  // width needs - the Boost panel's own hard-won sizing rule.
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const gfx::Size laid_out = views::BoxLayoutView::CalculatePreferredSize(
        views::SizeBounds(kBubbleWidth, available_size.height()));
    return gfx::Size(kBubbleWidth, laid_out.height());
  }

  static void Show(views::View* anchor,
                   adblock::AdBlockService* service,
                   base::WeakPtr<content::WebContents> web_contents,
                   const GURL& site_url,
                   uint64_t blocked_on_page) {
    auto bubble_delegate = std::make_unique<views::BubbleDialogDelegate>(
        anchor, views::BubbleBorder::TOP_LEFT);
    bubble_delegate->SetAccessibleTitle(u"Shields for this site");
    bubble_delegate->SetShowTitle(false);
    bubble_delegate->SetShowCloseButton(false);
    bubble_delegate->SetButtons(
        static_cast<int>(ui::mojom::DialogButton::kNone));
    bubble_delegate->set_close_on_deactivate(true);
    bubble_delegate->set_margins(gfx::Insets());
    bubble_delegate->SetContentsView(std::make_unique<SeoulShieldsBubble>(
        service, std::move(web_contents), site_url, blocked_on_page));
    views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
        std::move(bubble_delegate),
        views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
    widget->Show();
  }

 private:
  void BuildContents() {
    // Header: what this panel is, which site it governs, and the one switch
    // that turns the blocker off for that site.
    auto* header = AddChildView(std::make_unique<views::BoxLayoutView>());
    header->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    header->SetBetweenChildSpacing(12);
    header->SetCrossAxisAlignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* identity = header->AddChildView(std::make_unique<views::BoxLayoutView>());
    identity->SetOrientation(views::BoxLayout::Orientation::kVertical);
    identity->SetBetweenChildSpacing(1);
    identity->SetCrossAxisAlignment(
        views::BoxLayout::CrossAxisAlignment::kStart);
    auto* title = identity->AddChildView(
        std::make_unique<views::Label>(u"Shields"));
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    auto* host = identity->AddChildView(std::make_unique<views::Label>(
        base::UTF8ToUTF16(site_url_.host()), views::style::CONTEXT_LABEL,
        views::style::STYLE_SECONDARY));
    host->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    host->SetElideBehavior(gfx::ELIDE_HEAD);
    header->SetFlexForView(identity, 1);
    enabled_toggle_ =
        header->AddChildView(std::make_unique<views::ToggleButton>(
            base::BindRepeating(&SeoulShieldsBubble::OnEnabledToggled,
                                base::Unretained(this))));
    enabled_toggle_->GetViewAccessibility().SetName(
        u"Shields for this site");
    enabled_toggle_->SetTooltipText(u"Shields for this site");

    AddChildView(std::make_unique<views::Separator>());

    // What the blocker actually did on this page - the count is the panel's
    // proof of work, the same line Brave leads its shields with.
    blocked_label_ = AddChildView(std::make_unique<views::Label>());
    blocked_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    // The one real decision: how hard to block on this site. Standard is the
    // vetted default; Aggressive also applies ordinary first-party blocks.
    auto* mode_label = AddChildView(std::make_unique<views::Label>(
        u"Blocking", views::style::CONTEXT_LABEL,
        views::style::STYLE_SECONDARY));
    mode_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    mode_label->SetFontList(mode_label->font_list().DeriveWithSizeDelta(-1));
    auto* modes = AddChildView(std::make_unique<views::BoxLayoutView>());
    modes->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    modes->SetBetweenChildSpacing(8);
    standard_chip_ = modes->AddChildView(std::make_unique<SeoulChipButton>(
        base::BindRepeating(&SeoulShieldsBubble::OnModePicked,
                            base::Unretained(this),
                            adblock::AdBlockMode::kStandard),
        u"Standard"));
    aggressive_chip_ = modes->AddChildView(std::make_unique<SeoulChipButton>(
        base::BindRepeating(&SeoulShieldsBubble::OnModePicked,
                            base::Unretained(this),
                            adblock::AdBlockMode::kAggressive),
        u"Aggressive"));

    // Fingerprinting protection, canvas v1: with the toggle on, this site's
    // canvases are tainted and pixel readbacks throw instead of yielding a
    // fingerprint. It rides the shields switch - Off means off for
    // everything, this included.
    auto* fp_label = AddChildView(std::make_unique<views::Label>(
        u"Fingerprinting", views::style::CONTEXT_LABEL,
        views::style::STYLE_SECONDARY));
    fp_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    fp_label->SetFontList(fp_label->font_list().DeriveWithSizeDelta(-1));
    auto* fp_row = AddChildView(std::make_unique<views::BoxLayoutView>());
    fp_row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    fp_row->SetBetweenChildSpacing(12);
    fp_row->SetCrossAxisAlignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* fp_text = fp_row->AddChildView(
        std::make_unique<views::Label>(u"Block canvas reads"));
    fp_text->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    fp_row->SetFlexForView(fp_text, 1);
    fingerprint_toggle_ =
        fp_row->AddChildView(std::make_unique<views::ToggleButton>(
            base::BindRepeating(&SeoulShieldsBubble::OnFingerprintToggled,
                                base::Unretained(this))));
    fingerprint_toggle_->GetViewAccessibility().SetName(
        u"Block canvas fingerprinting");
    fingerprint_toggle_->SetTooltipText(
        u"Sites cannot read canvas pixels while this is on");

    // Only shown while this site overrides the profile default, so the row
    // never suggests there is something to reset when there is not.
    reset_chip_ = AddChildView(std::make_unique<SeoulChipButton>(
        base::BindRepeating(&SeoulShieldsBubble::OnResetToDefault,
                            base::Unretained(this)),
        u"Use default for this site"));
    reset_chip_->SetProminent(true);
  }

  void OnEnabledToggled() {
    if (!service_) {
      return;
    }
    // Off is a per-site decision; back on returns the site to the profile
    // default rather than pinning a mode the person never chose.
    service_->SetSiteMode(site_url_,
                          enabled_toggle_->GetIsOn()
                              ? std::optional<adblock::AdBlockMode>()
                              : adblock::AdBlockMode::kOff);
    RecomputeWebPreferences();
    RefreshFromService();
  }

  void OnModePicked(adblock::AdBlockMode mode) {
    if (!service_) {
      return;
    }
    const adblock::AdBlockSiteSettings settings =
        service_->GetSiteSettings(site_url_);
    if (settings.effective_mode == mode) {
      return;
    }
    service_->SetSiteMode(site_url_, mode);
    RefreshFromService();
  }

  void OnFingerprintToggled() {
    if (!service_) {
      return;
    }
    service_->SetCanvasFingerprintBlocked(site_url_,
                                          fingerprint_toggle_->GetIsOn());
    RecomputeWebPreferences();
    RefreshFromService();
  }

  // The enforcement lives in WebPreferences, recomputed by the content
  // layer; a settings write alone would not touch the live page until its
  // next navigation.
  void RecomputeWebPreferences() {
    if (web_contents_) {
      web_contents_->OnWebPreferencesChanged();
    }
  }

  void OnResetToDefault() {
    if (!service_) {
      return;
    }
    service_->SetSiteMode(site_url_, std::nullopt);
    service_->ClearTemporaryDisable(site_url_);
    service_->SetCanvasFingerprintBlocked(site_url_, false);
    RecomputeWebPreferences();
    RefreshFromService();
  }

  void RefreshFromService() {
    if (!service_) {
      return;
    }
    const adblock::AdBlockSiteSettings settings =
        service_->GetSiteSettings(site_url_);
    const bool enabled =
        settings.effective_mode != adblock::AdBlockMode::kOff;
    enabled_toggle_->SetIsOn(enabled);
    blocked_label_->SetText(
        blocked_on_page_ == 1
            ? u"1 request blocked on this page"
            : base::FormatNumber(static_cast<int64_t>(blocked_on_page_)) +
                  u" requests blocked on this page");
    standard_chip_->SetSelected(
        settings.effective_mode == adblock::AdBlockMode::kStandard);
    aggressive_chip_->SetSelected(
        settings.effective_mode == adblock::AdBlockMode::kAggressive);
    standard_chip_->SetEnabled(enabled);
    aggressive_chip_->SetEnabled(enabled);
    fingerprint_toggle_->SetIsOn(settings.canvas_fingerprint_blocked);
    fingerprint_toggle_->SetEnabled(enabled);
    reset_chip_->SetVisible(settings.site_mode.has_value() ||
                            settings.temporarily_disabled ||
                            settings.canvas_fingerprint_blocked);
    InvalidateLayout();
  }

  const raw_ptr<adblock::AdBlockService> service_;
  const base::WeakPtr<content::WebContents> web_contents_;
  const GURL site_url_;
  const uint64_t blocked_on_page_;

  raw_ptr<views::ToggleButton> enabled_toggle_ = nullptr;
  raw_ptr<views::Label> blocked_label_ = nullptr;
  raw_ptr<SeoulChipButton> standard_chip_ = nullptr;
  raw_ptr<SeoulChipButton> aggressive_chip_ = nullptr;
  raw_ptr<views::ToggleButton> fingerprint_toggle_ = nullptr;
  raw_ptr<SeoulChipButton> reset_chip_ = nullptr;
};

BEGIN_METADATA(SeoulShieldsBubble)
END_METADATA

}  // namespace

bool ShowShieldsBubbleForWebContents(content::WebContents* web_contents) {
  if (!CanBoostWebContents(web_contents)) {
    return false;
  }
  BrowserWindowInterface* browser = EligibleBrowserFor(web_contents);
  adblock::AdBlockService* service =
      browser ? adblock::AdBlockServiceFactory::GetForProfile(
                    browser->GetProfile())
              : nullptr;
  if (!service) {
    return false;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(
      browser->GetBrowserForMigrationOnly());
  if (!browser_view || !browser_view->toolbar()) {
    return false;
  }
  // Anchored to the address field for the same reason the Boost panel is:
  // the field names the site the panel governs.
  views::View* anchor = browser_view->toolbar()->location_bar_view();
  if (!anchor || !anchor->GetVisible() || anchor->GetWidget() == nullptr) {
    anchor = browser_view->toolbar();
  }
  const uint64_t blocked =
      service->stats()->GetBlockedCount(
          web_contents->GetPrimaryMainFrame()->GetGlobalFrameToken());
  SeoulShieldsBubble::Show(anchor, service, web_contents->GetWeakPtr(),
                           web_contents->GetLastCommittedURL(), blocked);
  return true;
}

}  // namespace seoul
