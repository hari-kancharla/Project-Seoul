// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/product/browser/handset_picker_menu.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/controls/hover_button.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "seoul/browser/handset/handset_types.h"
#include "seoul/browser/handset/viewport_math.h"
#include "seoul/browser/product/browser/handset_mode.h"
#include "seoul/browser/product/browser/seoul_handset_size_dialog.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/widget/widget.h"

namespace seoul {
namespace {

// Desktop pages open a dedicated phone tab. An existing phone tab changes in
// place. Resolve this only after the picker has released focus/capture.
content::WebContents* HandsetTarget(content::WebContents* contents) {
  if (IsHandsetModeEnabled(contents))
    return contents;
  Browser* browser = chrome::FindBrowserWithTab(contents);
  if (!browser)
    return nullptr;
  const int index = browser->tab_strip_model()->GetIndexOfWebContents(contents);
  return index == TabStripModel::kNoTab
             ? nullptr
             : chrome::DuplicateTabAt(browser, index);
}

// A deferred choice belongs to the document and visible tab where it was
// made. A weak WebContents alone survives navigation and can target a new page.
content::WeakDocumentPtr HandsetSource(content::WebContents* contents) {
  return contents && contents->GetPrimaryMainFrame()
             ? contents->GetPrimaryMainFrame()->GetWeakDocumentPtr()
             : content::WeakDocumentPtr();
}

content::WebContents* ResolveHandsetSource(content::WeakDocumentPtr source) {
  auto* frame = source.AsRenderFrameHostIfValid();
  auto* contents =
      frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
  return contents && contents->GetPrimaryMainFrame() == frame &&
                 contents->GetVisibility() != content::Visibility::HIDDEN &&
                 CanHandsetWebContents(contents)
             ? contents
             : nullptr;
}

void ApplyProfile(content::WeakDocumentPtr source, std::string id) {
  auto* contents = ResolveHandsetSource(source);
  if (!contents || !FindHandsetProfile(id))
    return;
  const auto orientation = IsHandsetModeEnabled(contents)
                               ? HandsetMetricsFor(contents, 0, 0).orientation
                               : HandsetOrientation::kPortrait;
  if (auto* target = HandsetTarget(contents))
    EnableHandsetMode(target, id, orientation, HandsetSnapMode::kSnapToProfile);
}

void ApplyCommand(content::WeakDocumentPtr source, int command) {
  auto* contents = ResolveHandsetSource(source);
  if (!contents)
    return;
  if (command == kHandsetPickerCommandTurnOff) {
    DisableHandsetMode(contents);
  } else if (command == kHandsetPickerCommandRotate) {
    RotateHandsetMode(contents);
  } else if (command == kHandsetPickerCommandCustomSize) {
    const auto* profile = HandsetProfileFor(contents);
    const std::string id = profile ? profile->id : DefaultHandsetProfile().id;
    const auto metrics = HandsetMetricsFor(contents, 0, 0);
    ShowHandsetSizeDialog(
        contents,
        profile ? metrics.view_width_dip
                : DefaultHandsetProfile().portrait_width_dip,
        profile ? metrics.view_height_dip
                : DefaultHandsetProfile().portrait_height_dip,
        base::BindOnce(
            [](content::WeakDocumentPtr source, std::string id,
               HandsetOrientation orientation, int width, int height) {
              auto* contents = ResolveHandsetSource(source);
              if (!contents)
                return;
              if (auto* target = HandsetTarget(contents))
                EnableHandsetMode(target, id, orientation,
                                  HandsetSnapMode::kFree, width, height);
            },
            source, id,
            profile ? metrics.orientation : HandsetOrientation::kPortrait));
  } else if (command >= 0 &&
             static_cast<size_t>(command) < HandsetProfiles().size()) {
    ApplyProfile(source, HandsetProfiles()[command].id);
  }
}

class HandsetDeviceButton : public HoverButton {
  METADATA_HEADER(HandsetDeviceButton, HoverButton)
 public:
  HandsetDeviceButton(PressedCallback callback,
                      const HandsetProfile& profile,
                      bool selected,
                      base::RepeatingCallback<void(int)> move)
      : HoverButton(std::move(callback),
                    MakeIcon(),
                    base::UTF8ToUTF16(profile.label),
                    base::NumberToString16(profile.portrait_width_dip) +
                        u" × " +
                        base::NumberToString16(profile.portrait_height_dip) +
                        (selected ? u" · Current device" : u"")),
        name_(base::UTF8ToUTF16(profile.label)),
        move_(std::move(move)) {
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(8, 10)));
    GetViewAccessibility().SetName(base::UTF8ToUTF16(profile.label));
    GetViewAccessibility().SetDescription(
        base::NumberToString16(profile.portrait_width_dip) + u" by " +
        base::NumberToString16(profile.portrait_height_dip) + u" pixels" +
        (selected ? u", current device" : u""));
    if (selected)
      SetBackground(views::CreateRoundedRectBackground(
          kColorToolbarBackgroundSubtleEmphasis, 8));
  }
  void OnViewBoundsChanged(views::View* observed_view) override {
    HoverButton::OnViewBoundsChanged(observed_view);
    // HoverButton recomposes its name from visible labels after layout. Keep
    // the device name stable; dimensions and selection are its description.
    GetViewAccessibility().SetName(name_);
  }
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override {
    return event.key_code() == ui::VKEY_UP ||
           event.key_code() == ui::VKEY_DOWN ||
           HoverButton::SkipDefaultKeyEventProcessing(event);
  }
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (event.key_code() == ui::VKEY_UP || event.key_code() == ui::VKEY_DOWN) {
      move_.Run(event.key_code() == ui::VKEY_DOWN ? 1 : -1);
      return true;
    }
    return HoverButton::OnKeyPressed(event);
  }
  void OnFocus() override {
    HoverButton::OnFocus();
    ScrollRectToVisible(GetLocalBounds());
  }

 private:
  static std::unique_ptr<views::View> MakeIcon() {
    auto icon = std::make_unique<views::ImageView>();
    icon->SetImage(ui::ImageModel::FromVectorIcon(kSeoulHandsetIcon,
                                                  kColorToolbarButtonIcon, 18));
    icon->SetPreferredSize(gfx::Size(18, 18));
    return icon;
  }
  const std::u16string name_;
  base::RepeatingCallback<void(int)> move_;
};
BEGIN_METADATA(HandsetDeviceButton)
END_METADATA

