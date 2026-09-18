// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/product/browser/seoul_boost_bubble.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/uuid.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/product/browser/boost_entry_points.h"
#include "seoul/browser/product/browser/seoul_boost_code_dialog.h"
#include "seoul/browser/product/browser/seoul_chip_button.h"
#include "seoul/browser/product/browser/seoul_runtime_service.h"
#include "seoul/browser/product/browser/seoul_runtime_service_factory.h"
#include "seoul/browser/site_layers/site_layer_registry.h"
#include "seoul/browser/site_layers/site_layer_types.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/dialog_model_host.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/slider.h"
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
constexpr int kBubbleWidth = 260;

struct FontChoice {
  const char* label;
  const char* family;  // empty = site default
};
constexpr auto kFonts = std::to_array<FontChoice>({
    {"Default", ""},
    {"Sans", "Avenir Next"},
    {"Helvetica", "Helvetica Neue"},
    {"Verdana", "Verdana"},
    {"Trebuchet", "Trebuchet MS"},
    {"Gill Sans", "Gill Sans"},
    {"Optima", "Optima"},
    {"Serif", "Georgia"},
    {"Times", "Times New Roman"},
    {"Palatino", "Palatino"},
    {"Baskerville", "Baskerville"},
    {"Cochin", "Cochin"},
    {"Didot", "Didot"},
    {"Typewriter", "American Typewriter"},
    {"Mono", "Menlo"},
    {"Courier", "Courier New"},
    {"Noteworthy", "Noteworthy"},
    {"Marker", "Marker Felt"},
    {"Chalkboard", "Chalkboard"},
    {"Handwriting", "Snell Roundhand"},
});

// Arc's Size control runs 90% to 150% ("change the overall size of the
// webpage from 90% to 150%"). 100% is the page as authored and clears the
// adjustment rather than writing a no-op one.
constexpr std::array<double, 7> kSizeScales = {0.9, 1.0, 1.1, 1.2,
                                               1.3, 1.4, 1.5};

// Arc's three "Advanced color controls" sliders. 1.0 is the page untouched.
struct FilterControl {
  const char* label;
  SiteAdjustmentKind kind;
};
constexpr auto kFilters = std::to_array<FilterControl>({
    {"Contrast", SiteAdjustmentKind::kContrastLevel},
    {"Brightness", SiteAdjustmentKind::kBrightnessLevel},
    {"Saturation", SiteAdjustmentKind::kSaturationLevel},
});
constexpr double kFilterMin = 0.5;
constexpr double kFilterMax = 1.5;

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

enum class BoostGlyph { kLight, kAdjust, kOriginal, kClose, kReset };

class BoostIconButton final : public views::LabelButton {
  METADATA_HEADER(BoostIconButton, views::LabelButton)
 public:
  BoostIconButton(PressedCallback callback,
                  BoostGlyph glyph,
                  const std::u16string& name,
                  bool filled = true)
      : views::LabelButton(std::move(callback), std::u16string()),
        glyph_(glyph),
        filled_(filled) {
    SetPreferredSize(gfx::Size(32, 32));
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetFocusRingCornerRadius(9);
    GetViewAccessibility().SetName(name);
    SetTooltipText(name);
  }
  void SetSelected(bool selected) {
    selected_ = selected;
    GetViewAccessibility().SetCheckedState(
        selected ? ax::mojom::CheckedState::kTrue
                 : ax::mojom::CheckedState::kFalse);
    SchedulePaint();
  }

 protected:
  void UpdateAccessibleCheckedState() override {
    GetViewAccessibility().SetCheckedState(
        selected_ ? ax::mojom::CheckedState::kTrue
                  : ax::mojom::CheckedState::kFalse);
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const SkColor ink = GetColorProvider()->GetColor(kColorToolbarButtonIcon);
    const SkColor surface = GetColorProvider()->GetColor(kColorToolbar);
    const bool dark = color_utils::IsDark(surface);
    const SkColor soft = dark ? SkColorSetRGB(0x23, 0x36, 0x46)
                              : SkColorSetRGB(0xEE, 0xF2, 0xF5);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (filled_ || selected_ || GetState() == STATE_HOVERED ||
        GetState() == STATE_PRESSED) {
      flags.setColor(selected_ ? ink : soft);
      canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), 9, flags);
    }
    flags.setColor(GetEnabled() ? (selected_ ? surface : ink)
                                : SkColorSetA(ink, 0x60));
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.6f);
    flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
    canvas->Save();
    canvas->Translate(gfx::Vector2d((width() - 20) / 2, (height() - 20) / 2));
    const auto line = [&](float x, float y, float x2, float y2) {
      canvas->DrawLine(gfx::PointF(x, y), gfx::PointF(x2, y2), flags);
    };
    switch (glyph_) {
      case BoostGlyph::kLight:
        canvas->DrawCircle(gfx::PointF(10, 7), 4.5f, flags);
        line(7, 11, 8, 15);
        line(13, 11, 12, 15);
        line(8, 15, 12, 15);
        line(9, 18, 11, 18);
        break;
      case BoostGlyph::kAdjust:
        for (int i = 0; i < 3; ++i) {
          const float x = i == 1 ? 7 : 13;
          const float y = 4 + i * 6;
          line(2, y, x - 2, y);
          line(x + 2, y, 18, y);
          canvas->DrawCircle(gfx::PointF(x, y), 2, flags);
        }
        break;
      case BoostGlyph::kOriginal:
        canvas->DrawCircle(gfx::PointF(10, 10), 7, flags);
        line(5, 5, 15, 15);
        break;
      case BoostGlyph::kClose:
        line(6, 6, 14, 14);
        line(6, 14, 14, 6);
        break;
      case BoostGlyph::kReset: {
        SkPathBuilder path;
        path.moveTo(6, 5)
            .cubicTo(16, -1, 22, 14, 12, 17)
            .cubicTo(7, 19, 3, 14, 4, 11);
        canvas->DrawPath(path.detach(), flags);
        line(6, 5, 6, 1);
        line(6, 5, 10, 6);
        break;
      }
    }
    canvas->Restore();
  }

 private:
  const BoostGlyph glyph_;
  const bool filled_;
  bool selected_ = false;
};
BEGIN_METADATA(BoostIconButton)
END_METADATA

