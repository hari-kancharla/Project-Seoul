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
#include "base/strings/stringprintf.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "cc/paint/paint_flags.h"
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
#include "seoul/browser/shell/views/seoul_workspace_name_dialog.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace seoul {

namespace {

// One fixed width for the whole panel. Every row then has the same measure to
// align against, which is what stops a stack of independently sized rows from
// reading as a pile of unrelated controls.
constexpr int kBubbleWidth = 288;

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

// Arc's Size control runs 90% to 150% ("change the overall size of the
// webpage from 90% to 150%"). 100% is the page as authored and clears the
// adjustment rather than writing a no-op one.
constexpr std::array<double, 7> kSizeScales = {0.9, 1.0,  1.1, 1.2,
                                               1.3, 1.4,  1.5};

// Arc's three "Advanced color controls" sliders. 1.0 is the page untouched.
struct FilterControl {
  const char* label;
  SiteAdjustmentKind kind;
};
constexpr auto kFilters = std::to_array<FilterControl>({
    {"Contrast", SiteAdjustmentKind::kContrastLevel},
    {"Brightness", SiteAdjustmentKind::kBrightnessLevel},
    {"Original Saturation", SiteAdjustmentKind::kSaturationLevel},
});
constexpr double kFilterMin = 0.5;
constexpr double kFilterMax = 1.5;
constexpr double kFilterStep = 0.1;

// Arc's Case control.
struct CaseChoice {
  const char* label;
  TextCase value;
};
constexpr auto kCases = std::to_array<CaseChoice>({
    {"Original", TextCase::kOriginal},
    {"UPPER", TextCase::kUpper},
    {"lower", TextCase::kLower},
    {"Title", TextCase::kTitle},
});

// "#rrggbb" back to an SkColor. The registry stores the canonical form the
// compiler validates, so anything else is treated as absent rather than
// guessed at.
bool ParseHexColor(const std::string& value, SkColor* out) {
  if (value.size() != 7 || value[0] != '#') {
    return false;
  }
  int r = 0, g = 0, b = 0;
  if (!base::HexStringToInt(std::string_view(value).substr(1, 2), &r) ||
      !base::HexStringToInt(std::string_view(value).substr(3, 2), &g) ||
      !base::HexStringToInt(std::string_view(value).substr(5, 2), &b)) {
    return false;
  }
  *out = SkColorSetRGB(r, g, b);
  return true;
}

// Arc's colour wheel: "drag the colored dots in different configurations to
// change the color of webpages". Two dots - page background and page text -
// on one HSV disc. Hue runs around the circle, saturation runs from the
// centre out, and the value is fixed so a single drag always lands on a
// colour that is actually usable as a page colour.
//
// Dragging reports live so the page restyles under the pointer, which is the
// whole point of the control: you are choosing against the real page, not
// against a swatch.
class BoostColorWheel final : public views::View {
  METADATA_HEADER(BoostColorWheel, views::View)

 public:
  enum class Dot { kBackground, kText };
  using ColorCallback = base::RepeatingCallback<void(Dot, SkColor)>;

  explicit BoostColorWheel(ColorCallback on_color)
      : on_color_(std::move(on_color)) {
    SetPreferredSize(gfx::Size(kWheelSize, kWheelSize));
    GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    GetViewAccessibility().SetName(u"Page colours");
  }
  BoostColorWheel(const BoostColorWheel&) = delete;
  BoostColorWheel& operator=(const BoostColorWheel&) = delete;
  ~BoostColorWheel() override = default;

  // Places a dot without notifying, so reading state back from the registry
  // cannot loop into another write.
  void SetDot(Dot dot, std::optional<SkColor> color) {
    (dot == Dot::kBackground ? background_ : text_) = color;
    SchedulePaint();
  }

 protected:
  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::Rect bounds = GetContentsBounds();
    const float radius = std::min(bounds.width(), bounds.height()) / 2.0f;
    const gfx::PointF centre(bounds.x() + radius, bounds.y() + radius);

    // The disc. Painted once per size change into a bitmap, because a
    // per-pixel HSV sweep on every paint would be visible while dragging.
    if (disc_.isNull() || disc_.width() != static_cast<int>(radius * 2)) {
      PaintDisc(static_cast<int>(radius * 2));
    }
    canvas->DrawImageInt(gfx::ImageSkia::CreateFrom1xBitmap(disc_), bounds.x(),
                         bounds.y());

