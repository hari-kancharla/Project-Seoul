// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/product/browser/seoul_boost_code_dialog.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "seoul/browser/product/browser/boost_entry_points.h"
#include "seoul/browser/product/browser/seoul_runtime_service.h"
#include "seoul/browser/product/browser/seoul_runtime_service_factory.h"
#include "seoul/browser/site_layers/site_layer_registry.h"
#include "seoul/browser/site_layers/site_layer_types.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/gfx/font_list.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/tabbed_pane/tabbed_pane.h"
#include "ui/views/controls/textarea/textarea.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/origin.h"

namespace seoul {
namespace {

content::WebContents* ResolveSource(content::WeakDocumentPtr source) {
  auto* frame = source.AsRenderFrameHostIfValid();
  auto* contents =
      frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
  return contents && contents->GetPrimaryMainFrame() == frame &&
                 contents->GetVisibility() != content::Visibility::HIDDEN &&
                 CanBoostWebContents(contents)
             ? contents
             : nullptr;
}

class BoostCodeDialog : public views::BoxLayoutView,
                        public views::TextfieldController,
                        public content::WebContentsObserver {
  METADATA_HEADER(BoostCodeDialog, views::BoxLayoutView)

 public:
  BoostCodeDialog(views::DialogDelegate* dialog,
                  content::WebContents* contents,
                  const SiteLayer& layer,
                  bool creating)
      : content::WebContentsObserver(contents),
        dialog_(dialog),
        source_(contents->GetPrimaryMainFrame()->GetWeakDocumentPtr()),
        initial_(layer),
        creating_(creating),
        initial_javascript_enabled_(
            Profile::FromBrowserContext(contents->GetBrowserContext())
                ->GetPrefs()
                ->GetBoolean(kSeoulBoostJavaScriptEnabledPref)) {
    SetOrientation(views::BoxLayout::Orientation::kVertical);
    SetInsideBorderInsets(gfx::Insets(20));
    SetBetweenChildSpacing(12);
    SetPreferredSize(gfx::Size(600, 430));
    AddLabel(base::UTF8ToUTF16(layer.origin_pattern));
    tabs_ = AddChildView(std::make_unique<views::TabbedPane>());
    SetFlexForView(tabs_, 1);
    css_ = AddEditor(u"CSS", u"Custom CSS", layer.custom_css);
    javascript_ =
        AddEditor(u"JavaScript", u"Custom JavaScript", layer.custom_javascript);
    javascript_enabled_ = AddChildView(std::make_unique<views::Checkbox>(
        u"Allow JavaScript from saved Boosts on this device",
        base::BindRepeating(&BoostCodeDialog::UpdateValidity,
                            base::Unretained(this))));
    javascript_enabled_->SetChecked(initial_javascript_enabled_);
    AddLabel(
        u"JavaScript can read and change matching pages. Disabling it "
        u"stops future runs; reload pages to clear effects. Site security "
        u"rules may prevent a script from running.");
    error_ = AddLabel(u"");
    error_->SetVisible(false);
    dialog_->SetExtraView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&BoostCodeDialog::ClearCurrentEditor,
                            base::Unretained(this)),
        u"Clear code"));
    dialog_->SetInitiallyFocusedView(css_);
    UpdateValidity();
  }

  bool Accept() {
    auto* contents = ResolveSource(source_);
    if (!contents || !ValidLengths())
      return false;
    auto* runtime = SeoulRuntimeServiceFactory::GetForProfile(
        Profile::FromBrowserContext(contents->GetBrowserContext()));
    const auto* latest = runtime && runtime->site_layers()
                             ? runtime->site_layers()->Find(initial_.id)
                             : nullptr;
    if (!runtime || (!creating_ && !latest)) {
      ShowError(u"This Boost was removed. Close the editor to continue.");
      return false;
    }
    if (latest && (latest->origin_pattern != initial_.origin_pattern ||
                   latest->custom_css != initial_.custom_css ||
                   latest->custom_javascript != initial_.custom_javascript)) {
      ShowError(
          u"This code changed in another editor. Close and reopen "
          u"to load the latest version.");
      return false;
    }
    // Preserve changes to appearance, name, pause state and Scene made while
    // this draft was open. Only this editor's two code fields are replaced.
    SiteLayer updated = latest ? *latest : initial_;
    updated.custom_css = base::UTF16ToUTF8(css_->GetText());
    updated.custom_javascript = base::UTF16ToUTF8(javascript_->GetText());
    std::optional<bool> permission;
    if (javascript_enabled_->GetChecked() != initial_javascript_enabled_)
      permission = javascript_enabled_->GetChecked();
    auto result = runtime->UpsertSiteLayer(std::move(updated), permission);
    if (!result.has_value()) {
      ShowError(u"The Boost could not be saved: " +
                base::UTF8ToUTF16(SiteLayerErrorToString(result.error())));
      return false;
    }
    return true;
  }

  void ContentsChanged(views::Textfield*, const std::u16string&) override {
    UpdateValidity();
  }
  void PrimaryPageChanged(content::Page&) override { CloseForSourceChange(); }
  void WebContentsDestroyed() override { CloseForSourceChange(); }
  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility == content::Visibility::HIDDEN)
      CloseForSourceChange();
  }

 private:
  views::Label* AddLabel(const std::u16string& text) {
    auto* label = AddChildView(std::make_unique<views::Label>(text));
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetMultiLine(true);
    return label;
  }
  views::Textarea* AddEditor(const std::u16string& tab_name,
                             const std::u16string& field_name,
                             const std::string& value) {
    auto pane = std::make_unique<views::BoxLayoutView>();
    pane->SetOrientation(views::BoxLayout::Orientation::kVertical);
    pane->SetInsideBorderInsets(gfx::Insets::TLBR(12, 0, 0, 0));
    auto* field = pane->AddChildView(std::make_unique<views::Textarea>());
    field->GetViewAccessibility().SetName(field_name);
    field->SetFontList(gfx::FontList("Menlo, Consolas, monospace, 13px"));
    field->SetText(base::UTF8ToUTF16(value));
    field->SetController(this);
    pane->SetFlexForView(field, 1);
    tabs_->AddTab(tab_name, std::move(pane));
    return field;
  }
  bool ValidLengths() const {
    return base::UTF16ToUTF8(css_->GetText()).size() <= kMaxCustomCssLength &&
           base::UTF16ToUTF8(javascript_->GetText()).size() <=
               kMaxCustomJavaScriptLength;
  }
  void UpdateValidity() {
    const bool valid = ValidLengths();
    if (valid)
      error_->SetVisible(false);
    else
      ShowError(
          u"Each code field can contain up to 64 KiB. Shorten the code "
          u"before saving.");
    dialog_->SetButtonEnabled(
        ui::mojom::DialogButton::kOk,
        valid && (!creating_ || !css_->GetText().empty() ||
                  !javascript_->GetText().empty()));
  }
  void ClearCurrentEditor() {
    auto* field =
        tabs_->GetSelectedTabIndex() == 0 ? css_.get() : javascript_.get();
    field->SelectAll(false);
    field->ExecuteCommand(views::Textfield::kDelete, 0);
    UpdateValidity();
    field->RequestFocus();
  }
  void ShowError(const std::u16string& text) {
    error_->SetText(text);
    error_->SetVisible(true);
    error_->GetViewAccessibility().AnnounceAlert(text);
  }
  void CloseForSourceChange() {
    if (GetWidget())
      GetWidget()->Close();
  }

  const raw_ptr<views::DialogDelegate> dialog_;
  const content::WeakDocumentPtr source_;
  const SiteLayer initial_;
  const bool creating_;
  const bool initial_javascript_enabled_;
  raw_ptr<views::TabbedPane> tabs_ = nullptr;
  raw_ptr<views::Textarea> css_ = nullptr;
  raw_ptr<views::Textarea> javascript_ = nullptr;
  raw_ptr<views::Checkbox> javascript_enabled_ = nullptr;
  raw_ptr<views::Label> error_ = nullptr;
};

