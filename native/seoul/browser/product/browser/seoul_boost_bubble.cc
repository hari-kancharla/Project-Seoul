// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/product/browser/seoul_boost_bubble.h"

#include <algorithm>
#include <tuple>
#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/string_number_conversions.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "content/public/browser/web_contents.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/product/browser/boost_entry_points.h"
#include "seoul/browser/product/browser/seoul_runtime_service.h"
#include "seoul/browser/product/browser/seoul_runtime_service_factory.h"
#include "seoul/browser/site_layers/site_layer_registry.h"
#include "seoul/browser/site_layers/site_layer_types.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace seoul {

namespace {

// The tint palette. Muted, warm-leaning, chosen to sit on real pages without
// shouting; strength is fixed at a gentle default so one click gives a usable
// result, the way Arc's swatches do.
struct TintChoice {
  const char* name;
  const char* color;  // #rrggbb
};
constexpr auto kTints = std::to_array<TintChoice>({
    {"Sand", "#c8a97a"},
    {"Rose", "#c98a8a"},
    {"Sage", "#8aa98a"},
    {"Sky", "#7a9ec8"},
    {"Lilac", "#a98ac9"},
    {"Slate", "#8a97a9"},
});
constexpr double kTintStrength = 0.22;

struct FontChoice {
  const char* label;
  const char* family;  // empty = site default
};
constexpr auto kFonts = std::to_array<FontChoice>({
    {"Default", ""},
    {"Serif", "Georgia"},
    {"Sans", "Avenir Next"},
    {"Mono", "Menlo"},
});

constexpr std::array<double, 4> kSizeScales = {0.85, 1.0, 1.15, 1.3};

// One editing session, bound to one origin in one window. The bubble owns no
// state the backend does not: every control writes through UpsertSiteLayer and
// re-reads, so what the bubble shows is what the registry holds, and the page
// behind restyles live through the applicator's existing path.
class SeoulBoostBubble final : public views::BoxLayoutView {
  METADATA_HEADER(SeoulBoostBubble, views::BoxLayoutView)

 public:
  SeoulBoostBubble(SeoulRuntimeService* runtime,
                   const LiveWindowKey& window,
                   const url::Origin& origin)
      : runtime_(runtime), window_(window), origin_(origin) {
    SetOrientation(views::BoxLayout::Orientation::kVertical);
    SetInsideBorderInsets(gfx::Insets::TLBR(14, 16, 12, 16));
    SetBetweenChildSpacing(10);
    BuildContents();
    RefreshFromRegistry();
  }
  SeoulBoostBubble(const SeoulBoostBubble&) = delete;
  SeoulBoostBubble& operator=(const SeoulBoostBubble&) = delete;
  ~SeoulBoostBubble() override = default;

  static void Show(views::View* anchor,
                   SeoulRuntimeService* runtime,
                   const LiveWindowKey& window,
                   const url::Origin& origin) {
    auto bubble_delegate = std::make_unique<views::BubbleDialogDelegate>(
        anchor, views::BubbleBorder::TOP_LEFT);
    bubble_delegate->SetAccessibleTitle(u"Boost this site");
    bubble_delegate->SetShowTitle(false);
    bubble_delegate->SetShowCloseButton(false);
    bubble_delegate->SetButtons(
        static_cast<int>(ui::mojom::DialogButton::kNone));
    bubble_delegate->set_close_on_deactivate(true);
    bubble_delegate->set_margins(gfx::Insets());
    bubble_delegate->SetContentsView(
        std::make_unique<SeoulBoostBubble>(runtime, window, origin));
    views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
        std::move(bubble_delegate),
        views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
    widget->Show();
  }

 private:
  // --- Registry binding -----------------------------------------------------

  const SiteLayer* FindLayer() const {
    if (!runtime_ || !runtime_->site_layers()) {
      return nullptr;
    }
    for (const SiteLayer* layer : runtime_->site_layers()->List()) {
      if (layer->origin_pattern == origin_.Serialize()) {
        return layer;
      }
    }
    return nullptr;
  }

  const SiteAdjustment* FindAdjustment(const SiteLayer* layer,
                                       SiteAdjustmentKind kind) const {
    if (!layer) {
      return nullptr;
    }
    for (const SiteAdjustment& adjustment : layer->adjustments) {
      if (adjustment.kind == kind) {
        return &adjustment;
      }
    }
    return nullptr;
  }