// Hue runs around a softly tinted square. Saturation increases with distance
// from its centre. The large handle changes the background; the small one
// changes text. Pointer and keyboard both use the same polar mapping.
class BoostColorWheel final : public views::View {
  METADATA_HEADER(BoostColorWheel, views::View)
 public:
  enum class Dot { kBackground, kText };
  using ColorCallback = base::RepeatingCallback<void(Dot, SkColor)>;
  explicit BoostColorWheel(ColorCallback callback)
      : on_color_(std::move(callback)) {
    SetPreferredSize(gfx::Size(228, 228));
    SetFocusBehavior(FocusBehavior::ALWAYS);
    GetViewAccessibility().SetRole(ax::mojom::Role::kColorWell);
    GetViewAccessibility().SetName(u"Page colors");
    GetViewAccessibility().SetDescription(
        u"Drag Background or Text. Use Space to switch handles and arrow keys "
        u"to adjust.");
    UpdateAccessibleValue();
  }
  void SetDot(Dot dot, std::optional<SkColor> color) {
    (dot == Dot::kBackground ? background_ : text_) = color;
    UpdateAccessibleValue();
    SchedulePaint();
  }

 protected:
  void OnPaint(gfx::Canvas* canvas) override {
    const auto bounds = GetContentsBounds();
    if (bounds.IsEmpty())
      return;
    const float pixel_scale = canvas->image_scale();
    const int pixel_width = std::ceil(bounds.width() * pixel_scale);
    const int pixel_height = std::ceil(bounds.height() * pixel_scale);
    if (field_.width() != pixel_width || field_.height() != pixel_height) {
      field_.allocN32Pixels(pixel_width, pixel_height);
      const float half = std::min(bounds.width(), bounds.height()) / 2.f;
      for (int y = 0; y < pixel_height; ++y) {
        for (int x = 0; x < pixel_width; ++x) {
          const float dx = (x + .5f) / pixel_scale - half;
          const float dy = (y + .5f) / pixel_scale - half;
          const float hue = std::fmod(
              std::atan2(dy, dx) * 180.f / std::numbers::pi_v<float> + 360.f,
              360.f);
          const float qx = std::abs(dx) - (half - 16);
          const float qy = std::abs(dy) - (half - 16);
          const float distance =
              std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) +
              std::min(std::max(qx, qy), 0.f) - 9.f;
          const float border =
              std::clamp(distance * pixel_scale + .5f, 0.f, 1.f);
          const float inner = .09f * std::min(1.f, std::hypot(dx, dy) / half);
          SkScalar hsv[] = {hue, inner + (.72f - inner) * border, .98f};
          *field_.getAddr32(x, y) = SkPreMultiplyColor(SkHSVToColor(hsv));
        }
      }
    }
    canvas->Save();
    canvas->ClipPath(
        SkPath::RRect(SkRect::MakeXYWH(bounds.x(), bounds.y(), bounds.width(),
                                       bounds.height()),
                      12.f, 12.f),
        true);
    canvas->DrawImageInt(gfx::ImageSkia(gfx::ImageSkiaRep(field_, pixel_scale)),
                         bounds.x(), bounds.y());
    canvas->Restore();
    cc::PaintFlags orbit;
    orbit.setAntiAlias(true);
    orbit.setStyle(cc::PaintFlags::kStroke_Style);
    orbit.setStrokeWidth(1);
    orbit.setColor(SkColorSetA(SK_ColorBLACK, 0x10));
    canvas->DrawCircle(gfx::PointF(bounds.CenterPoint()), bounds.width() * .34f,
                       orbit);
    orbit.setColor(SkColorSetA(SK_ColorBLACK, 0x50));
    orbit.setStrokeWidth(1.5f);
    canvas->DrawLine(Position(Dot::kBackground), Position(Dot::kText), orbit);
    // Draw the active handle last when the two overlap.
    const Dot other =
        active_ == Dot::kBackground ? Dot::kText : Dot::kBackground;
    for (Dot dot : {other, active_}) {
      const auto point = Position(dot);
      const auto& value = dot == Dot::kBackground ? background_ : text_;
      const SkColor fill = value.value_or(
          dot == Dot::kBackground ? SkColorSetRGB(0xD8, 0xEF, 0xFA)
                                  : SkColorSetRGB(0x17, 0x2B, 0x3A));
      const float radius = dot == Dot::kBackground ? 15.f : 9.f;
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(SkColorSetA(SK_ColorBLACK, 0x24));
      canvas->DrawCircle(gfx::PointF(point.x(), point.y() + 1), radius + 3,
                         flags);
      flags.setColor(HasFocus() && active_ == dot
                         ? SkColorSetRGB(0x03, 0x69, 0xA1)
                         : SK_ColorWHITE);
      canvas->DrawCircle(point, radius + 2, flags);
      flags.setColor(fill);
      canvas->DrawCircle(point, radius, flags);
    }
  }
  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!event.IsOnlyLeftMouseButton())
      return false;
    RequestFocus();
    const auto point = gfx::PointF(event.location());
    active_ = (point - Position(Dot::kBackground)).LengthSquared() <=
                      (point - Position(Dot::kText)).LengthSquared()
                  ? Dot::kBackground
                  : Dot::kText;
    return UpdateFromPoint(event.location());
  }
  bool OnMouseDragged(const ui::MouseEvent& event) override {
    return UpdateFromPoint(event.location());
  }
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (event.key_code() == ui::VKEY_SPACE) {
      active_ = active_ == Dot::kBackground ? Dot::kText : Dot::kBackground;
      UpdateAccessibleValue();
      SchedulePaint();
      return true;
    }
    auto point = Position(active_);
    switch (event.key_code()) {
      case ui::VKEY_LEFT:
        point.Offset(-4, 0);
        break;
      case ui::VKEY_RIGHT:
        point.Offset(4, 0);
        break;
      case ui::VKEY_UP:
        point.Offset(0, -4);
        break;
      case ui::VKEY_DOWN:
        point.Offset(0, 4);
        break;
      default:
        return false;
    }
    return UpdateFromPoint(gfx::Point(point.x(), point.y()));
  }
  void OnFocus() override {
    views::View::OnFocus();
    SchedulePaint();
  }
  void OnBlur() override {
    views::View::OnBlur();
    SchedulePaint();
  }

 private:
  gfx::PointF Position(Dot dot) const {
    const auto bounds = GetContentsBounds();
    const auto& color = dot == Dot::kBackground ? background_ : text_;
    SkScalar hsv[] = {dot == Dot::kBackground ? 195.f : 220.f, .3f, 1.f};
    if (color)
      SkColorToHSV(*color, hsv);
    const float radius = std::min(bounds.width(), bounds.height()) / 2.f - 20;
    const float angle = hsv[0] * std::numbers::pi_v<float> / 180.f;
    const float distance =
        color ? hsv[1] * radius
              : (dot == Dot::kBackground ? .48f : .28f) * radius;
    return gfx::PointF(bounds.CenterPoint().x() + std::cos(angle) * distance,
                       bounds.CenterPoint().y() + std::sin(angle) * distance);
  }
  bool UpdateFromPoint(const gfx::Point& point) {
    const auto bounds = GetContentsBounds();
    if (bounds.IsEmpty())
      return false;
    const float dx = point.x() - bounds.CenterPoint().x();
    const float dy = point.y() - bounds.CenterPoint().y();
    const float radius =
        std::max(1.f, std::min(bounds.width(), bounds.height()) / 2.f - 20);
    SkScalar hsv[] = {
        std::fmod(
            std::atan2(dy, dx) * 180.f / std::numbers::pi_v<float> + 360.f,
            360.f),
        std::clamp(std::hypot(dx, dy) / radius, 0.f, 1.f),
        active_ == Dot::kBackground ? .98f : .3f};
    const SkColor color = SkHSVToColor(hsv);
    SetDot(active_, color);
    on_color_.Run(active_, color);
    return true;
  }
  void UpdateAccessibleValue() {
    const auto& color = active_ == Dot::kBackground ? background_ : text_;
    const std::u16string name =
        active_ == Dot::kBackground ? u"Background" : u"Text";
    GetViewAccessibility().SetValue(
        name + (color ? base::ASCIIToUTF16(base::StringPrintf(
                            " #%02x%02x%02x", SkColorGetR(*color),
                            SkColorGetG(*color), SkColorGetB(*color)))
                      : u" original"));
  }
  ColorCallback on_color_;
  Dot active_ = Dot::kBackground;
  std::optional<SkColor> background_, text_;
  SkBitmap field_;
};

