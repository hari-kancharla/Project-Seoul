// Project Seoul outbound browser command layer.

#ifndef SEOUL_BROWSER_COMMANDS_BROWSER_COMMAND_H_
#define SEOUL_BROWSER_COMMANDS_BROWSER_COMMAND_H_

#include <optional>
#include <string>
#include <vector>

#include "seoul/browser/commands/command_id.h"
#include "seoul/browser/commands/command_types.h"
#include "seoul/browser/organization/organization_ids.h"
#include "seoul/browser/organization/organization_types.h"
#include "url/gurl.h"

namespace seoul {

struct BrowserCommand {
  BrowserCommand();
  BrowserCommand(const BrowserCommand&);
  BrowserCommand(BrowserCommand&&);
  BrowserCommand& operator=(const BrowserCommand&);
  BrowserCommand& operator=(BrowserCommand&&);
  ~BrowserCommand();

  CommandId id;
  CommandKind kind = CommandKind::kCreateWorkspace;
  CommandOrigin origin = CommandOrigin::kUser;

  // Model-only payloads.
  std::optional<WorkspaceId> workspace_id;
  std::optional<TabMembershipId> membership_id;
  std::optional<EssentialId> essential_id;
  std::optional<RoutingRuleId> routing_rule_id;
  std::string name;
  std::string icon;
  int order = 0;
  RoutingRule routing_rule;
  std::string saved_root_url;

  // Chromium-affecting payloads.
  LiveWindowKey window;
  LiveTabKey tab;
  LiveTabKey split_pane_b;
  GURL url;
  int destination_index = -1;
  CommandForegroundDisposition foreground =
      CommandForegroundDisposition::kForeground;
  double split_ratio = 0.5;
  std::string upstream_split_token;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_COMMANDS_BROWSER_COMMAND_H_
