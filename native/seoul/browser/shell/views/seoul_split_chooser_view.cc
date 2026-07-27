// Project Seoul native browser shell: explicit split-partner chooser.

#include "seoul/browser/shell/views/seoul_split_chooser_view.h"

#include <tuple>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "seoul/browser/shell/shell_controller.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"

namespace seoul {
namespace {

constexpr int kSplitCandidateBase = 1;

std::u16string CandidateLabel(const ShellSplitCandidate& candidate) {
  if (!candidate.title.empty() && !candidate.origin.empty()) {
    return base::UTF8ToUTF16(candidate.title + " — " + candidate.origin);
  }
  if (!candidate.title.empty()) {
    return base::UTF8ToUTF16(candidate.title);
  }
  if (!candidate.origin.empty()) {
    return base::UTF8ToUTF16(candidate.origin);
  }
  return u"Untitled tab";
}

class SplitChooserMenuModel final : public ui::SimpleMenuModel,
                                    public ui::SimpleMenuModel::Delegate {
 public:
  SplitChooserMenuModel(ShellController* controller,
                        std::vector<ShellSplitCandidate> candidates)
      : ui::SimpleMenuModel(this),
        controller_(controller),
        candidates_(std::move(candidates)) {
    for (size_t index = 0; index < candidates_.size(); ++index) {
      AddItem(kSplitCandidateBase + static_cast<int>(index),
              CandidateLabel(candidates_[index]));
    }
  }

  void ExecuteCommand(int command_id, int event_flags) override {
    (void)event_flags;
    const int index = command_id - kSplitCandidateBase;
    if (!controller_ || index < 0 ||
        static_cast<size_t>(index) >= candidates_.size()) {
      return;
    }
    std::ignore = controller_->CreateSplitWithPartner(candidates_[index].tab);
  }

 private:
  raw_ptr<ShellController> controller_;
  std::vector<ShellSplitCandidate> candidates_;
};

}  // namespace

SeoulSplitChooserView::SeoulSplitChooserView(views::View* anchor,
                                             ShellController* controller)
    : anchor_(anchor), controller_(controller) {}

SeoulSplitChooserView::~SeoulSplitChooserView() = default;

void SeoulSplitChooserView::Show() {
  if (!anchor_ || !controller_ || !anchor_->GetWidget()) {
    return;
  }
  std::vector<ShellSplitCandidate> candidates = controller_->SplitCandidates();
  if (candidates.empty()) {
    return;
  }

  // MenuRunner can outlive RunMenuAt(), so destroy any previous runner before
  // replacing the model it references and keep both objects owned by this
  // shell-scoped controller.
  menu_runner_.reset();
  model_ = std::make_unique<SplitChooserMenuModel>(controller_,
                                                   std::move(candidates));
  menu_runner_ = std::make_unique<views::MenuRunner>(
      model_.get(), views::MenuRunner::HAS_MNEMONICS);
  menu_runner_->RunMenuAt(anchor_->GetWidget(), nullptr,
                          anchor_->GetAnchorBoundsInScreen(),
                          views::MenuAnchorPosition::kTopLeft,
                          ui::mojom::MenuSourceType::kKeyboard);
}

}  // namespace seoul