BEGIN_METADATA(BoostColorWheel)
END_METADATA

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kBoostNameFieldId);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kBoostNameSaveId);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kBoostNameLengthErrorId);

content::WebContents* ResolveBoostNameSource(content::WeakDocumentPtr source) {
  auto* frame = source.AsRenderFrameHostIfValid();
  auto* contents =
      frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
  return contents && contents->GetPrimaryMainFrame() == frame &&
                 contents->GetVisibility() != content::Visibility::HIDDEN &&
                 CanBoostWebContents(contents)
             ? contents
             : nullptr;
}

SeoulRuntimeService* RuntimeForBoostName(content::WebContents* contents) {
  auto* browser = contents ? EligibleBrowserFor(contents) : nullptr;
  return browser
             ? SeoulRuntimeServiceFactory::GetForProfile(browser->GetProfile())
             : nullptr;
}

void ApplyBoostName(content::WeakDocumentPtr source,
                    std::string layer_id,
                    std::string name) {
  auto* contents = ResolveBoostNameSource(source);
  auto* runtime = RuntimeForBoostName(contents);
  const auto* layer = runtime && runtime->site_layers()
                          ? runtime->site_layers()->Find(layer_id)
                          : nullptr;
  if (!layer ||
      layer->origin_pattern !=
          url::Origin::Create(contents->GetLastCommittedURL()).Serialize())
    return;
  // Read the latest layer, so renaming never overwrites another edit or
  // recreates a Boost that was removed while the dialog was open.
  SiteLayer updated = *layer;
  updated.name = std::move(name);
  std::ignore = runtime->UpsertSiteLayer(std::move(updated));
}

