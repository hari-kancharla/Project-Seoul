// Project Seoul native lifecycle bridge.

#include "seoul/browser/lifecycle/tab_strip_bridge.h"

#include <map>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/sessions/core/session_id.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "seoul/browser/lifecycle/lifecycle_event_sink.h"
#include "seoul/browser/lifecycle/live_window_state.h"
#include "seoul/browser/lifecycle/session_restore_metadata.h"
#include "seoul/browser/organization/organization_model.h"

namespace seoul {

TabStripBridge::TabStripBridge(LiveWindowKey window,
                               BrowserWindowInterface* browser,
                               TabStripModel* model,
                               LifecycleEventSink* sink,
                               LiveWindowStateProvider* live_state,
                               OrganizationModel* organization)
    : window_(window),
      browser_(browser),
      model_(model),
      sink_(sink),
      live_state_(live_state),
      organization_(organization) {
  model_->AddObserver(this);
}

TabStripBridge::~TabStripBridge() {
  if (model_) {
    model_->RemoveObserver(this);
  }
}

TabRemovalKind TabStripBridge::ClassifyRemoval(TabRemovedReason reason) const {
  if (model_ && model_->closing_all()) {
    return TabRemovalKind::kWindowShutdown;
  }
  if (browser_ && browser_->IsDeleteScheduled()) {
    return TabRemovalKind::kWindowShutdown;
  }
  switch (reason) {
    case TabRemovedReason::kDeleted:
      return TabRemovalKind::kGenuineClose;
    case TabRemovedReason::kInsertedIntoOtherTabStrip:
      return TabRemovalKind::kTransferOut;
    case TabRemovedReason::kInsertedIntoSidePanel:
      return TabRemovalKind::kSidePanel;
    case TabRemovedReason::kDeletedAndExpandSidePanel:
      return TabRemovalKind::kGenuineClose;
  }
  return TabRemovalKind::kUnknown;
}

// static
LiveTabKey TabStripBridge::KeyForContents(
    const content::WebContents* contents) {
  if (!contents) {
    return LiveTabKey();
  }
  const SessionID id = sessions::SessionTabHelper::IdForTab(contents);
  return id.is_valid() ? LiveTabKey::FromSessionId(id.id()) : LiveTabKey();
}

// static
LiveTabKey TabStripBridge::KeyForTab(const tabs::TabInterface* tab) {
  return tab ? KeyForContents(tab->GetContents()) : LiveTabKey();
}

void TabStripBridge::EmitTabEvent(NormalizedEventType type,
                                  const LiveTabKey& tab,
                                  int order_index,
                                  int batch_sequence,
                                  TabInsertKind insert_kind,
                                  TabRemovalKind removal_kind) {
  NormalizedEvent event;
  event.type = type;
  event.window = window_;
  event.tab = tab;
  event.order_index = order_index;
  event.batch_sequence = batch_sequence;
  event.insert_kind = insert_kind;
  event.removal_kind = removal_kind;
  if (type == NormalizedEventType::kTabInserted) {
    event.restored_membership = RestoredMembershipForTab(ContentsForKey(tab));
  }
  sink_->OnNormalizedEvent(event);
  if (type == NormalizedEventType::kTabInserted) {
    PersistMembershipForTab(tab);
    ScheduleMembershipPersistence(tab);
    PersistSyntheticPlaceholderForTab(tab);
    ScheduleSyntheticPlaceholderPersistence(tab);
  }
}

content::WebContents* TabStripBridge::ContentsForKey(
    const LiveTabKey& tab) const {
  if (!model_ || !tab.is_valid()) {
    return nullptr;
  }
  for (int index = 0; index < model_->count(); ++index) {
    content::WebContents* contents = model_->GetWebContentsAt(index);
    if (KeyForContents(contents) == tab) {
      return contents;
    }
  }
  return nullptr;
}

void TabStripBridge::PersistMembershipForTab(const LiveTabKey& tab) {
  if (!organization_ || !tab.is_valid()) {
    return;
  }
  const TabMembershipId membership =
      organization_->FindMembershipIdByTabKey(tab.value());
  if (!membership.is_valid()) {
    return;
  }
  PersistSeoulSessionMetadata(browser_, ContentsForKey(tab), membership);
}

void TabStripBridge::ScheduleMembershipPersistence(const LiveTabKey& tab) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&TabStripBridge::PersistMembershipForTab,
                                weak_factory_.GetWeakPtr(), tab));
}

void TabStripBridge::PersistSyntheticPlaceholderForTab(const LiveTabKey& tab) {
  PersistSeoulSyntheticNewTabPlaceholderMetadata(browser_, ContentsForKey(tab));
}

void TabStripBridge::ScheduleSyntheticPlaceholderPersistence(
    const LiveTabKey& tab) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&TabStripBridge::PersistSyntheticPlaceholderForTab,
                     weak_factory_.GetWeakPtr(), tab));
}

void TabStripBridge::EnumerateExistingState() {
  if (enumerated_ || !model_ || !sink_) {
    return;
  }
  enumerated_ = true;
  RescanExistingState();
}

