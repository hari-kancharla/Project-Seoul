// Project Seoul outbound browser command layer.

#ifndef SEOUL_BROWSER_COMMANDS_CHROMIUM_MUTATION_ADAPTER_IMPL_H_
#define SEOUL_BROWSER_COMMANDS_CHROMIUM_MUTATION_ADAPTER_IMPL_H_

#include "seoul/browser/commands/chromium_mutation_adapter.h"

namespace seoul {

class ChromiumMutationAdapterImpl : public ChromiumMutationAdapter {
 public:
  ChromiumMutationAdapterImpl();
  ChromiumMutationAdapterImpl(const ChromiumMutationAdapterImpl&) = delete;
  ChromiumMutationAdapterImpl& operator=(const ChromiumMutationAdapterImpl&) =
      delete;
  ~ChromiumMutationAdapterImpl() override;

  CommandStatusResult OpenNewTab(Profile* profile,
                                 const ResolvedWindowTarget& window,
                                 CommandForegroundDisposition disposition,
                                 LiveTabKey* out_tab) override;

  CommandStatusResult OpenTab(Profile* profile,
                              const ResolvedWindowTarget& window,
                              const GURL& url,
                              CommandForegroundDisposition disposition,
                              LiveTabKey* out_tab) override;

  CommandStatusResult NavigateTab(Profile* profile,
                                  const ResolvedTabTarget& tab,
                                  const GURL& url) override;

  CommandStatusResult OpenTabInSplit(Profile* profile,
                                     const ResolvedWindowTarget& window,
                                     LiveTabKey existing_tab,
                                     const GURL& url,
                                     double ratio,
                                     LiveTabKey* out_tab,
                                     std::string* upstream_token) override;

  CommandStatusResult OpenExternal(Profile* profile,
                                   const GURL& url) override;

  CommandStatusResult ActivateTab(Profile* profile,
                                  const ResolvedTabTarget& tab) override;

  CommandStatusResult CloseTab(Profile* profile,
                               const ResolvedTabTarget& tab) override;

  CommandStatusResult SetPinned(Profile* profile,
                                const ResolvedTabTarget& tab,
                                bool pinned) override;

  CommandStatusResult MoveTab(Profile* profile,
                              const ResolvedTabTarget& tab,
                              int destination_index) override;

  CommandStatusResult CreateSplit(Profile* profile,
                                  const ResolvedSplitTarget& split,
                                  double ratio,
                                  std::string* upstream_token) override;

  CommandStatusResult DissolveSplit(Profile* profile,
                                    LiveWindowKey window,
                                    const std::string& upstream_token) override;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_COMMANDS_CHROMIUM_MUTATION_ADAPTER_IMPL_H_
