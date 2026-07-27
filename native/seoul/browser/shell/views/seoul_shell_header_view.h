// Project Seoul native browser shell V0.

#ifndef SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_HEADER_VIEW_H_
#define SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_HEADER_VIEW_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "components/favicon_base/favicon_callback.h"
#include "seoul/browser/shell/shell_observer.h"
#include "seoul/browser/shell/shell_types.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/models/image_model.h"
#include "ui/views/view.h"
#include "url/gurl.h"

class BrowserWindowInterface;
class Profile;

namespace ui {
class SimpleMenuModel;
}

namespace views {
class BoxLayout;
class Label;
class LabelButton;
class MenuRunner;
} // namespace views

namespace seoul {

class SeoulSplitChooserView;
class ShellController;

class SeoulShellHeaderView : public views::View, public ShellObserver {
  METADATA_HEADER(SeoulShellHeaderView, views::View)

public:
  using CachedFaviconLookupForTesting = base::RepeatingCallback<void(
      const GURL &, favicon_base::FaviconImageCallback)>;

  SeoulShellHeaderView(ShellController *controller,
                       BrowserWindowInterface *browser_window,
                       Profile *profile);
  SeoulShellHeaderView(const SeoulShellHeaderView &) = delete;
  SeoulShellHeaderView &operator=(const SeoulShellHeaderView &) = delete;
  ~SeoulShellHeaderView() override;

  void BindController(ShellController *controller);
  void BindBrowserContext(BrowserWindowInterface *browser_window,
                          Profile *profile);
  // Changes only the way the shell is presented. The durable expanded/
  // collapsed mode remains owned by ShellController; compact-mode hover uses
  // this to reveal the same full hierarchy without mutating product state.
  void SetPresentationCollapsed(bool collapsed);
  void OnShellSnapshotChanged(const ShellChange &change,
                              const ShellSnapshot &snapshot) override;
  bool AcceleratorPressed(const ui::Accelerator &accelerator) override;

  void SetCachedFaviconLookupForTesting(CachedFaviconLookupForTesting lookup);
  ui::ImageModel EssentialIconForTesting(const EssentialId &id) const;

private:
  enum class EssentialIconSource {
    kFallback,
    kCachePending,
    kCacheMiss,
    kCached,
    kLive,
  };

  struct EssentialIconBinding {
    EssentialIconBinding();
    EssentialIconBinding(const EssentialIconBinding &);
    EssentialIconBinding(EssentialIconBinding &&);
    EssentialIconBinding &operator=(const EssentialIconBinding &);
    EssentialIconBinding &operator=(EssentialIconBinding &&);
    ~EssentialIconBinding();

    raw_ptr<views::LabelButton> button = nullptr;
    EssentialId id;
    GURL root_url;
    LiveTabKey live_tab;
    base::CancelableTaskTracker::TaskId task_id =
        base::CancelableTaskTracker::kBadTaskId;
    uint64_t generation = 0;
    EssentialIconSource source = EssentialIconSource::kFallback;
  };

  void RebuildFromSnapshot(const ShellSnapshot &snapshot);
  void CancelAllFaviconRequestsAndInvalidateCallbacks();
  void CancelFaviconRequest(EssentialIconBinding &binding);
  void ResolveEssentialIcon(EssentialIconBinding &binding,
                            const ShellEssentialItem &essential);
  void StartCachedFaviconLookup(EssentialIconBinding &binding);
  void OnCachedFaviconAvailable(EssentialId id, GURL requested_url,
                                uint64_t generation,
                                const favicon_base::FaviconImageResult &result);
  ui::ImageModel FindLiveFavicon(const LiveTabKey &live_tab) const;
  void ApplyEssentialIcon(views::LabelButton *button,
                          const ui::ImageModel &icon);
  void ApplyDefaultEssentialIcon(views::LabelButton *button);
  void OnEssentialsOverflowPressed();
  void ShowSplitChooser();

  raw_ptr<ShellController> controller_ = nullptr;
  raw_ptr<BrowserWindowInterface> browser_window_ = nullptr;
  raw_ptr<Profile> profile_ = nullptr;
  raw_ptr<views::View> essentials_container_ = nullptr;
  raw_ptr<views::BoxLayout> essentials_layout_ = nullptr;
  raw_ptr<views::LabelButton> essentials_overflow_button_ = nullptr;
  std::vector<EssentialIconBinding> essential_icon_bindings_;
  std::unique_ptr<ui::SimpleMenuModel> essentials_overflow_menu_model_;
  std::unique_ptr<views::MenuRunner> essentials_overflow_menu_runner_;
  std::unique_ptr<SeoulSplitChooserView> split_chooser_;
  std::vector<ShellEssentialItem> rendered_essentials_;
  std::vector<ShellEssentialItem> overflow_essentials_;
  bool essentials_initialized_ = false;
  bool essentials_collapsed_ = false;
  bool presentation_collapsed_ = false;
  base::CancelableTaskTracker favicon_task_tracker_;
  CachedFaviconLookupForTesting cached_favicon_lookup_for_testing_;
  base::WeakPtrFactory<SeoulShellHeaderView> weak_factory_{this};
};

} // namespace seoul

#endif // SEOUL_BROWSER_SHELL_VIEWS_SEOUL_SHELL_HEADER_VIEW_H_
