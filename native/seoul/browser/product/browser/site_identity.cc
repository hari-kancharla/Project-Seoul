// Project Seoul site identity - forgetting a site.

#include "seoul/browser/product/browser/site_identity.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_constants.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_service_factory.h"
#include "url/gurl.h"

namespace seoul {

namespace {

// One removal, self-owned: registered with the remover (which insists that a
// task's observer be a registered observer), attached to exactly this task,
// and gone once the task reports back. If the profile goes away with the task
// still running, the remover never reports and this object is simply never
// reached again.
class ForgetSiteJob final : public content::BrowsingDataRemover::Observer {
 public:
  ForgetSiteJob(content::BrowsingDataRemover* remover,
                base::WeakPtr<content::WebContents> web_contents,
                const GURL& site_url,
                std::string identity_scope,
                base::OnceClosure done)
      : remover_(remover),
        web_contents_(std::move(web_contents)),
        site_url_(site_url),
        identity_scope_(std::move(identity_scope)),
        done_(std::move(done)) {
    remover_->AddObserver(this);
  }
  ForgetSiteJob(const ForgetSiteJob&) = delete;
  ForgetSiteJob& operator=(const ForgetSiteJob&) = delete;
  ~ForgetSiteJob() override { remover_->RemoveObserver(this); }

  void Start(std::unique_ptr<content::BrowsingDataFilterBuilder> filter) {
    // Everything a site can store that the remover can scope to one site.
    constexpr uint64_t kSiteData =
        (chrome_browsing_data_remover::DATA_TYPE_SITE_DATA |
         content::BrowsingDataRemover::DATA_TYPE_CACHE) &
        chrome_browsing_data_remover::FILTERABLE_DATA_TYPES;
    remover_->RemoveWithFilterAndReply(
        base::Time::Min(), base::Time::Max(), kSiteData,
        content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB |
            content::BrowsingDataRemover::ORIGIN_TYPE_PROTECTED_WEB,
        std::move(filter), this);
  }

  // content::BrowsingDataRemover::Observer:
  void OnBrowsingDataRemoverDone(uint64_t failed_data_types) override {
    if (content::WebContents* contents = web_contents_.get()) {
      content::BrowserContext* context = contents->GetBrowserContext();
      Profile* profile = Profile::FromBrowserContext(context);
      adblock::AdBlockService* service =
          profile ? adblock::AdBlockServiceFactory::GetForProfile(profile)
                  : nullptr;
      // The site's data is gone; now its fingerprint of this machine stops
      // matching too, and the page comes back as a first visit.
      if (service) {
        service->RotateIdentity(site_url_, identity_scope_);
      }
      contents->GetController().Reload(content::ReloadType::BYPASSING_CACHE,
                                       /*check_for_repost=*/false);
    }
    std::move(done_).Run();
    delete this;
  }

 private:
  const raw_ptr<content::BrowsingDataRemover> remover_;
  const base::WeakPtr<content::WebContents> web_contents_;
  const GURL site_url_;
  const std::string identity_scope_;
  base::OnceClosure done_;
};

}  // namespace

bool ForgetSite(content::WebContents* web_contents, base::OnceClosure done) {
  if (!web_contents) {
    return false;
  }
  const GURL site_url = web_contents->GetLastCommittedURL();
  if (!site_url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  content::BrowserContext* context = web_contents->GetBrowserContext();
  content::BrowsingDataRemover* remover =
      context ? context->GetBrowsingDataRemover() : nullptr;
  if (!remover) {
    return false;
  }
  std::string domain = net::registry_controlled_domains::GetDomainAndRegistry(
      site_url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  if (domain.empty()) {
    // An IP address or an internal hostname.
    domain = std::string(site_url.host());
  }
  std::unique_ptr<content::BrowsingDataFilterBuilder> filter =
      content::BrowsingDataFilterBuilder::Create(
          content::BrowsingDataFilterBuilder::Mode::kDelete);
  filter->AddRegisterableDomain(domain);
  // A tab in an isolated Space keeps its storage in that Space's own
  // partition. Without this the remover falls back to the profile's default
  // partition and deletes nothing the site actually wrote, while the panel
  // reports the site forgotten - the one outcome a privacy control must never
  // produce.
  content::RenderFrameHost* const frame = web_contents->GetPrimaryMainFrame();
  content::StoragePartition* const partition =
      frame ? frame->GetStoragePartition() : nullptr;
  if (partition && !partition->GetConfig().is_default()) {
    filter->SetStoragePartitionConfig(partition->GetConfig());
  }
  auto* job = new ForgetSiteJob(
      remover, web_contents->GetWeakPtr(), site_url,
      adblock::AdBlockService::IdentityScopeFor(web_contents), std::move(done));
  job->Start(std::move(filter));
  return true;
}

}  // namespace seoul