void TabStripBridge::RescanExistingState() {
  if (!model_ || !sink_) {
    return;
  }

  std::set<LiveTabKey> current_tabs;
  std::map<LiveTabKey, int> current_order;
  std::map<LiveTabKey, bool> current_pinned;

  const int count = model_->count();
  for (int index = 0; index < count; ++index) {
    tabs::TabInterface* tab = model_->GetTabAtIndex(index);
    const LiveTabKey key = KeyForTab(tab);
    if (!key.is_valid()) {
      continue;
    }
    current_tabs.insert(key);
    current_order[key] = index;
    current_pinned[key] = model_->IsTabPinned(index);
  }

  for (const LiveTabKey& key : last_enumerated_tabs_) {
    if (!current_tabs.count(key)) {
      EmitTabEvent(NormalizedEventType::kTabRemoved, key, -1, 0,
                   TabInsertKind::kUnknown, TabRemovalKind::kGenuineClose);
    }
  }

  for (const LiveTabKey& key : current_tabs) {
    if (!last_enumerated_tabs_.count(key)) {
      EmitTabEvent(NormalizedEventType::kTabInserted, key, current_order[key],
                   0, TabInsertKind::kExisting, TabRemovalKind::kUnknown);
    } else {
      EmitTabEvent(NormalizedEventType::kTabMoved, key, current_order[key], 0);
    }
    const bool pinned = current_pinned[key];
    if (!last_enumerated_tabs_.count(key) && pinned) {
      NormalizedEvent pinned_event;
      pinned_event.type = NormalizedEventType::kPinnedStateChanged;
      pinned_event.window = window_;
      pinned_event.tab = key;
      pinned_event.order_index = current_order[key];
      pinned_event.pinned = true;
      sink_->OnNormalizedEvent(pinned_event);
    }
  }

  const int active = model_->active_index();
  if (active >= 0 && active < count) {
    EmitTabEvent(NormalizedEventType::kActiveTabChanged,
                 KeyForTab(model_->GetTabAtIndex(active)), -1, 0);
  }

  std::set<std::string> current_splits;
  for (int index = 0; index < count; ++index) {
    const std::optional<split_tabs::SplitTabId> split_id =
        model_->GetSplitForTab(index);
    if (!split_id.has_value()) {
      continue;
    }
    const std::string token = split_id->ToString();
    if (!current_splits.insert(token).second) {
      continue;
    }
    split_tabs::SplitTabData* split_data =
        model_->GetSplitData(split_id.value());
    if (!split_data) {
      continue;
    }
    const std::vector<tabs::TabInterface*> panes = split_data->ListTabs();
    if (panes.size() < 2) {
      continue;
    }
    if (last_enumerated_splits_.count(token)) {
      NormalizedEvent changed;
      changed.type = NormalizedEventType::kSplitContentsChanged;
      changed.window = window_;
      changed.upstream_split_token = token;
      changed.split_pane_a = KeyForTab(panes[0]);
      changed.split_pane_b = KeyForTab(panes[1]);
      sink_->OnNormalizedEvent(changed);
    } else {
      NormalizedEvent event;
      event.type = NormalizedEventType::kSplitAdded;
      event.window = window_;
      event.upstream_split_token = token;
      event.split_pane_a = KeyForTab(panes[0]);
      event.split_pane_b = KeyForTab(panes[1]);
      event.divider_ratio = split_data->visual_data()->split_ratio();
      sink_->OnNormalizedEvent(event);
    }
  }

  for (const std::string& token : last_enumerated_splits_) {
    if (!current_splits.count(token)) {
      NormalizedEvent removed;
      removed.type = NormalizedEventType::kSplitRemoved;
      removed.window = window_;
      removed.upstream_split_token = token;
      sink_->OnNormalizedEvent(removed);
    }
  }

  last_enumerated_tabs_ = std::move(current_tabs);
  last_enumerated_splits_ = std::move(current_splits);
  enumerated_ = true;
  PublishLiveSnapshot();
}