// Rename outlives the editor bubble. Its native parent is the browser window,
// and its authority is the original document rather than the dismissed editor.
class BoostNameDelegate : public ui::DialogModelDelegate,
                          public content::WebContentsObserver {
 public:
  BoostNameDelegate(content::WebContents* contents, std::string layer_id)
      : content::WebContentsObserver(contents),
        source_(contents->GetPrimaryMainFrame()->GetWeakDocumentPtr()),
        layer_id_(std::move(layer_id)) {}
  void ObserveName() {
    name_changed_ =
        dialog_model()
            ->GetTextfieldByUniqueId(kBoostNameFieldId)
            ->AddOnFieldChangedCallback(base::BindRepeating(
                &BoostNameDelegate::UpdateValidity, base::Unretained(this)));
    UpdateValidity();
  }
  bool Accept() {
    auto name = Name();
    if (name.empty() || name.size() > kMaxLayerNameLength ||
        !ResolveBoostNameSource(source_))
      return false;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&ApplyBoostName, source_, layer_id_, std::move(name)));
    return true;
  }
  void PrimaryPageChanged(content::Page&) override { CloseForSourceChange(); }
  void WebContentsDestroyed() override { CloseForSourceChange(); }
  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility == content::Visibility::HIDDEN)
      CloseForSourceChange();
  }

 private:
  std::string Name() {
    return base::UTF16ToUTF8(base::TrimWhitespace(
        dialog_model()->GetTextfieldByUniqueId(kBoostNameFieldId)->text(),
        base::TRIM_ALL));
  }
  void UpdateValidity() {
    const auto name = Name();
    dialog_model()->SetButtonEnabled(
        dialog_model()->GetButtonByUniqueId(kBoostNameSaveId),
        !name.empty() && name.size() <= kMaxLayerNameLength);
    dialog_model()->SetVisible(kBoostNameLengthErrorId,
                               name.size() > kMaxLayerNameLength);
  }
  void CloseForSourceChange() {
    if (dialog_model() && dialog_model()->host())
      dialog_model()->host()->Close();  // Destroys this delegate.
  }
  const content::WeakDocumentPtr source_;
  const std::string layer_id_;
  base::CallbackListSubscription name_changed_;
};

void ShowBoostNameDialog(content::WeakDocumentPtr source,
                         std::string layer_id) {
  auto* contents = ResolveBoostNameSource(source);
  auto* runtime = RuntimeForBoostName(contents);
  const auto* layer = runtime && runtime->site_layers()
                          ? runtime->site_layers()->Find(layer_id)
                          : nullptr;
  if (!layer) {
    return;
  }
  auto delegate = std::make_unique<BoostNameDelegate>(contents, layer_id);
  auto* delegate_ptr = delegate.get();
  auto model =
      ui::DialogModel::Builder(std::move(delegate))
          .SetTitle(u"Rename this Boost")
          .AddTextfield(kBoostNameFieldId, u"Boost name",
                        base::UTF8ToUTF16(layer->name))
          .AddParagraph(ui::DialogModelLabel(
                            u"That name is too long. Please shorten it."),
                        std::u16string(), kBoostNameLengthErrorId)
          .AddOkButton(
              base::BindRepeating(&BoostNameDelegate::Accept,
                                  base::Unretained(delegate_ptr)),
              ui::DialogModel::Button::Params().SetLabel(u"Save").SetId(
                  kBoostNameSaveId))
          .AddCancelButton(base::DoNothing())
          .Build();
  delegate_ptr->ObserveName();
  constrained_window::ShowBrowserModal(std::move(model),
                                       contents->GetTopLevelNativeWindow());
}

