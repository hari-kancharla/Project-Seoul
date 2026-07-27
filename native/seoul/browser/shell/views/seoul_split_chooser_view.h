// Project Seoul native browser shell: explicit split-partner chooser.

#ifndef SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SPLIT_CHOOSER_VIEW_H_
#define SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SPLIT_CHOOSER_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"

namespace ui {
class SimpleMenuModel;
}

namespace views {
class MenuRunner;
class View;
}  // namespace views

namespace seoul {

class ShellController;

class SeoulSplitChooserView {
 public:
  SeoulSplitChooserView(views::View* anchor, ShellController* controller);
  SeoulSplitChooserView(const SeoulSplitChooserView&) = delete;
  SeoulSplitChooserView& operator=(const SeoulSplitChooserView&) = delete;
  ~SeoulSplitChooserView();

  void Show();

 private:
  raw_ptr<views::View> anchor_ = nullptr;
  raw_ptr<ShellController> controller_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SPLIT_CHOOSER_VIEW_H_