class HandsetPickerView : public views::BoxLayoutView,
                          public views::TextfieldController,
                          public content::WebContentsObserver {
  METADATA_HEADER(HandsetPickerView, views::BoxLayoutView)
 public:
  explicit HandsetPickerView(content::WebContents* contents)
      : content::WebContentsObserver(contents),
        contents_(contents->GetWeakPtr()) {
    SetOrientation(views::BoxLayout::Orientation::kVertical);
    SetInsideBorderInsets(gfx::Insets(12));
    SetBetweenChildSpacing(8);
    search_ = AddChildView(std::make_unique<views::Textfield>());
    search_->SetPlaceholderText(u"Search phones and tablets");
    search_->GetViewAccessibility().SetName(u"Search devices");
    search_->SetController(this);
    search_->SetPreferredSize(gfx::Size(312, 34));
    scroll_ = AddChildView(std::make_unique<views::ScrollView>());
    scroll_->ClipHeightTo(280, 280);
    scroll_->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    auto rows = std::make_unique<views::BoxLayoutView>();
    rows->SetOrientation(views::BoxLayout::Orientation::kVertical);
    rows->SetBetweenChildSpacing(2);
    rows_ = scroll_->SetContents(std::move(rows));
    empty_ =
        AddChildView(std::make_unique<views::Label>(u"No matching devices"));
    empty_->SetVisible(false);
    auto* footer = AddChildView(std::make_unique<views::BoxLayoutView>());
    footer->SetBetweenChildSpacing(6);
    footer->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&HandsetPickerView::Command, base::Unretained(this),
                            kHandsetPickerCommandCustomSize),
        u"Custom size…"));
    if (IsHandsetModeEnabled(contents)) {
      footer->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&HandsetPickerView::Command,
                              base::Unretained(this),
                              kHandsetPickerCommandRotate),
          u"Rotate"));
      footer->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&HandsetPickerView::Command,
                              base::Unretained(this),
                              kHandsetPickerCommandTurnOff),
          u"Desktop"));
    }
    RebuildRows();
  }
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& bounds) const override {
    auto size = views::BoxLayoutView::CalculatePreferredSize(
        views::SizeBounds(336, bounds.height()));
    return gfx::Size(336, size.height());
  }
  views::Textfield* search() { return search_; }
  void ContentsChanged(views::Textfield*, const std::u16string&) override {
    RebuildRows();
  }
  bool HandleKeyEvent(views::Textfield*, const ui::KeyEvent& event) override {
    if (event.type() != ui::EventType::kKeyPressed || buttons_.empty())
      return false;
    if (event.key_code() == ui::VKEY_DOWN) {
      buttons_.front()->RequestFocus();
      return true;
    }
    if (event.key_code() == ui::VKEY_RETURN && buttons_.size() == 1) {
      Select(visible_ids_.front());
      return true;
    }
    return false;
  }
  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility == content::Visibility::HIDDEN)
      Close();
  }
  void PrimaryPageChanged(content::Page&) override { Close(); }
  void WebContentsDestroyed() override { Close(); }

 private:
  void Close() {
    if (auto* widget = GetWidget())
      widget->Close();
  }
  void Select(std::string id) {
    if (committing_)
      return;
    committing_ = true;
    const auto source = HandsetSource(contents_.get());
    Close();
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&ApplyProfile, source, std::move(id)));
  }
  void Command(int id) {
    if (committing_)
      return;
    committing_ = true;
    const auto source = HandsetSource(contents_.get());
    Close();
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&ApplyCommand, source, id));
  }
  void MoveFocus(size_t from, int direction) {
    if (direction < 0 && from == 0) {
      search_->RequestFocus();
      return;
    }
    const int next = static_cast<int>(from) + direction;
    if (next >= 0 && static_cast<size_t>(next) < buttons_.size())
      buttons_[next]->RequestFocus();
  }
  void RebuildRows() {
    buttons_.clear();
    visible_ids_.clear();
    rows_->RemoveAllChildViews();
    const auto query =
        base::ToLowerASCII(base::UTF16ToUTF8(search_->GetText()));
    const auto words = base::SplitStringPiece(query, " ", base::TRIM_WHITESPACE,
                                              base::SPLIT_WANT_NONEMPTY);
    const auto* active =
        contents_ ? HandsetProfileFor(contents_.get()) : nullptr;
    for (const auto& profile : HandsetProfiles()) {
      const std::string text = base::ToLowerASCII(
          profile.label + " " +
          (profile.platform == HandsetPlatform::kIOS ? "Apple iOS"
                                                     : "Android") +
          (profile.form_factor == HandsetFormFactor::kTablet ? " tablet"
                                                             : " phone"));
      if (!std::ranges::all_of(words,
                               [&](auto word) { return text.contains(word); }))
        continue;
      const size_t index = buttons_.size();
      buttons_.push_back(
          rows_->AddChildView(std::make_unique<HandsetDeviceButton>(
              base::BindRepeating(&HandsetPickerView::Select,
                                  base::Unretained(this), profile.id),
              profile, active && active->id == profile.id,
              base::BindRepeating(&HandsetPickerView::MoveFocus,
                                  base::Unretained(this), index))));
      visible_ids_.push_back(profile.id);
    }
    empty_->SetVisible(buttons_.empty());
    rows_->InvalidateLayout();
    const int list_height = std::min(
        280, rows_->GetPreferredSize(views::SizeBounds(312, {})).height());
    scroll_->ClipHeightTo(list_height, list_height);
    InvalidateLayout();
    if (GetWidget()) {
      if (auto* bubble =
              GetWidget()->widget_delegate()->AsBubbleDialogDelegate())
        bubble->SizeToContents();
    }
  }
  base::WeakPtr<content::WebContents> contents_;
  raw_ptr<views::Textfield> search_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  raw_ptr<views::BoxLayoutView> rows_ = nullptr;
  raw_ptr<views::Label> empty_ = nullptr;
  std::vector<raw_ptr<HandsetDeviceButton>> buttons_;
  std::vector<std::string> visible_ids_;
  bool committing_ = false;
};
BEGIN_METADATA(HandsetPickerView)
END_METADATA
}  // namespace

