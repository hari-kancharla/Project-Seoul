// Project Seoul outbound browser command layer.

#ifndef SEOUL_BROWSER_COMMANDS_CHROMIUM_MUTATION_ADAPTER_H_
#define SEOUL_BROWSER_COMMANDS_CHROMIUM_MUTATION_ADAPTER_H_

#include "seoul/browser/commands/browser_command.h"
#include "seoul/browser/commands/command_errors.h"
#include "seoul/browser/commands/target_resolver.h"

class Profile;

namespace seoul {

class ChromiumMutationAdapter {
 public:
  virtual ~ChromiumMutationAdapter() = default;

  // Open Chromium's normal New Tab Page in `window` via the dedicated new-tab
  // path (no URL navigation, no URL policy). On success `out_tab` is the
  // inserted tab's live key.
  virtual CommandStatusResult OpenNewTab(
      Profile* profile,
      const ResolvedWindowTarget& window,
      CommandForegroundDisposition disposition,
      LiveTabKey* out_tab) = 0;

  virtual CommandStatusResult OpenTab(Profile* profile,
                                      const ResolvedWindowTarget& window,
                                      const GURL& url,
                                      CommandForegroundDisposition disposition,
                                      LiveTabKey* out_tab) = 0;

  virtual CommandStatusResult NavigateTab(Profile* profile,
                                          const ResolvedTabTarget& tab,
                                          const GURL& url) = 0;

  // Opens `url` as a new tab and immediately pairs it with `existing_tab` in
  // Chromium's real split model. The implementation rolls the new tab back if
  // split creation fails, so callers never receive a false "split" success.
  virtual CommandStatusResult OpenTabInSplit(
      Profile* profile,
      const ResolvedWindowTarget& window,
      LiveTabKey existing_tab,
      const GURL& url,
      double ratio,
      LiveTabKey* out_tab,
      std::string* upstream_token) = 0;

  virtual CommandStatusResult OpenExternal(Profile* profile,
                                           const GURL& url) = 0;

  virtual CommandStatusResult ActivateTab(Profile* profile,
                                          const ResolvedTabTarget& tab) = 0;

  virtual CommandStatusResult CloseTab(Profile* profile,
                                       const ResolvedTabTarget& tab) = 0;

  virtual CommandStatusResult SetPinned(Profile* profile,
                                        const ResolvedTabTarget& tab,
                                        bool pinned) = 0;

  virtual CommandStatusResult MoveTab(Profile* profile,
                                      const ResolvedTabTarget& tab,
                                      int destination_index) = 0;

  virtual CommandStatusResult CreateSplit(Profile* profile,
                                          const ResolvedSplitTarget& split,
                                          double ratio,
                                          std::string* upstream_token) = 0;

  virtual CommandStatusResult DissolveSplit(
      Profile* profile,
      LiveWindowKey window,
      const std::string& upstream_token) = 0;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_COMMANDS_CHROMIUM_MUTATION_ADAPTER_H_