BEGIN_METADATA(BoostCodeDialog)
END_METADATA

}  // namespace

void ShowBoostCodeDialog(content::WeakDocumentPtr source,
                         std::string layer_id) {
  auto* contents = ResolveSource(source);
  if (!contents)
    return;
  auto* runtime = SeoulRuntimeServiceFactory::GetForProfile(
      Profile::FromBrowserContext(contents->GetBrowserContext()));
  if (!runtime || !runtime->site_layers())
    return;
  const auto origin = url::Origin::Create(contents->GetLastCommittedURL());
  SiteLayer layer;
  const bool creating = layer_id.empty();
  if (creating) {
    layer.id = "boost-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
    layer.name = origin.host() + " Boost";
    layer.origin_pattern = origin.Serialize();
  } else {
    const auto* existing = runtime->site_layers()->Find(layer_id);
    if (!existing || existing->origin_pattern != origin.Serialize())
      return;
    layer = *existing;
  }
  auto dialog = std::make_unique<views::DialogDelegate>();
  dialog->SetTitle(u"Edit Boost code");
  dialog->SetModalType(ui::mojom::ModalType::kWindow);
  dialog->SetButtonLabel(ui::mojom::DialogButton::kOk, u"Save");
  dialog->SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kNone));
  auto* editor = dialog->SetContentsView(std::make_unique<BoostCodeDialog>(
      dialog.get(), contents, layer, creating));
  dialog->SetAcceptCallbackWithClose(
      base::BindRepeating(&BoostCodeDialog::Accept, base::Unretained(editor)));
  auto* widget = constrained_window::CreateBrowserModalDialogViews(
      std::move(dialog), contents->GetTopLevelNativeWindow());
  widget->Show();
}

}  // namespace seoul