HandsetPickerMenu::HandsetPickerMenu() = default;
HandsetPickerMenu::~HandsetPickerMenu() {
  if (widget_)
    widget_->Close();
}

void HandsetPickerMenu::Show(views::View* anchor,
                             content::WebContents* contents) {
  if (!anchor || !anchor->GetWidget() || !contents ||
      !CanHandsetWebContents(contents))
    return;
  if (widget_ && !widget_->IsClosed()) {
    widget_->Close();
    widget_.reset();
    return;
  }
  web_contents_ = contents->GetWeakPtr();
  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor, views::BubbleBorder::TOP_RIGHT);
  delegate->SetTitle(u"Phone view");
  delegate->SetShowCloseButton(true);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_close_on_deactivate(true);
  delegate->set_margins(gfx::Insets());
  auto view = std::make_unique<HandsetPickerView>(contents);
  auto* search = view->search();
  delegate->SetContentsView(std::move(view));
  delegate->SetInitiallyFocusedView(search);
  auto* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(delegate),
      views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  widget_ = widget->GetWeakPtr();
  widget->Show();
}

views::Widget* HandsetPickerMenu::GetWidgetForTesting() {
  return widget_.get();
}

void HandsetPickerMenu::ExecuteCommandForTesting(content::WebContents* contents,
                                                 int command_id) {
  web_contents_ =
      contents ? contents->GetWeakPtr() : base::WeakPtr<content::WebContents>();
  ApplyCommand(HandsetSource(contents), command_id);
}

bool HandsetPickerMenu::IsCommandIdChecked(int command_id) const {
  const auto* active =
      web_contents_ ? HandsetProfileFor(web_contents_.get()) : nullptr;
  return active && command_id >= 0 &&
         static_cast<size_t>(command_id) < HandsetProfiles().size() &&
         active->id == HandsetProfiles()[command_id].id;
}
}  // namespace seoul