    PaintDot(canvas, centre, radius, background_, u"BG");
    PaintDot(canvas, centre, radius, text_, u"A");
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    // Whichever dot is nearer to the press is the one being dragged, which is
    // how a two-handle control stays predictable.
    dragging_ = NearestDot(event.location());
    return UpdateFromPoint(event.location());
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    return UpdateFromPoint(event.location());
  }

 private:
  static constexpr int kWheelSize = 132;
  static constexpr float kDotRadius = 7.0f;
  // Fixed value keeps every reachable colour usable as a page colour rather
  // than letting a drag to the rim produce something unreadable.
  static constexpr float kValue = 0.92f;

  void PaintDisc(int size) {
    if (size <= 0) {
      return;
    }
    disc_.allocN32Pixels(size, size);
    disc_.eraseColor(SK_ColorTRANSPARENT);
    const float radius = size / 2.0f;
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        const float dx = (x + 0.5f) - radius;
        const float dy = (y + 0.5f) - radius;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance > radius) {
          continue;
        }
        const float hue =
            static_cast<float>(std::atan2(dy, dx) * 180.0 / M_PI) + 180.0f;
        const float saturation = std::min(1.0f, distance / radius);
        *disc_.getAddr32(x, y) =
            SkPreMultiplyColor(HsvToRgb(hue, saturation, kValue));
      }
    }
  }

  static SkColor HsvToRgb(float hue, float saturation, float value) {
    const float chroma = value * saturation;
    const float sector = std::fmod(hue / 60.0f, 6.0f);
    const float second = chroma * (1.0f - std::fabs(std::fmod(sector, 2.0f) - 1.0f));
    const float match = value - chroma;
    float r = 0, g = 0, b = 0;
    if (sector < 1) { r = chroma; g = second; }
    else if (sector < 2) { r = second; g = chroma; }
    else if (sector < 3) { g = chroma; b = second; }
    else if (sector < 4) { g = second; b = chroma; }
    else if (sector < 5) { r = second; b = chroma; }
    else { r = chroma; b = second; }
    return SkColorSetRGB(static_cast<uint8_t>((r + match) * 255),
                         static_cast<uint8_t>((g + match) * 255),
                         static_cast<uint8_t>((b + match) * 255));
  }

  gfx::PointF DotPosition(std::optional<SkColor> color,
                          const gfx::PointF& centre,
                          float radius) const {
    if (!color) {
      return centre;
    }
    SkScalar hsv[3];
    SkColorToHSV(*color, hsv);
    const float angle = (hsv[0] - 180.0f) * static_cast<float>(M_PI) / 180.0f;
    const float distance = std::min(1.0f, hsv[1]) * radius;
    return gfx::PointF(centre.x() + std::cos(angle) * distance,
                       centre.y() + std::sin(angle) * distance);
  }

  void PaintDot(gfx::Canvas* canvas,
                const gfx::PointF& centre,
                float radius,
                std::optional<SkColor> color,
                const std::u16string& label) {
    const gfx::PointF at = DotPosition(color, centre, radius);
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    fill.setColor(color.value_or(SK_ColorWHITE));
    canvas->DrawCircle(at, kDotRadius, fill);
    cc::PaintFlags ring;
    ring.setAntiAlias(true);
    ring.setStyle(cc::PaintFlags::kStroke_Style);
    ring.setStrokeWidth(2.0f);
    ring.setColor(SK_ColorWHITE);
    canvas->DrawCircle(at, kDotRadius, ring);
    ring.setStrokeWidth(1.0f);
    ring.setColor(SkColorSetA(SK_ColorBLACK, 0x55));
    canvas->DrawCircle(at, kDotRadius + 1.0f, ring);
  }

  Dot NearestDot(const gfx::Point& point) const {
    const gfx::Rect bounds = GetContentsBounds();
    const float radius = std::min(bounds.width(), bounds.height()) / 2.0f;
    const gfx::PointF centre(bounds.x() + radius, bounds.y() + radius);
    const gfx::PointF background = DotPosition(background_, centre, radius);
    const gfx::PointF text = DotPosition(text_, centre, radius);
    const auto squared = [&point](const gfx::PointF& at) {
      const float dx = at.x() - point.x();
      const float dy = at.y() - point.y();
      return dx * dx + dy * dy;
    };
    return squared(background) <= squared(text) ? Dot::kBackground : Dot::kText;
  }

  bool UpdateFromPoint(const gfx::Point& point) {
    const gfx::Rect bounds = GetContentsBounds();
    const float radius = std::min(bounds.width(), bounds.height()) / 2.0f;
    const float dx = point.x() - (bounds.x() + radius);
    const float dy = point.y() - (bounds.y() + radius);
    const float distance = std::sqrt(dx * dx + dy * dy);
    const float hue =
        static_cast<float>(std::atan2(dy, dx) * 180.0 / M_PI) + 180.0f;
    // Clamped rather than ignored, so a drag that leaves the disc keeps
    // tracking the rim instead of freezing.
    const float saturation = std::min(1.0f, distance / radius);
    const SkColor color = HsvToRgb(hue, saturation, kValue);
    SetDot(dragging_, color);
    on_color_.Run(dragging_, color);
    return true;
  }

  ColorCallback on_color_;
  Dot dragging_ = Dot::kBackground;
  std::optional<SkColor> background_;
  std::optional<SkColor> text_;
  SkBitmap disc_;
};