// One editing session, bound to one origin in one window. The bubble owns no
// state the backend does not: every control writes through UpsertSiteLayer and
// re-reads, so what the bubble shows is what the registry holds, and the page
// behind restyles live through the applicator's existing path.
class SeoulBoostBubble final : public views::BoxLayoutView,
                               public ui::SimpleMenuModel::Delegate,
                               public views::SliderListener,
                               public content::WebContentsObserver {
  METADATA_HEADER(SeoulBoostBubble, views::BoxLayoutView)

 public:
  SeoulBoostBubble(content::WebContents* contents,
                   SeoulRuntimeService* runtime,
                   const LiveWindowKey& window,
                   const url::Origin& origin)
      : content::WebContentsObserver(contents),
        runtime_(runtime),
        window_(window),
        origin_(origin) {
    SetOrientation(views::BoxLayout::Orientation::kVertical);
    SetInsideBorderInsets(gfx::Insets::TLBR(14, 16, 12, 16));
    SetBetweenChildSpacing(10);
    BuildContents();
    RefreshFromRegistry();
    settings_subscription_ = runtime_->AddSiteLayersChangedCallback(
        base::BindRepeating(&SeoulBoostBubble::RefreshFromRegistry,
                            weak_factory_.GetWeakPtr()));
  }
  SeoulBoostBubble(const SeoulBoostBubble&) = delete;
  SeoulBoostBubble& operator=(const SeoulBoostBubble&) = delete;
  ~SeoulBoostBubble() override = default;

  void OnThemeChanged() override {
    views::BoxLayoutView::OnThemeChanged();
    const bool dark =
        color_utils::IsDark(GetColorProvider()->GetColor(kColorToolbar));
    SetBackground(views::CreateRoundedRectBackground(
        dark ? SkColorSetRGB(0x15, 0x27, 0x37)
             : SkColorSetRGB(0xF6, 0xF8, 0xFA),
        12));
  }

  // The panel holds a fixed width - `kBubbleWidth` - and CreateBubbleDeprecated
  // sizes the widget from this view's preferred size. Setting that preferred
  // size outright would fix both dimensions, so the height would stay at
  // whatever value was on hand when it was set - before a single row had been
  // added. Constraining only the width and asking the box layout for the
  // height it actually needs at that width is what keeps the panel's real
  // height, whichever rows are showing, instead of collapsing to zero.
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const gfx::Size laid_out = views::BoxLayoutView::CalculatePreferredSize(
        views::SizeBounds(kBubbleWidth, available_size.height()));
    return gfx::Size(kBubbleWidth, laid_out.height());
  }

  static void Show(views::View* anchor,
                   content::WebContents* contents,
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
        std::make_unique<SeoulBoostBubble>(contents, runtime, window, origin));
    views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
        std::move(bubble_delegate),
        views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
    widget->Show();
  }

  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility == content::Visibility::HIDDEN)
      CloseForPageChange();
  }
  void PrimaryPageChanged(content::Page&) override { CloseForPageChange(); }
  void WebContentsDestroyed() override { CloseForPageChange(); }

 private:
  void CloseForPageChange() {
    closing_ = true;
    weak_factory_.InvalidateWeakPtrs();
    if (GetWidget())
      GetWidget()->Close();
  }
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
    if (closing_ || !web_contents() || !runtime_) {
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
    // Code is authored content too. Resetting a visual control must never
    // delete a CSS-only or JavaScript-only Boost.
    if (layer.adjustments.empty() && layer.custom_css.empty() &&
        layer.custom_javascript.empty()) {
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
    auto* header = AddRow();
    header->SetBetweenChildSpacing(2);
    auto* close = header->AddChildView(std::make_unique<BoostIconButton>(
        base::BindRepeating(
            [](SeoulBoostBubble* self) {
              if (self->GetWidget())
                self->GetWidget()->Close();
            },
            base::Unretained(this)),
        BoostGlyph::kClose, u"Close Boost editor", false));
    close->SetPreferredSize(gfx::Size(24, 28));
    name_button_ = AddChip(header, u"My Boost ▾",
                           base::BindRepeating(&SeoulBoostBubble::OnNameMenu,
                                               base::Unretained(this)));
    name_button_->SetTooltipText(base::UTF8ToUTF16(origin_.host()));
    name_button_->GetViewAccessibility().SetHasPopup(
        ax::mojom::HasPopup::kMenu);
    header->SetFlexForView(name_button_, 1);
    reset_all_ = header->AddChildView(std::make_unique<BoostIconButton>(
        base::BindRepeating(&SeoulBoostBubble::ExecuteCommand,
                            base::Unretained(this), kCommandResetAllEdits, 0),
        BoostGlyph::kReset, u"Reset all edits", false));
    reset_all_->SetPreferredSize(gfx::Size(24, 28));
    resume_all_ = AddChildView(std::make_unique<SeoulChipButton>(
        base::BindRepeating(&SeoulBoostBubble::OnResumeAll,
                            base::Unretained(this)),
        u"Enable all Boosts"));
    resume_all_->SetTooltipText(
        u"Boosts are off in Settings. Your edits are saved but not applied.");
    color_wheel_ =
        AddChildView(std::make_unique<BoostColorWheel>(base::BindRepeating(
            &SeoulBoostBubble::OnWheelColor, base::Unretained(this))));
    auto* tools = AddRow();
    tools->SetBetweenChildSpacing(8);
    dark_toggle_ = tools->AddChildView(std::make_unique<BoostIconButton>(
        base::BindRepeating(&SeoulBoostBubble::OnDarkToggled,
                            base::Unretained(this)),
        BoostGlyph::kLight, u"Dark mode for this site"));
    dark_toggle_->GetViewAccessibility().SetRole(
        ax::mojom::Role::kToggleButton);
    advanced_button_ = tools->AddChildView(std::make_unique<BoostIconButton>(
        base::BindRepeating(&SeoulBoostBubble::OnAdvanced,
                            base::Unretained(this)),
        BoostGlyph::kAdjust, u"Advanced color controls"));
    advanced_button_->GetViewAccessibility().SetIsCollapsed();
    tools->AddChildView(std::make_unique<BoostIconButton>(
        base::BindRepeating(&SeoulBoostBubble::OnResetColors,
                            base::Unretained(this)),
        BoostGlyph::kOriginal, u"Reset to original colors"));
    for (views::View* tool : tools->children())
      tools->SetFlexForView(tool, 1);
    advanced_ = AddChildView(std::make_unique<views::BoxLayoutView>());
    advanced_->SetOrientation(views::BoxLayout::Orientation::kVertical);
    advanced_->SetBetweenChildSpacing(6);
    for (size_t i = 0; i < kFilters.size(); ++i) {
      auto* row =
          advanced_->AddChildView(std::make_unique<views::BoxLayoutView>());
      row->SetBetweenChildSpacing(6);
      row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
      auto* label = row->AddChildView(
          std::make_unique<views::Label>(base::UTF8ToUTF16(kFilters[i].label)));
      label->SetPreferredSize(gfx::Size(70, 24));
      label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      filter_sliders_[i] =
          row->AddChildView(std::make_unique<views::Slider>(this));
      filter_sliders_[i]->GetViewAccessibility().SetName(
          base::UTF8ToUTF16(kFilters[i].label));
      row->SetFlexForView(filter_sliders_[i], 1);
      filter_values_[i] =
          row->AddChildView(std::make_unique<views::Label>(u"100%"));
      FixReadoutWidth(filter_values_[i]);
    }
    advanced_->SetVisible(false);
    auto* fonts = AddChildView(std::make_unique<views::BoxLayoutView>());
    fonts->SetOrientation(views::BoxLayout::Orientation::kVertical);
    fonts->SetInsideBorderInsets(gfx::Insets(4));
    fonts->SetBetweenChildSpacing(2);
    fonts->SetBackground(views::CreateRoundedRectBackground(kColorToolbar, 12));
    fonts->GetViewAccessibility().SetRole(ax::mojom::Role::kRadioGroup);
    fonts->GetViewAccessibility().SetName(u"Font");
    for (size_t row_index = 0; row_index < 4; ++row_index) {
      auto* row = fonts->AddChildView(std::make_unique<views::BoxLayoutView>());
      row->SetBetweenChildSpacing(4);
      for (size_t col = 0; col < 5; ++col) {
        const size_t i = row_index * 5 + col;
        font_chips_[i] =
            AddChip(row, u"Aa",
                    base::BindRepeating(&SeoulBoostBubble::OnFontPicked,
                                        base::Unretained(this), i));
        font_chips_[i]->SetPreviewFont(
            i == 0 ? gfx::FontList().DeriveWithSizeDelta(3)
                   : gfx::FontList(gfx::Font(kFonts[i].family, 16)));
        font_chips_[i]->SetMinSize(gfx::Size(36, 32));
        font_chips_[i]->GetViewAccessibility().SetName(
            base::UTF8ToUTF16(kFonts[i].label));
        font_chips_[i]->SetTooltipText(base::UTF8ToUTF16(
            kFonts[i].family[0] ? kFonts[i].family : "Original font"));
        font_chips_[i]->SetChoice(static_cast<int>(i) + 1,
                                  static_cast<int>(kFonts.size()));
        font_chips_[i]->SetGroup(101);
        row->SetFlexForView(font_chips_[i], 1);
      }
    }
    auto* text_tools = AddRow();
    text_tools->SetBetweenChildSpacing(8);
    size_button_ = AddChip(text_tools, u"Size",
                           base::BindRepeating(&SeoulBoostBubble::OnSizeMenu,
                                               base::Unretained(this)));
    case_button_ = AddChip(text_tools, u"Case",
                           base::BindRepeating(&SeoulBoostBubble::OnCaseMenu,
                                               base::Unretained(this)));
    for (SeoulChipButton* button : {size_button_.get(), case_button_.get()}) {
      button->SetProminent(true);
      button->SetMinSize(gfx::Size(100, 32));
      button->GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);
      text_tools->SetFlexForView(button, 1);
      button->SetTooltipText(button == size_button_ ? u"Change page size"
                                                    : u"Change text case");
    }
    zap_button_ = AddChildView(std::make_unique<SeoulChipButton>(
        base::BindRepeating(&SeoulBoostBubble::OnZap, base::Unretained(this)),
        u"Zap an element"));
    zap_button_->SetMinSize(gfx::Size(228, 34));
    zap_button_->SetProminent(true);
    auto* footer = AddRow();
    undo_zap_ = footer->AddChildView(std::make_unique<SeoulChipButton>(
        base::BindRepeating(&SeoulBoostBubble::OnUndoZap,
                            base::Unretained(this)),
        u"Undo last Zap"));
    footer->SetFlexForView(undo_zap_, 1);
    auto* code = AddChip(
        footer, u"Code…",
        base::BindRepeating(&SeoulBoostBubble::OnCode, base::Unretained(this)));
    code->GetViewAccessibility().SetName(u"Edit Boost code");
    code->SetTooltipText(u"Edit CSS and JavaScript for this site");
  }

  void OnCode() {
    if (closing_ || !web_contents())
      return;
    const auto source =
        web_contents()->GetPrimaryMainFrame()->GetWeakDocumentPtr();
    const auto* layer = FindLayer();
    const std::string layer_id = layer ? layer->id : std::string();
    CloseForPageChange();
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&ShowBoostCodeDialog, source, layer_id));
  }

  void OnSizeMenu() {
    menu_runner_.reset();
    menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    for (size_t i = 0; i < kSizeScales.size(); ++i)
      menu_model_->AddCheckItem(
          kCommandSize + i, base::UTF8ToUTF16(std::to_string(static_cast<int>(
                                                  kSizeScales[i] * 100 + .5)) +
                                              "%"));
    ShowMenu(size_button_);
  }
  void OnCaseMenu() {
    menu_runner_.reset();
    menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    for (size_t i = 0; i < kCases.size(); ++i)
      menu_model_->AddCheckItem(kCommandCase + i,
                                base::UTF8ToUTF16(kCases[i].label));
    ShowMenu(case_button_);
  }
  void ShowMenu(views::View* anchor) {
    menu_runner_ = std::make_unique<views::MenuRunner>(
        menu_model_.get(), views::MenuRunner::HAS_MNEMONICS);
    menu_runner_->RunMenuAt(GetWidget(), nullptr, anchor->GetBoundsInScreen(),
                            views::MenuAnchorPosition::kTopLeft,
                            ui::mojom::MenuSourceType::kNone);
  }

  void OnAdvanced() {
    const bool visible = !advanced_->GetVisible();
    advanced_->SetVisible(visible);
    advanced_button_->SetSelected(visible);
    if (visible)
      advanced_button_->GetViewAccessibility().SetIsExpanded();
    else
      advanced_button_->GetViewAccessibility().SetIsCollapsed();
    if (GetWidget())
      GetWidget()
          ->widget_delegate()
          ->AsBubbleDialogDelegate()
          ->SizeToContents();
  }

  void SliderValueChanged(views::Slider* sender,
                          float value,
                          float old_value,
                          views::SliderChangeReason reason) override {
    if (reason != views::SliderChangeReason::kByUser)
      return;
    for (size_t i = 0; i < filter_sliders_.size(); ++i) {
      if (filter_sliders_[i] != sender)
        continue;
      const double next =
          std::round((kFilterMin + value * (kFilterMax - kFilterMin)) * 100) /
          100;
      if (next == 1.0)
        ClearAdjustment(kFilters[i].kind);
      else
        SetDocumentAdjustment(kFilters[i].kind,
                              base::BindOnce(
                                  [](double level, SiteAdjustment& adjustment) {
                                    adjustment.numeric_value = level;
                                  },
                                  next));
      break;
    }
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
    // A step down from the rows it introduces - the muted colour alone was
    // not separating "Color" the heading from "Contrast" the control.
    label->SetFontList(label->font_list().DeriveWithSizeDelta(-1));
    // Structure, not decoration, and the same structure the Shields panel
    // uses. Styled like a heading but with no heading role, these three
    // labels read as loose strings and there is no way to move between the
    // panel's sections.
    label->GetViewAccessibility().SetRole(ax::mojom::Role::kHeading);
    label->GetViewAccessibility().SetHierarchicalLevel(3);
  }

  SeoulChipButton* AddChip(views::BoxLayoutView* row,
                           const std::u16string& text,
                           views::Button::PressedCallback callback) {
    return row->AddChildView(
        std::make_unique<SeoulChipButton>(std::move(callback), text));
  }

  // Pins a numeric readout to the width of its widest value, so stepping
  // between "90%" and "150%" cannot reflow the row and slide the chip out
  // from under the pointer mid-click.
  void FixReadoutWidth(views::Label* readout) {
    const std::u16string current(readout->GetText());
    readout->SetText(u"150%");
    readout->SetPreferredSize(readout->GetPreferredSize({}));
    readout->SetText(current);
    readout->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  }

  // --- Handlers, all through the one write path -----------------------------

  void OnEnabledToggled() {
    const SiteLayer* existing = FindLayer();
    const bool on = existing && !existing->enabled;
    MutateLayer(base::BindOnce(
        [](bool on, SiteLayer& layer) { layer.enabled = on; }, on));
  }

  void OnDarkToggled() {
    if (!FindAdjustment(FindLayer(), SiteAdjustmentKind::kAutomaticDarkMode)) {
      SetDocumentAdjustment(SiteAdjustmentKind::kAutomaticDarkMode,
                            base::BindOnce([](SiteAdjustment&) {}));
    } else {
      ClearAdjustment(SiteAdjustmentKind::kAutomaticDarkMode);
    }
  }

  void OnNameMenu() {
    menu_runner_.reset();
    menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    menu_model_->AddCheckItem(kCommandEnabled, u"Enable this Boost");
    menu_model_->AddItem(kCommandRename, u"Rename this Boost\u2026");
    menu_model_->AddItem(kCommandResetAllEdits, u"Reset all edits");
    menu_model_->AddItem(kCommandDelete, u"Remove Boost");
    ShowMenu(name_button_);
  }

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override {
    // Both act on an existing Boost; there is nothing to rename or reset
    // before one exists.
    return command_id >= kCommandSize || FindLayer() != nullptr;
  }
  bool IsCommandIdChecked(int command_id) const override {
    const SiteLayer* layer = FindLayer();
    if (command_id == kCommandEnabled)
      return layer && layer->enabled;
    if (command_id >= kCommandSize &&
        command_id < kCommandSize + static_cast<int>(kSizeScales.size())) {
      const auto* size = FindAdjustment(layer, SiteAdjustmentKind::kPageScale);
      return std::abs((size ? size->numeric_value : 1.0) -
                      kSizeScales[command_id - kCommandSize]) < .001;
    }
    if (command_id >= kCommandCase &&
        command_id < kCommandCase + static_cast<int>(kCases.size())) {
      const auto* text_case =
          FindAdjustment(layer, SiteAdjustmentKind::kTextCase);
      return (text_case ? text_case->text_case : TextCase::kOriginal) ==
             kCases[command_id - kCommandCase].value;
    }
    return false;
  }

  void ExecuteCommand(int command_id, int event_flags) override {
    // A menu command arrives while Cocoa is releasing mouse capture. Apply
    // edits after that stack unwinds, before updating the owning bubble.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&SeoulBoostBubble::ApplyMenuCommand,
                                  weak_factory_.GetWeakPtr(), command_id));
  }

  void ApplyMenuCommand(int command_id) {
    if (command_id == kCommandEnabled) {
      OnEnabledToggled();
      return;
    }
    if (command_id >= kCommandSize &&
        command_id < kCommandSize + static_cast<int>(kSizeScales.size())) {
      const double scale = kSizeScales[command_id - kCommandSize];
      if (scale == 1.0)
        ClearAdjustment(SiteAdjustmentKind::kPageScale);
      else
        SetDocumentAdjustment(SiteAdjustmentKind::kPageScale,
                              base::BindOnce(
                                  [](double value, SiteAdjustment& adjustment) {
                                    adjustment.numeric_value = value;
                                  },
                                  scale));
      return;
    }
    if (command_id >= kCommandCase &&
        command_id < kCommandCase + static_cast<int>(kCases.size())) {
      OnCasePicked(command_id - kCommandCase);
      return;
    }
    if (command_id == kCommandDelete) {
      OnDelete();
      return;
    }
    if (command_id == kCommandRename) {
      const SiteLayer* layer = FindLayer();
      if (closing_ || !web_contents() || !layer)
        return;
      auto source = web_contents()->GetPrimaryMainFrame()->GetWeakDocumentPtr();
      std::string layer_id = layer->id;
      CloseForPageChange();
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&ShowBoostNameDialog, source, std::move(layer_id)));
      return;
    }
    if (command_id == kCommandResetAllEdits) {
      MutateLayer(base::BindOnce([](SiteLayer& layer) {
        layer.adjustments.clear();
        layer.custom_css.clear();
        layer.custom_javascript.clear();
      }));
    }
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
                  background
                      ? std::vector<std::string>{"html", "body"}
                      : std::vector<std::string>{"html", "body", "body *"};
              adjustment.color_value =
                  base::StringPrintf("#%02x%02x%02x", SkColorGetR(color),
                                     SkColorGetG(color), SkColorGetB(color));
            },
            background, color));
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
    const SiteAdjustment* current =
        FindAdjustment(FindLayer(), SiteAdjustmentKind::kTextCase);
    const TextCase active = current ? current->text_case : TextCase::kOriginal;
    if (kCases[index].value == active) {
      // The highlighted chip stays clickable (it must not look disabled),
      // but re-picking it writes nothing.
      return;
    }
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
    SetDocumentAdjustment(SiteAdjustmentKind::kFontFamily,
                          base::BindOnce(
                              [](size_t index, SiteAdjustment& adjustment) {
                                adjustment.font_family = kFonts[index].family;
                              },
                              index));
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
      created.id =
          "boost-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
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

  void OnResumeAll() {
    if (closing_ || !web_contents())
      return;
    Profile::FromBrowserContext(web_contents()->GetBrowserContext())
        ->GetPrefs()->SetBoolean(kSeoulBoostsEnabledPref, true);
    RefreshFromRegistry();
  }

  void RefreshFromRegistry() {
    if (closing_ || !web_contents())
      return;
    const bool paused = !Profile::FromBrowserContext(
        web_contents()->GetBrowserContext())->GetPrefs()->GetBoolean(
            kSeoulBoostsEnabledPref);
    const bool resize = resume_all_->GetVisible() != paused;
    resume_all_->SetVisible(paused);
    const SiteLayer* layer = FindLayer();
    const bool exists = layer != nullptr;
    if (name_button_) {
      const std::u16string name =
          exists && !layer->name.empty() &&
                  layer->name != origin_.host() + " Boost"
              ? base::UTF8ToUTF16(layer->name)
              : u"My Boost";
      name_button_->SetText(name +
                            (exists && !layer->enabled ? u" · Off ▾" : u" ▾"));
      name_button_->GetViewAccessibility().SetName(name);
    }
    reset_all_->SetEnabled(exists);
    const size_t zaps =
        layer ? std::ranges::count_if(layer->adjustments,
                                      [](const SiteAdjustment& adjustment) {
                                        return adjustment.kind ==
                                               SiteAdjustmentKind::kHide;
                                      })
              : 0;
    undo_zap_->SetVisible(zaps > 0);
    zap_button_->SetText(zaps ? u"Zap an element · " +
                                    base::NumberToString16(zaps)
                              : u"Zap an element");
    dark_toggle_->SetSelected(
        FindAdjustment(layer, SiteAdjustmentKind::kAutomaticDarkMode) !=
        nullptr);
    const SiteAdjustment* size =
        FindAdjustment(layer, SiteAdjustmentKind::kPageScale);
    const double scale = size ? size->numeric_value : 1.0;
    size_button_->SetText(
        scale == 1.0
            ? u"Size"
            : base::UTF8ToUTF16(
                  std::to_string(static_cast<int>(scale * 100 + .5)) + "%"));
    size_button_->GetViewAccessibility().SetName(
        u"Page size: " +
        base::NumberToString16(static_cast<int>(scale * 100 + .5)) + u"%");

    // Each colour slider reads back from the registry, so the number shown is
    // the number stored rather than one the panel is remembering separately.
    for (size_t i = 0; i < kFilters.size(); ++i) {
      const SiteAdjustment* level = FindAdjustment(layer, kFilters[i].kind);
      const double value = level ? level->numeric_value : 1.0;
      filter_values_[i]->SetText(base::UTF8ToUTF16(
          std::to_string(static_cast<int>(value * 100 + 0.5)) + "%"));
      filter_sliders_[i]->SetValue((value - kFilterMin) /
                                   (kFilterMax - kFilterMin));
    }

    if (color_wheel_) {
      const auto stored =
          [&](SiteAdjustmentKind kind) -> std::optional<SkColor> {
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

    // The font row never showed which family was active at all - not even
    // the disabled-state signal the case row had. Empty family means the
    // default, matching the empty entry OnFontPicked treats as "no override".
    const SiteAdjustment* font_family =
        FindAdjustment(layer, SiteAdjustmentKind::kFontFamily);
    const std::string active_font =
        font_family ? font_family->font_family : std::string();
    for (size_t i = 0; i < kFonts.size(); ++i) {
      font_chips_[i]->SetSelected(active_font == kFonts[i].family);
    }

    const SiteAdjustment* text_case =
        FindAdjustment(layer, SiteAdjustmentKind::kTextCase);
    const TextCase active =
        text_case ? text_case->text_case : TextCase::kOriginal;
    case_button_->SetText(active == TextCase::kOriginal ? u"Case"
                          : active == TextCase::kUpper  ? u"UPPER"
                          : active == TextCase::kLower  ? u"lower"
                                                        : u"Title");
    case_button_->GetViewAccessibility().SetName(
        u"Text case: " + std::u16string(active == TextCase::kOriginal
                                            ? u"Original"
                                            : case_button_->GetText()));
    if (resize && GetWidget()) {
      static_cast<views::BubbleDialogDelegate*>(GetWidget()->widget_delegate())
          ->SizeToContents();
    }
  }

  const raw_ptr<SeoulRuntimeService> runtime_;
  const LiveWindowKey window_;
  const url::Origin origin_;

  static constexpr int kCommandRename = 1;
  static constexpr int kCommandResetAllEdits = 2;
  static constexpr int kCommandDelete = 3;
  static constexpr int kCommandEnabled = 4;
  static constexpr int kCommandSize = 10;
  static constexpr int kCommandCase = 20;

  raw_ptr<SeoulChipButton> name_button_ = nullptr;
  raw_ptr<SeoulChipButton> resume_all_ = nullptr;
  base::CallbackListSubscription settings_subscription_;
  raw_ptr<BoostIconButton> reset_all_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  raw_ptr<BoostIconButton> dark_toggle_ = nullptr;
  raw_ptr<BoostColorWheel> color_wheel_ = nullptr;
  std::array<raw_ptr<views::Label>, kFilters.size()> filter_values_ = {};
  std::array<raw_ptr<views::Slider>, kFilters.size()> filter_sliders_ = {};
  raw_ptr<views::BoxLayoutView> advanced_ = nullptr;
  raw_ptr<BoostIconButton> advanced_button_ = nullptr;
  std::array<raw_ptr<SeoulChipButton>, kFonts.size()> font_chips_ = {};
  raw_ptr<SeoulChipButton> size_button_ = nullptr;
  raw_ptr<SeoulChipButton> case_button_ = nullptr;
  raw_ptr<SeoulChipButton> zap_button_ = nullptr;
  raw_ptr<SeoulChipButton> undo_zap_ = nullptr;

  bool closing_ = false;
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
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(
      browser->GetBrowserForMigrationOnly());
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
      anchor, web_contents, runtime, window,
      url::Origin::Create(web_contents->GetLastCommittedURL()));
  return true;
}

}  // namespace seoul
