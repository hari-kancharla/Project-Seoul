// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/product/browser/space_partition_resolver.h"

#include <string>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/sessions/core/session_id.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition_config.h"
#include "seoul/browser/containers/space_container.h"
#include "seoul/browser/lifecycle/lifecycle_identity.h"
#include "seoul/browser/organization/organization_model.h"
#include "seoul/browser/organization/seoul_organization_service.h"
#include "seoul/browser/organization/seoul_organization_service_factory.h"
#include "url/gurl.h"

namespace seoul {

scoped_refptr<content::SiteInstance> SiteInstanceForNewTabInActiveSpace(
    BrowserWindowInterface* browser,
    const GURL& url) {
  if (!browser) {
    return nullptr;
  }
  Profile* const profile = browser->GetProfile();
  // Incognito and guest are already isolated by Chromium, and layering a second
  // partition inside them would be a way to get this wrong rather than a way to
  // add protection.
  if (!profile || !profile->IsRegularProfile()) {
    return nullptr;
  }

  SeoulOrganizationService* const service =
      SeoulOrganizationServiceFactory::GetForProfile(profile);
  if (!service) {
    return nullptr;
  }

  const LiveWindowKey window =
      LiveWindowKey::FromSessionId(browser->GetSessionID().id());
  if (!window.is_valid()) {
    return nullptr;
  }

  const OrganizationModel& model = service->model();
  const WorkspaceId active = model.ActiveWorkspaceForWindow(window.value());
  if (!active.is_valid()) {
    return nullptr;
  }

  const WorkspaceRecord* const record = model.FindWorkspace(active);
  if (!record || !record->isolated) {
    return nullptr;
  }

  const std::string partition_name =
      containers::PartitionNameForWorkspace(active.value());
  if (partition_name.empty()) {
    // The id cannot form a safe partition name. Returning null keeps the tab in
    // the default partition, which is the honest outcome: it is not isolated,
    // and pretending otherwise by inventing a name would put two Spaces in one
    // container.
    return nullptr;
  }

  const content::StoragePartitionConfig config =
      content::StoragePartitionConfig::Create(
          profile, containers::kPartitionDomain, partition_name,
          /*in_memory=*/false);
  return content::SiteInstance::CreateForFixedStoragePartition(profile, url,
                                                               config);
}

}  // namespace seoul