  // Applies `mutate` to a copy of the current layer (or a fresh one) and writes
  // it back. The single write path, so the bubble can never hold an edit the
  // registry has not accepted.
  void MutateLayer(base::OnceCallback<void(SiteLayer&)> mutate) {
    if (!runtime_) {
      return;
    }
    SiteLayer layer;
    if (const SiteLayer* existing = FindLayer()) {
      layer = *existing;
    } else {
      // Prefixed: layer ids must start with a lowercase letter, and a raw
      // uuid starts with a hex digit six times out of sixteen - a rejection
      // that would strike at random.
      layer.id = "boost-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
      layer.name = origin_.host() + " Boost";
      layer.origin_pattern = origin_.Serialize();
      layer.enabled = true;
    }
    std::move(mutate).Run(layer);
    // A boost with no adjustments left is deletion, not an empty upsert - an
    // empty layer would linger in the list as a do-nothing entry.
    if (layer.adjustments.empty()) {
      if (const SiteLayer* existing = FindLayer()) {
        std::ignore = runtime_->RemoveSiteLayer(existing->id);
      }
    } else {
      std::ignore = runtime_->UpsertSiteLayer(std::move(layer));
    }
    RefreshFromRegistry();
  }

  void SetDocumentAdjustment(SiteAdjustmentKind kind,
                             base::OnceCallback<void(SiteAdjustment&)> fill) {
    MutateLayer(base::BindOnce(
        [](SiteAdjustmentKind kind,
           base::OnceCallback<void(SiteAdjustment&)> fill, SiteLayer& layer) {
          std::erase_if(layer.adjustments,
                        [kind](const SiteAdjustment& adjustment) {
                          return adjustment.kind == kind;
                        });
          SiteAdjustment adjustment;
          adjustment.kind = kind;
          std::move(fill).Run(adjustment);
          layer.adjustments.push_back(std::move(adjustment));
        },
        kind, std::move(fill)));
  }

  void ClearAdjustment(SiteAdjustmentKind kind) {
    MutateLayer(base::BindOnce(
        [](SiteAdjustmentKind kind, SiteLayer& layer) {
          std::erase_if(layer.adjustments,
                        [kind](const SiteAdjustment& adjustment) {
                          return adjustment.kind == kind;
                        });
        },
        kind));
  }

  // --- Controls -------------------------------------------------------------

  void BuildContents() {
    // Header: the site, and the master switch.
    auto* header = AddChildView(std::make_unique<views::BoxLayoutView>());
    header->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    header->SetBetweenChildSpacing(12);
    auto* host = header->AddChildView(std::make_unique<views::Label>(
        base::UTF8ToUTF16(origin_.host()), views::style::CONTEXT_DIALOG_TITLE));
    host->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    header->SetFlexForView(host, 1);
    enabled_toggle_ =
        header->AddChildView(std::make_unique<views::ToggleButton>(
            base::BindRepeating(&SeoulBoostBubble::OnEnabledToggled,
                                base::Unretained(this))));
    enabled_toggle_->GetViewAccessibility().SetName(u"Boost enabled");

    AddChildView(std::make_unique<views::Separator>());

    // Dark row.
    auto* dark_row = AddChildView(std::make_unique<views::BoxLayoutView>());
    dark_row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    dark_row->SetBetweenChildSpacing(12);
    auto* dark_label = dark_row->AddChildView(
        std::make_unique<views::Label>(u"Dark mode for this site"));
    dark_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    dark_row->SetFlexForView(dark_label, 1);
    dark_toggle_ = dark_row->AddChildView(std::make_unique<views::ToggleButton>(
        base::BindRepeating(&SeoulBoostBubble::OnDarkToggled,
                            base::Unretained(this))));
    dark_toggle_->GetViewAccessibility().SetName(u"Dark mode for this site");

    // Tint row: None + six swatches.
    AddSectionLabel(u"Tint");
    auto* tints = AddChildView(std::make_unique<views::BoxLayoutView>());
    tints->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    tints->SetBetweenChildSpacing(8);
    tint_none_ = AddChip(tints, u"None",
                         base::BindRepeating(&SeoulBoostBubble::OnTintCleared,
                                             base::Unretained(this)));
    for (size_t i = 0; i < kTints.size(); ++i) {
      auto* swatch = tints->AddChildView(std::make_unique<views::LabelButton>(
          base::BindRepeating(&SeoulBoostBubble::OnTintPicked,
                              base::Unretained(this), i),
          std::u16string()));
      swatch->SetPreferredSize(gfx::Size(22, 22));
      swatch->SetMinSize(gfx::Size(22, 22));
      swatch->SetMaxSize(gfx::Size(22, 22));
      // #rrggbb literals from the table above.
      const std::string hex = kTints[i].color;
      int r = 0, g = 0, b = 0;
      base::HexStringToInt(std::string_view(hex).substr(1, 2), &r);
      base::HexStringToInt(std::string_view(hex).substr(3, 2), &g);
      base::HexStringToInt(std::string_view(hex).substr(5, 2), &b);
      const SkColor color = SkColorSetRGB(r, g, b);
      swatch->SetBackground(
          views::CreateRoundedRectBackground(color, /*radius=*/11));
      swatch->GetViewAccessibility().SetName(
          base::UTF8ToUTF16(std::string("Tint ") + kTints[i].name));
      tint_swatches_[i] = swatch;
    }

    // Font row.
    AddSectionLabel(u"Font");
    auto* fonts = AddChildView(std::make_unique<views::BoxLayoutView>());
    fonts->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    fonts->SetBetweenChildSpacing(8);
    for (size_t i = 0; i < kFonts.size(); ++i) {
      font_chips_[i] = AddChip(
          fonts, base::UTF8ToUTF16(std::string(kFonts[i].label)),
          base::BindRepeating(&SeoulBoostBubble::OnFontPicked,
                              base::Unretained(this), i));
    }

    // Size row.
    auto* size_row = AddChildView(std::make_unique<views::BoxLayoutView>());
    size_row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    size_row->SetBetweenChildSpacing(8);
    auto* size_label =
        size_row->AddChildView(std::make_unique<views::Label>(u"Text size"));
    size_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    size_row->SetFlexForView(size_label, 1);
    smaller_ = AddChip(size_row, u"A−",
                       base::BindRepeating(&SeoulBoostBubble::OnSizeStep,
                                           base::Unretained(this), -1));
    size_value_ =
        size_row->AddChildView(std::make_unique<views::Label>(u"100%"));
    larger_ = AddChip(size_row, u"A+",
                      base::BindRepeating(&SeoulBoostBubble::OnSizeStep,
                                          base::Unretained(this), 1));

    AddChildView(std::make_unique<views::Separator>());

    // Footer: zap and delete.
    auto* footer = AddChildView(std::make_unique<views::BoxLayoutView>());
    footer->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    footer->SetBetweenChildSpacing(8);
    auto* zap = AddChip(footer, u"Zap element…",
                        base::BindRepeating(&SeoulBoostBubble::OnZap,
                                            base::Unretained(this)));
    footer->SetFlexForView(zap, 0);
    auto* spacer = footer->AddChildView(std::make_unique<views::View>());
    footer->SetFlexForView(spacer, 1);
    delete_button_ =
        AddChip(footer, u"Remove Boost",
                base::BindRepeating(&SeoulBoostBubble::OnDelete,
                                    base::Unretained(this)));
  }