BEGIN_METADATA(BoostColorWheel)
END_METADATA

// One editing session, bound to one origin in one window. The bubble owns no
// state the backend does not: every control writes through UpsertSiteLayer and
// re-reads, so what the bubble shows is what the registry holds, and the page
// behind restyles live through the applicator's existing path.
class SeoulBoostBubble final : public views::BoxLayoutView,
                               public ui::SimpleMenuModel::Delegate {
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
    SetPreferredSize(gfx::Size(kBubbleWidth, 0));

    // Header. The panel says what it is, then which site it acts on, then
    // offers the one switch that turns all of it off. A bare hostname with an
    // unlabelled toggle beside it does not say either of the first two.
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
    // Arc titles the editor with the Boost's own name and hangs a caret menu
    // off it: "Click the current Boost name with the caret icon next to it
    // (v). Select 'Rename this Boost...' or 'Reset all Edits'."
    name_button_ = identity->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&SeoulBoostBubble::OnNameMenu,
                            base::Unretained(this)),
        u"Boost"));
    name_button_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    name_button_->SetTextColor(views::Button::STATE_NORMAL,
                               kColorOmniboxText);
    name_button_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
    name_button_->GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);
    auto* host = identity->AddChildView(std::make_unique<views::Label>(
        base::UTF8ToUTF16(origin_.host()), views::style::CONTEXT_LABEL,
        views::style::STYLE_SECONDARY));
    host->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    host->SetElideBehavior(gfx::ELIDE_HEAD);
    header->SetFlexForView(identity, 1);

    enabled_toggle_ =
        header->AddChildView(std::make_unique<views::ToggleButton>(
            base::BindRepeating(&SeoulBoostBubble::OnEnabledToggled,
                                base::Unretained(this))));
    enabled_toggle_->GetViewAccessibility().SetName(
        u"Boost this site");
    enabled_toggle_->SetTooltipText(u"Boost this site");

    AddChildView(std::make_unique<views::Separator>());

    // Arc's control #1: the colour wheel, above everything else it affects.
    color_wheel_ = AddChildView(std::make_unique<BoostColorWheel>(
        base::BindRepeating(&SeoulBoostBubble::OnWheelColor,
                            base::Unretained(this))));

    // Dark row.
    auto* dark_row = AddRow();
    auto* dark_label = dark_row->AddChildView(
        std::make_unique<views::Label>(u"Dark mode"));
    dark_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    dark_row->SetFlexForView(dark_label, 1);
    dark_toggle_ = dark_row->AddChildView(std::make_unique<views::ToggleButton>(
        base::BindRepeating(&SeoulBoostBubble::OnDarkToggled,
                            base::Unretained(this))));
    dark_toggle_->GetViewAccessibility().SetName(u"Dark mode for this site");
    dark_toggle_->SetTooltipText(u"Dark mode for this site");

    // Arc's "Advanced color controls": Contrast, Brightness and Original
    // Saturation. Three independent controls that compile into one CSS
    // `filter`, because a second filter declaration replaces the first.
    AddSectionLabel(u"Color");
    for (size_t i = 0; i < kFilters.size(); ++i) {
      auto* row = AddRow();
      auto* label = row->AddChildView(std::make_unique<views::Label>(
          base::UTF8ToUTF16(std::string(kFilters[i].label))));
      label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      row->SetFlexForView(label, 1);
      AddChip(row, u"\u2212",
              base::BindRepeating(&SeoulBoostBubble::OnFilterStep,
                                  base::Unretained(this), i, -1));
      filter_values_[i] =
          row->AddChildView(std::make_unique<views::Label>(u"100%"));
      AddChip(row, u"+",
              base::BindRepeating(&SeoulBoostBubble::OnFilterStep,
                                  base::Unretained(this), i, 1));
    }

    // Arc's "Reset to original colors".
    auto* reset_row = AddRow();
    auto* reset_spacer = reset_row->AddChildView(std::make_unique<views::View>());
    reset_row->SetFlexForView(reset_spacer, 1);
    AddChip(reset_row, u"Reset to original colors",
            base::BindRepeating(&SeoulBoostBubble::OnResetColors,
                                base::Unretained(this)));

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

    // Size row. Arc: 90% to 150%.
    auto* size_row = AddRow();
    size_row->SetBetweenChildSpacing(8);
    auto* size_label =
        size_row->AddChildView(std::make_unique<views::Label>(u"Size"));
    size_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    size_row->SetFlexForView(size_label, 1);
    smaller_ = AddChip(size_row, u"A\u2212",
                       base::BindRepeating(&SeoulBoostBubble::OnSizeStep,
                                           base::Unretained(this), -1));
    size_value_ =
        size_row->AddChildView(std::make_unique<views::Label>(u"100%"));
    larger_ = AddChip(size_row, u"A+",
                      base::BindRepeating(&SeoulBoostBubble::OnSizeStep,
                                          base::Unretained(this), 1));

    // Arc's "Case".
    AddSectionLabel(u"Case");
    auto* cases = AddChildView(std::make_unique<views::BoxLayoutView>());
    cases->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    cases->SetBetweenChildSpacing(8);
    for (size_t i = 0; i < kCases.size(); ++i) {
      case_chips_[i] = AddChip(
          cases, base::UTF8ToUTF16(std::string(kCases[i].label)),
          base::BindRepeating(&SeoulBoostBubble::OnCasePicked,
                              base::Unretained(this), i));
    }

    AddChildView(std::make_unique<views::Separator>());

    // Footer: zap and delete.
    auto* footer = AddChildView(std::make_unique<views::BoxLayoutView>());
    footer->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    footer->SetBetweenChildSpacing(8);
    undo_zap_ = AddChip(footer, u"Undo Zap",
                        base::BindRepeating(&SeoulBoostBubble::OnUndoZap,
                                            base::Unretained(this)));
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

  // A label-left, control-right row. Shared so the rows cannot drift apart.
  views::BoxLayoutView* AddRow() {
    auto* row = AddChildView(std::make_unique<views::BoxLayoutView>());
    row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    row->SetBetweenChildSpacing(12);
    row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    return row;
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

  void OnNameMenu() {
    menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    menu_model_->AddItem(kCommandRename, u"Rename this Boost\u2026");
    menu_model_->AddItem(kCommandResetAllEdits, u"Reset all Edits");
    menu_runner_ = std::make_unique<views::MenuRunner>(
        menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
    menu_runner_->RunMenuAt(GetWidget(), nullptr,
                            name_button_->GetBoundsInScreen(),
                            views::MenuAnchorPosition::kTopLeft,
                            ui::mojom::MenuSourceType::kNone);
  }

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override {
    // Both act on an existing Boost; there is nothing to rename or reset
    // before one exists.
    return FindLayer() != nullptr;
  }
  bool IsCommandIdChecked(int command_id) const override { return false; }

  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kCommandRename) {
      const SiteLayer* const layer = FindLayer();
      ShowWorkspaceNameDialog(
          GetWidget() ? GetWidget()->GetNativeWindow() : gfx::NativeWindow(),
          u"Rename this Boost",
          base::UTF8ToUTF16(layer ? layer->name : std::string()),
          base::BindOnce(&SeoulBoostBubble::OnRenamed,
                         weak_factory_.GetWeakPtr()));
      return;
    }
    if (command_id == kCommandResetAllEdits) {
      // Arc's "Reset all Edits" returns the page to how the site wrote it
      // while keeping the Boost itself, so the name and its place in the list
      // survive. Removing every adjustment through the one write path would
      // delete the layer, so the reset is written directly.
      MutateLayer(base::BindOnce([](SiteLayer& layer) {
        layer.adjustments.clear();
      }));
    }
  }

  void OnRenamed(std::string name) {
    if (name.empty()) {
      return;
    }
    MutateLayer(base::BindOnce(
        [](std::string name, SiteLayer& layer) { layer.name = std::move(name); },
        std::move(name)));
  }

  // A wheel drag writes the page background or the page text colour. Both are
  // adjustments Seoul's compiler already supported and the panel never
  // surfaced; they need an explicit selector, so the document-wide ones are
  // used rather than a bare document scope.
  void OnWheelColor(BoostColorWheel::Dot dot, SkColor color) {
    const bool background = dot == BoostColorWheel::Dot::kBackground;
    SetDocumentAdjustment(
        background ? SiteAdjustmentKind::kBackgroundColor
                   : SiteAdjustmentKind::kTextColor,
        base::BindOnce(
            [](bool background, SkColor color, SiteAdjustment& adjustment) {
              adjustment.selectors =
                  background ? std::vector<std::string>{"html", "body"}
                             : std::vector<std::string>{"html", "body",
                                                        "body *"};
              adjustment.color_value = base::StringPrintf(
                  "#%02x%02x%02x", SkColorGetR(color), SkColorGetG(color),
                  SkColorGetB(color));
            },
            background, color));
  }

  // One step of an Arc colour slider. 1.0 is the page as authored, so landing
  // back on it clears the adjustment rather than persisting a no-op.
  void OnFilterStep(size_t index, int direction) {
    const SiteAdjustment* current =
        FindAdjustment(FindLayer(), kFilters[index].kind);
    const double now = current ? current->numeric_value : 1.0;
    double next = now + direction * kFilterStep;
    next = std::clamp(next, kFilterMin, kFilterMax);
    // Fold accumulated floating-point drift back onto the step grid so the
    // readout cannot show 99% where the user expects 100%.
    next = std::round(next * 100.0) / 100.0;
    if (next == 1.0) {
      ClearAdjustment(kFilters[index].kind);
      return;
    }
    SetDocumentAdjustment(kFilters[index].kind,
                          base::BindOnce(
                              [](double value, SiteAdjustment& adjustment) {
                                adjustment.numeric_value = value;
                              },
                              next));
  }

  // Arc's "Reset to original colors": restores the colour changes only, and
  // leaves font, size, case and zaps alone.
  void OnResetColors() {
    MutateLayer(base::BindOnce([](SiteLayer& layer) {
      std::erase_if(layer.adjustments, [](const SiteAdjustment& adjustment) {
        return adjustment.kind == SiteAdjustmentKind::kContrastLevel ||
               adjustment.kind == SiteAdjustmentKind::kBrightnessLevel ||
               adjustment.kind == SiteAdjustmentKind::kSaturationLevel ||
               adjustment.kind == SiteAdjustmentKind::kAutomaticDarkMode ||
               adjustment.kind == SiteAdjustmentKind::kTintColor ||
               adjustment.kind == SiteAdjustmentKind::kBackgroundColor ||
               adjustment.kind == SiteAdjustmentKind::kTextColor;
      });
    }));
  }

  void OnCasePicked(size_t index) {
    if (kCases[index].value == TextCase::kOriginal) {
      ClearAdjustment(SiteAdjustmentKind::kTextCase);
      return;
    }
    SetDocumentAdjustment(SiteAdjustmentKind::kTextCase,
                          base::BindOnce(
                              [](TextCase value, SiteAdjustment& adjustment) {
                                adjustment.text_case = value;
                              },
                              kCases[index].value));
  }

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

  // Arc restores a zapped area from a control on the page: "clicking the
  // Slash (\\) icon near the bottom of the webpage afterward will restore any
  // zapped area". Same behaviour, reached from the editor: the most recent
  // zap is removed, and the element comes back on the next apply.
  void OnUndoZap() {
    MutateLayer(base::BindOnce([](SiteLayer& layer) {
      for (auto it = layer.adjustments.rbegin(); it != layer.adjustments.rend();
           ++it) {
        if (it->kind == SiteAdjustmentKind::kHide) {
          layer.adjustments.erase(std::next(it).base());
          return;
        }
      }
    }));
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
    if (name_button_) {
      // The panel shows the Boost's own name once it has one, so the caret
      // menu is renaming the thing the header names.
      name_button_->SetText(exists && !layer->name.empty()
                                ? base::UTF8ToUTF16(layer->name)
                                : u"Boost");
    }
    enabled_toggle_->SetIsOn(!exists || layer->enabled);
    enabled_toggle_->SetEnabled(exists);
    delete_button_->SetEnabled(exists);
    if (undo_zap_) {
      undo_zap_->SetEnabled(
          FindAdjustment(layer, SiteAdjustmentKind::kHide) != nullptr);
    }
    dark_toggle_->SetIsOn(
        FindAdjustment(layer, SiteAdjustmentKind::kAutomaticDarkMode) !=
        nullptr);
    const SiteAdjustment* size =
        FindAdjustment(layer, SiteAdjustmentKind::kFontSizeScale);
    const double scale = size ? size->numeric_value : 1.0;
    size_value_->SetText(base::UTF8ToUTF16(
        std::to_string(static_cast<int>(scale * 100 + 0.5)) + "%"));

    // Each colour slider reads back from the registry, so the number shown is
    // the number stored rather than one the panel is remembering separately.
    for (size_t i = 0; i < kFilters.size(); ++i) {
      const SiteAdjustment* level = FindAdjustment(layer, kFilters[i].kind);
      const double value = level ? level->numeric_value : 1.0;
      filter_values_[i]->SetText(base::UTF8ToUTF16(
          std::to_string(static_cast<int>(value * 100 + 0.5)) + "%"));
    }

    if (color_wheel_) {
      const auto stored = [&](SiteAdjustmentKind kind) -> std::optional<SkColor> {
        const SiteAdjustment* const found = FindAdjustment(layer, kind);
        SkColor parsed = SK_ColorTRANSPARENT;
        if (found && ParseHexColor(found->color_value, &parsed)) {
          return parsed;
        }
        return std::nullopt;
      };
      color_wheel_->SetDot(BoostColorWheel::Dot::kBackground,
                           stored(SiteAdjustmentKind::kBackgroundColor));
      color_wheel_->SetDot(BoostColorWheel::Dot::kText,
                           stored(SiteAdjustmentKind::kTextColor));
    }

    const SiteAdjustment* text_case =
        FindAdjustment(layer, SiteAdjustmentKind::kTextCase);
    const TextCase active =
        text_case ? text_case->text_case : TextCase::kOriginal;
    for (size_t i = 0; i < kCases.size(); ++i) {
      case_chips_[i]->SetEnabled(kCases[i].value != active);
    }
  }

  const raw_ptr<SeoulRuntimeService> runtime_;
  const LiveWindowKey window_;
  const url::Origin origin_;

  static constexpr int kCommandRename = 1;
  static constexpr int kCommandResetAllEdits = 2;

  raw_ptr<views::LabelButton> name_button_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  raw_ptr<views::ToggleButton> enabled_toggle_ = nullptr;
  raw_ptr<views::ToggleButton> dark_toggle_ = nullptr;
  raw_ptr<BoostColorWheel> color_wheel_ = nullptr;
  std::array<raw_ptr<views::Label>, kFilters.size()> filter_values_ = {};
  std::array<raw_ptr<views::LabelButton>, kCases.size()> case_chips_ = {};
  std::array<views::LabelButton*, kFonts.size()> font_chips_ = {};
  raw_ptr<views::LabelButton> smaller_ = nullptr;
  raw_ptr<views::LabelButton> larger_ = nullptr;
  raw_ptr<views::Label> size_value_ = nullptr;
  raw_ptr<views::LabelButton> undo_zap_ = nullptr;
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
  // Anchor to the address field, not the toolbar. In Single Toolbar the
  // toolbar *is* the vertical rail - a full-height view pinned to the window's
  // left edge - so anchoring to it puts the panel at the rail's top-left
  // corner, overlapping the sidebar and pointing at nothing. The address field
  // is the control that names the site being boosted, which is where a
  // site-scoped panel belongs and where Arc and Zen put theirs.
  views::View* anchor = browser_view->toolbar()->location_bar_view();
  if (!anchor || !anchor->GetVisible() || anchor->GetWidget() == nullptr) {
    anchor = browser_view->toolbar();
  }
  const LiveWindowKey window =
      LiveWindowKey::FromSessionId(browser->GetSessionID().id());
  SeoulBoostBubble::Show(
      anchor, runtime, window,
      url::Origin::Create(web_contents->GetLastCommittedURL()));
  return true;
}

}  // namespace seoul
