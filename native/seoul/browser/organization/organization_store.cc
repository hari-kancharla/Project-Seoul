// Project Seoul native organization engine.

#include "seoul/browser/organization/organization_store.h"

#include <string>
#include <utility>

#include "base/json/json_writer.h"
#include "base/json/values_util.h"
#include "seoul/browser/organization/organization_limits.h"

namespace seoul {

namespace {

// Enum <-> int with range validation, so an out-of-range stored value is
// rejected rather than reinterpreted.
template <typename Enum>
bool ReadEnum(const base::DictValue& d,
              const char* key,
              int max_inclusive,
              Enum* out) {
  std::optional<int> v = d.FindInt(key);
  if (!v || *v < 0 || *v > max_inclusive) {
    return false;
  }
  *out = static_cast<Enum>(*v);
  return true;
}

base::DictValue WorkspaceToDict(const WorkspaceRecord& w) {
  base::DictValue d;
  d.Set("id", w.id.value());
  d.Set("name", w.name);
  d.Set("icon", w.icon);
  d.Set("order", w.order);
  d.Set("created_at", base::TimeToValue(w.created_at));
  d.Set("last_active_at", base::TimeToValue(w.last_active_at));
  d.Set("archived", w.archived);
  d.Set("is_default", w.is_default);
  d.Set("isolated", w.isolated);
  return d;
}

base::DictValue EssentialToDict(const EssentialRecord& e) {
  base::DictValue d;
  d.Set("id", e.id.value());
  d.Set("kind", static_cast<int>(e.kind));
  d.Set("name", e.name);
  d.Set("icon", e.icon);
  d.Set("root_url", e.root_url);
  d.Set("order", e.order);
  d.Set("created_at", base::TimeToValue(e.created_at));
  return d;
}

base::DictValue MembershipToDict(const TabMembershipRecord& m) {
  base::DictValue d;
  d.Set("id", m.id.value());
  d.Set("workspace_id", m.workspace_id.value());
  d.Set("tab_key", m.tab_key);
  d.Set("role", static_cast<int>(m.role));
  d.Set("saved_root_url", m.saved_root_url);
  d.Set("order", m.order);
  d.Set("created_at", base::TimeToValue(m.created_at));
  d.Set("last_active_at", base::TimeToValue(m.last_active_at));
  return d;
}

base::DictValue SplitToDict(const SplitGroupRecord& s) {
  base::DictValue d;
  d.Set("id", s.id.value());
  d.Set("workspace_id", s.workspace_id.value());
  base::ListValue panes;
  for (const std::string& key : s.pane_tab_keys) {
    panes.Append(key);
  }
  d.Set("panes", std::move(panes));
  d.Set("divider_ratio", s.divider_ratio);
  d.Set("active_pane_index", s.active_pane_index);
  d.Set("upstream_split_token", s.upstream_split_token);
  d.Set("created_at", base::TimeToValue(s.created_at));
  return d;
}

base::DictValue RoutingToDict(const RoutingRule& r) {
  base::DictValue d;
  d.Set("id", r.id.value());
  d.Set("priority", r.priority);
  d.Set("enabled", r.enabled);
  d.Set("match_type", static_cast<int>(r.predicate.match_type));
  d.Set("pattern", r.predicate.pattern);
  d.Set("source_workspace", r.predicate.source_workspace.value());
  d.Set("require_user_gesture", r.predicate.require_user_gesture);
  d.Set("disposition", static_cast<int>(r.result.disposition));
  d.Set("target_workspace", r.result.target_workspace.value());
  return d;
}

base::DictValue ArchivedToDict(const ArchivedTabRecord& a) {
  base::DictValue d;
  d.Set("original_id", a.original_id.value());
  d.Set("workspace_id", a.workspace_id.value());
  d.Set("original_role", static_cast<int>(a.original_role));
  d.Set("saved_root_url", a.saved_root_url);
  d.Set("title", a.title);
  d.Set("archived_at", base::TimeToValue(a.archived_at));
  return d;
}

// Required-string reader; returns false if missing or not a string.
bool ReadString(const base::DictValue& d, const char* key, std::string* out) {
  const std::string* s = d.FindString(key);
  if (!s) {
    return false;
  }
  *out = *s;
  return true;
}

base::Time ReadTime(const base::DictValue& d, const char* key) {
  const base::Value* v = d.Find(key);
  std::optional<base::Time> t = v ? base::ValueToTime(*v) : std::nullopt;
  return t.value_or(base::Time());
}

}  // namespace

base::DictValue SerializeSnapshot(const OrganizationSnapshot& snapshot) {
  base::DictValue root;
  root.Set("schema_version", snapshot.schema_version);
  root.Set("default_workspace", snapshot.default_workspace_id.value());

  base::ListValue workspaces;
  for (const WorkspaceRecord& w : snapshot.workspaces) {
    workspaces.Append(WorkspaceToDict(w));
  }
  root.Set("workspaces", std::move(workspaces));

  base::ListValue essentials;
  for (const EssentialRecord& e : snapshot.essentials) {
    essentials.Append(EssentialToDict(e));
  }
  root.Set("essentials", std::move(essentials));

  base::ListValue memberships;
  for (const TabMembershipRecord& m : snapshot.memberships) {
    memberships.Append(MembershipToDict(m));
  }
  root.Set("memberships", std::move(memberships));

  base::ListValue splits;
  for (const SplitGroupRecord& s : snapshot.splits) {
    splits.Append(SplitToDict(s));
  }
  root.Set("splits", std::move(splits));

  base::ListValue windows;
  for (const WindowWorkspaceState& ws : snapshot.window_states) {
    base::DictValue d;
    d.Set("window_key", ws.window_key);
    d.Set("active_workspace", ws.active_workspace_id.value());
    windows.Append(std::move(d));
  }
  root.Set("windows", std::move(windows));

  base::ListValue routing;
  for (const RoutingRule& r : snapshot.routing_rules) {
    routing.Append(RoutingToDict(r));
  }
  root.Set("routing_rules", std::move(routing));

  base::ListValue archived;
  for (const ArchivedTabRecord& a : snapshot.archived_tabs) {
    archived.Append(ArchivedToDict(a));
  }
  root.Set("archived_tabs", std::move(archived));

  return root;
}

MutationResult<OrganizationSnapshot> DeserializeSnapshot(
    const base::DictValue& dict) {
  std::optional<int> version = dict.FindInt("schema_version");
  if (!version) {
    return Err(OrganizationError::kCorruptState);
  }
  // Reject unknown/future versions; do not downgrade or guess.
  if (*version != kOrganizationSchemaVersion) {
    return Err(OrganizationError::kUnsupportedSchema);
  }

  OrganizationSnapshot snap;
  snap.schema_version = *version;
  const std::string* def = dict.FindString("default_workspace");
  snap.default_workspace_id =
      def ? WorkspaceId::FromString(*def) : WorkspaceId();

  const base::ListValue* workspaces = dict.FindList("workspaces");
  if (workspaces && workspaces->size() > kMaxWorkspaces) {
    return Err(OrganizationError::kLimitExceeded);
  }
  if (workspaces) {
    for (const base::Value& v : *workspaces) {
      const base::DictValue* d = v.GetIfDict();
      if (!d) {
        return Err(OrganizationError::kCorruptState);
      }
      WorkspaceRecord w;
      std::string id, name;
      if (!ReadString(*d, "id", &id) || !ReadString(*d, "name", &name)) {
        return Err(OrganizationError::kCorruptState);
      }
      w.id = WorkspaceId::FromString(id);
      w.name = name;
      if (const std::string* icon = d->FindString("icon")) {
        w.icon = *icon;
      }
      w.order = d->FindInt("order").value_or(0);
      w.created_at = ReadTime(*d, "created_at");
      w.last_active_at = ReadTime(*d, "last_active_at");
      w.archived = d->FindBool("archived").value_or(false);
      w.is_default = d->FindBool("is_default").value_or(false);
      // Absent in stores written before containers existed, and absent means
      // not isolated - never a default that would silently partition an
      // existing Space and hide everything already stored in it.
      w.isolated = d->FindBool("isolated").value_or(false);
      snap.workspaces.push_back(std::move(w));
    }
  }

  const base::ListValue* essentials = dict.FindList("essentials");
  if (essentials && essentials->size() > kMaxEssentials) {
    return Err(OrganizationError::kLimitExceeded);
  }
  if (essentials) {
    for (const base::Value& v : *essentials) {
      const base::DictValue* d = v.GetIfDict();
      if (!d) {
        return Err(OrganizationError::kCorruptState);
      }
      EssentialRecord e;
      std::string id, name, url;
      if (!ReadString(*d, "id", &id) || !ReadString(*d, "name", &name) ||
          !ReadString(*d, "root_url", &url)) {
        return Err(OrganizationError::kCorruptState);
      }
      e.id = EssentialId::FromString(id);
      e.name = name;
      e.root_url = url;
      if (!ReadEnum(*d, "kind", /*max=*/0, &e.kind)) {
        return Err(OrganizationError::kCorruptState);
      }
      if (const std::string* icon = d->FindString("icon")) {
        e.icon = *icon;
      }
      e.order = d->FindInt("order").value_or(0);
      e.created_at = ReadTime(*d, "created_at");
      snap.essentials.push_back(std::move(e));
    }
  }

  const base::ListValue* memberships = dict.FindList("memberships");
  if (memberships && memberships->size() > kMaxTotalMemberships) {
    return Err(OrganizationError::kLimitExceeded);
  }
  if (memberships) {
    for (const base::Value& v : *memberships) {
      const base::DictValue* d = v.GetIfDict();
      if (!d) {
        return Err(OrganizationError::kCorruptState);
      }
      TabMembershipRecord m;
      std::string id, ws, tab_key;
      if (!ReadString(*d, "id", &id) || !ReadString(*d, "workspace_id", &ws) ||
          !ReadString(*d, "tab_key", &tab_key)) {
        return Err(OrganizationError::kCorruptState);
      }
      m.id = TabMembershipId::FromString(id);
      m.workspace_id = WorkspaceId::FromString(ws);
      m.tab_key = tab_key;
      if (!ReadEnum(*d, "role", /*max=*/2, &m.role)) {
        return Err(OrganizationError::kCorruptState);
      }
      if (const std::string* s = d->FindString("saved_root_url")) {
        m.saved_root_url = *s;
      }
      m.order = d->FindInt("order").value_or(0);
      m.created_at = ReadTime(*d, "created_at");
      m.last_active_at = ReadTime(*d, "last_active_at");
      snap.memberships.push_back(std::move(m));
    }
  }

  const base::ListValue* splits = dict.FindList("splits");
  if (splits && splits->size() > kMaxTotalSplits) {
    return Err(OrganizationError::kLimitExceeded);
  }
  if (splits) {
    for (const base::Value& v : *splits) {
      const base::DictValue* d = v.GetIfDict();
      if (!d) {
        return Err(OrganizationError::kCorruptState);
      }
      SplitGroupRecord s;
      std::string id, ws;
      if (!ReadString(*d, "id", &id) || !ReadString(*d, "workspace_id", &ws)) {
        return Err(OrganizationError::kCorruptState);
      }
      s.id = SplitGroupId::FromString(id);
      s.workspace_id = WorkspaceId::FromString(ws);
      const base::ListValue* panes = d->FindList("panes");
      if (!panes) {
        return Err(OrganizationError::kCorruptState);
      }
      for (const base::Value& p : *panes) {
        const std::string* key = p.GetIfString();
        if (!key) {
          return Err(OrganizationError::kCorruptState);
        }
        s.pane_tab_keys.push_back(*key);
      }
      s.divider_ratio = d->FindDouble("divider_ratio").value_or(0.5);
      s.active_pane_index = d->FindInt("active_pane_index").value_or(0);
      if (const std::string* tok = d->FindString("upstream_split_token")) {
        s.upstream_split_token = *tok;
      }
      s.created_at = ReadTime(*d, "created_at");
      snap.splits.push_back(std::move(s));
    }
  }

  const base::ListValue* windows = dict.FindList("windows");
  if (windows && windows->size() > kMaxWindowStates) {
    return Err(OrganizationError::kLimitExceeded);
  }
  if (windows) {
    for (const base::Value& v : *windows) {
      const base::DictValue* d = v.GetIfDict();
      if (!d) {
        return Err(OrganizationError::kCorruptState);
      }
      WindowWorkspaceState ws;
      std::string key, active;
      if (!ReadString(*d, "window_key", &key) ||
          !ReadString(*d, "active_workspace", &active)) {
        return Err(OrganizationError::kCorruptState);
      }
      ws.window_key = key;
      ws.active_workspace_id = WorkspaceId::FromString(active);
      snap.window_states.push_back(std::move(ws));
    }
  }

  const base::ListValue* routing = dict.FindList("routing_rules");
  if (routing) {
    if (routing->size() > kMaxRoutingRules) {
      return Err(OrganizationError::kLimitExceeded);
    }
    for (const base::Value& v : *routing) {
      const base::DictValue* d = v.GetIfDict();
      if (!d) {
        return Err(OrganizationError::kCorruptState);
      }
      RoutingRule r;
      std::string id;
      if (!ReadString(*d, "id", &id)) {
        return Err(OrganizationError::kCorruptState);
      }
      r.id = RoutingRuleId::FromString(id);
      r.priority = d->FindInt("priority").value_or(0);
      r.enabled = d->FindBool("enabled").value_or(true);
      if (!ReadEnum(*d, "match_type", /*max=*/3, &r.predicate.match_type)) {
        return Err(OrganizationError::kInvalidRoutingRule);
      }
      if (const std::string* p = d->FindString("pattern")) {
        r.predicate.pattern = *p;
      }
      if (const std::string* sw = d->FindString("source_workspace")) {
        r.predicate.source_workspace = WorkspaceId::FromString(*sw);
      }
      r.predicate.require_user_gesture =
          d->FindBool("require_user_gesture").value_or(false);
      if (!ReadEnum(*d, "disposition", /*max=*/7, &r.result.disposition)) {
        return Err(OrganizationError::kInvalidRoutingRule);
      }
      if (const std::string* tw = d->FindString("target_workspace")) {
        r.result.target_workspace = WorkspaceId::FromString(*tw);
      }
      snap.routing_rules.push_back(std::move(r));
    }
  }

  const base::ListValue* archived = dict.FindList("archived_tabs");
  if (archived) {
    if (archived->size() > kMaxArchivedTabs) {
      return Err(OrganizationError::kLimitExceeded);
    }
    for (const base::Value& v : *archived) {
      const base::DictValue* d = v.GetIfDict();
      if (!d) {
        return Err(OrganizationError::kCorruptState);
      }
      ArchivedTabRecord a;
      std::string id, ws;
      if (!ReadString(*d, "original_id", &id) ||
          !ReadString(*d, "workspace_id", &ws)) {
        return Err(OrganizationError::kCorruptState);
      }
      a.original_id = TabMembershipId::FromString(id);
      a.workspace_id = WorkspaceId::FromString(ws);
      if (!ReadEnum(*d, "original_role", /*max=*/2, &a.original_role)) {
        return Err(OrganizationError::kCorruptState);
      }
      if (const std::string* u = d->FindString("saved_root_url")) {
        a.saved_root_url = *u;
      }
      if (const std::string* t = d->FindString("title")) {
        a.title = *t;
      }
      a.archived_at = ReadTime(*d, "archived_at");
      snap.archived_tabs.push_back(std::move(a));
    }
  }

  return snap;
}

bool SerializedSizeWithinLimit(const base::DictValue& dict) {
  std::string json;
  if (!base::JSONWriter::Write(dict, &json)) {
    return false;
  }
  return json.size() <= kMaxSerializedBytes;
}

}  // namespace seoul