  void AddSectionLabel(const std::u16string& text) {
    auto* label = AddChildView(std::make_unique<views::Label>(
        text, views::style::CONTEXT_LABEL, views::style::STYLE_SECONDARY));
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }

  views::LabelButton* AddChip(views::BoxLayoutView* row,
                              const std::u16string& text,
                              views::Button::PressedCallback callback) {
    auto* chip = row->AddChildView(
        std::make_unique<views::LabelButton>(std::move(callback), text));
    chip->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 10)));
    return chip;
  }

  // --- Handlers, all through the one write path -----------------------------

  void OnEnabledToggled() {
    const bool on = enabled_toggle_->GetIsOn();
    MutateLayer(base::BindOnce(
        [](bool on, SiteLayer& layer) { layer.enabled = on; }, on));
  }

  void OnDarkToggled() {
    if (dark_toggle_->GetIsOn()) {
      SetDocumentAdjustment(SiteAdjustmentKind::kAutomaticDarkMode,
                            base::BindOnce([](SiteAdjustment&) {}));
    } else {
      ClearAdjustment(SiteAdjustmentKind::kAutomaticDarkMode);
    }
  }

  void OnTintPicked(size_t index) {
    SetDocumentAdjustment(
        SiteAdjustmentKind::kTintColor,
        base::BindOnce(
            [](size_t index, SiteAdjustment& adjustment) {
              adjustment.color_value = kTints[index].color;
              adjustment.numeric_value = kTintStrength;
            },
            index));
  }

  void OnTintCleared() { ClearAdjustment(SiteAdjustmentKind::kTintColor); }

  void OnFontPicked(size_t index) {
    if (kFonts[index].family[0] == '\0') {
      ClearAdjustment(SiteAdjustmentKind::kFontFamily);
      return;
    }
    SetDocumentAdjustment(
        SiteAdjustmentKind::kFontFamily,
        base::BindOnce(
            [](size_t index, SiteAdjustment& adjustment) {
              adjustment.font_family = kFonts[index].family;
            },
            index));
  }

  void OnSizeStep(int direction) {
    const SiteAdjustment* current =
        FindAdjustment(FindLayer(), SiteAdjustmentKind::kFontSizeScale);
    const double now = current ? current->numeric_value : 1.0;
    auto it = std::ranges::lower_bound(kSizeScales, now - 0.001);
    size_t index = static_cast<size_t>(it - kSizeScales.begin());
    index = std::clamp<size_t>(
        static_cast<size_t>(static_cast<int>(index) + direction), 0,
        kSizeScales.size() - 1);
    if (kSizeScales[index] == 1.0) {
      ClearAdjustment(SiteAdjustmentKind::kFontSizeScale);
      return;
    }
    SetDocumentAdjustment(
        SiteAdjustmentKind::kFontSizeScale,
        base::BindOnce(
            [](double scale, SiteAdjustment& adjustment) {
              adjustment.numeric_value = scale;
            },
            kSizeScales[index]));
  }

  void OnZap() {
    if (!runtime_) {
      return;
    }
    // Zap attaches its hide rule to a layer, so a site without one gets an
    // empty layer first. remove_layer_on_cancel exists for exactly this case:
    // cancelling a zap that was the site's first adjustment removes the layer
    // again, leaving no do-nothing Boost behind.
    const SiteLayer* layer = FindLayer();
    const bool fresh = layer == nullptr;
    std::string layer_id;
    if (fresh) {
      SiteLayer created;
      created.id = "boost-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
      created.name = origin_.host() + " Boost";
      created.origin_pattern = origin_.Serialize();
      layer_id = created.id;
      std::ignore = runtime_->UpsertSiteLayer(std::move(created));
    } else {
      layer_id = layer->id;
    }
    runtime_->BeginSiteLayerZap(layer_id, window_,
                                /*remove_layer_on_cancel=*/fresh,
                                base::DoNothing());
    // The bubble closes so the page underneath is fully visible for picking.
    if (GetWidget()) {
      GetWidget()->Close();
    }
  }

  void OnDelete() {
    if (const SiteLayer* layer = FindLayer()) {
      std::ignore = runtime_->RemoveSiteLayer(layer->id);
    }
    if (GetWidget()) {
      GetWidget()->Close();
    }
  }

  // --- Read-back ------------------------------------------------------------

  void RefreshFromRegistry() {
    const SiteLayer* layer = FindLayer();
    const bool exists = layer != nullptr;
    enabled_toggle_->SetIsOn(!exists || layer->enabled);
    enabled_toggle_->SetEnabled(exists);
    delete_button_->SetEnabled(exists);
    dark_toggle_->SetIsOn(
        FindAdjustment(layer, SiteAdjustmentKind::kAutomaticDarkMode) !=
        nullptr);
    const SiteAdjustment* size =
        FindAdjustment(layer, SiteAdjustmentKind::kFontSizeScale);
    const double scale = size ? size->numeric_value : 1.0;
    size_value_->SetText(base::UTF8ToUTF16(
        std::to_string(static_cast<int>(scale * 100 + 0.5)) + "%"));
  }

  const raw_ptr<SeoulRuntimeService> runtime_;
  const LiveWindowKey window_;
  const url::Origin origin_;

  raw_ptr<views::ToggleButton> enabled_toggle_ = nullptr;
  raw_ptr<views::ToggleButton> dark_toggle_ = nullptr;
  raw_ptr<views::LabelButton> tint_none_ = nullptr;
  std::array<views::LabelButton*, kTints.size()> tint_swatches_ = {};
  std::array<views::LabelButton*, kFonts.size()> font_chips_ = {};
  raw_ptr<views::LabelButton> smaller_ = nullptr;
  raw_ptr<views::LabelButton> larger_ = nullptr;
  raw_ptr<views::Label> size_value_ = nullptr;
  raw_ptr<views::LabelButton> delete_button_ = nullptr;

  base::WeakPtrFactory<SeoulBoostBubble> weak_factory_{this};
};

BEGIN_METADATA(SeoulBoostBubble)
END_METADATA

}  // namespace

bool ShowBoostBubbleForWebContents(content::WebContents* web_contents) {
  if (!CanBoostWebContents(web_contents)) {
    return false;
  }
  BrowserWindowInterface* browser = EligibleBrowserFor(web_contents);
  SeoulRuntimeService* runtime =
      browser ? SeoulRuntimeServiceFactory::GetForProfile(browser->GetProfile())
              : nullptr;
  if (!runtime) {
    return false;
  }
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser->GetBrowserForMigrationOnly());
  if (!browser_view || !browser_view->toolbar()) {
    return false;
  }
  const LiveWindowKey window =
      LiveWindowKey::FromSessionId(browser->GetSessionID().id());
  SeoulBoostBubble::Show(
      browser_view->toolbar(), runtime, window,
      url::Origin::Create(web_contents->GetLastCommittedURL()));
  return true;
}

}  // namespace seoul