void TabStripBridge::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  switch (change.type()) {
    case TabStripModelChange::kInserted: {
      const TabStripModelChange::Insert* insert = change.GetInsert();
      int seq = 0;
      for (const auto& entry : insert->contents) {
        EmitTabEvent(NormalizedEventType::kTabInserted,
                     KeyForContents(entry.contents), entry.index, seq++,
                     TabInsertKind::kNew);
      }
      break;
    }
    case TabStripModelChange::kRemoved: {
      const TabStripModelChange::Remove* remove = change.GetRemove();
      int seq = 0;
      for (const auto& entry : remove->contents) {
        // Insertions and every live target use the WebContents SessionTabHelper
        // id. Chromium's removal entry also carries a tab-model session id, but
        // that is a different identity on current builds. Prefer the same
        // WebContents-derived key used on insertion; use the removal snapshot
        // only if the detached tab no longer exposes its contents.
        LiveTabKey key = KeyForTab(entry.tab);
        if (!key.is_valid() && entry.session_id.has_value() &&
            entry.session_id->is_valid()) {
          key = LiveTabKey::FromSessionId(entry.session_id->id());
        }
        EmitTabEvent(NormalizedEventType::kTabRemoved, key, entry.index, seq++,
                     TabInsertKind::kUnknown,
                     ClassifyRemoval(entry.remove_reason));
      }
      break;
    }
    case TabStripModelChange::kMoved: {
      const TabStripModelChange::Move* move = change.GetMove();
      EmitTabEvent(NormalizedEventType::kTabMoved, KeyForTab(move->tab),
                   move->to_index, 0);
      break;
    }
    case TabStripModelChange::kReplaced: {
      const TabStripModelChange::Replace* replace = change.GetReplace();
      EmitTabEvent(NormalizedEventType::kTabReplaced, KeyForTab(replace->tab),
                   replace->index, 0);
      break;
    }
    case TabStripModelChange::kSelectionOnly:
      break;
  }

  if (selection.active_tab_changed() && selection.new_tab) {
    EmitTabEvent(NormalizedEventType::kActiveTabChanged,
                 KeyForTab(selection.new_tab), -1, 0);
  }
  PublishLiveSnapshot();
}

void TabStripBridge::OnTabCloseCancelled(const tabs::TabInterface* tab) {
  EmitTabEvent(NormalizedEventType::kTabCloseCancelled, KeyForTab(tab), -1, 0);
}

void TabStripBridge::OnTabChangedAt(tabs::TabInterface* tab,
                                    int index,
                                    TabChangeType change_type) {
  (void)index;
  if (change_type != TabChangeType::kAll &&
      change_type != TabChangeType::kLoadingOnly) {
    return;
  }
  // Chromium reports navigation commit/loading transitions as kLoadingOnly.
  // They do not alter tab-strip structure, but the committed URL can already
  // have changed. Rebuild the minimized descriptor for both kAll and
  // kLoadingOnly so Canvas, Boosts, and command search never retain the prior
  // page's origin or title. PublishSnapshot de-duplicates unchanged metadata.
  PersistSyntheticPlaceholderForTab(KeyForTab(tab));
  PublishLiveSnapshot();
}

void TabStripBridge::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                             int index) {
  NormalizedEvent event;
  event.type = NormalizedEventType::kPinnedStateChanged;
  event.window = window_;
  event.tab = KeyForTab(tab);
  event.order_index = index;
  event.pinned = model_->IsTabPinned(index);
  sink_->OnNormalizedEvent(event);
  PublishLiveSnapshot();
}

void TabStripBridge::OnSplitTabChanged(const SplitTabChange& change) {
  NormalizedEvent event;
  event.window = window_;
  event.upstream_split_token = change.split_id.ToString();

  switch (change.type) {
    case SplitTabChange::Type::kAdded: {
      const SplitTabChange::AddedChange* added = change.GetAddedChange();
      event.type = NormalizedEventType::kSplitAdded;
      const auto& tabs = added->tabs();
      if (tabs.size() >= 2) {
        event.split_pane_a = KeyForTab(tabs[0].first);
        event.split_pane_b = KeyForTab(tabs[1].first);
      }
      event.divider_ratio = added->visual_data().split_ratio();
      break;
    }
    case SplitTabChange::Type::kContentsChanged: {
      const SplitTabChange::ContentsChange* contents =
          change.GetContentsChange();
      event.type = NormalizedEventType::kSplitContentsChanged;
      const auto& tabs = contents->new_tabs();
      if (tabs.size() >= 2) {
        event.split_pane_a = KeyForTab(tabs[0].first);
        event.split_pane_b = KeyForTab(tabs[1].first);
      }
      break;
    }
    case SplitTabChange::Type::kVisualsChanged: {
      const SplitTabChange::VisualsChange* visuals = change.GetVisualsChange();
      event.type = NormalizedEventType::kSplitVisualsChanged;
      event.divider_ratio = visuals->new_visual_data().split_ratio();
      // M149 SplitTabChange::VisualsChange has no is_intermediate(). Ratio
      // bursts are coalesced by PersistenceScheduler at the service layer.
      event.split_visuals_intermediate = false;
      break;
    }
    case SplitTabChange::Type::kRemoved:
      event.type = NormalizedEventType::kSplitRemoved;
      break;
  }
  sink_->OnNormalizedEvent(event);
  PublishLiveSnapshot();
}

void TabStripBridge::PublishLiveSnapshot() {
  if (!live_state_ || !model_) {
    return;
  }
  live_state_->PublishSnapshot(window_, model_.get());
}

void TabStripBridge::OnTabStripModelDestroyed(TabStripModel* tab_strip_model) {
  if (model_) {
    model_->RemoveObserver(this);
    model_ = nullptr;
  }
  NormalizedEvent event;
  event.type = NormalizedEventType::kTabStripDestroyed;
  event.window = window_;
  sink_->OnNormalizedEvent(event);
}

}  // namespace seoul
