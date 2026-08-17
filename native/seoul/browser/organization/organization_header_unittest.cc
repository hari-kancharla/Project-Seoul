// Header self-containment regression: organization_types.h must pull in limits.
#include "seoul/browser/organization/organization_types.h"

namespace seoul {
static_assert(kOrganizationSchemaVersion == 2, "schema constant visible");
static_assert(kOrganizationSchemaVersionWithoutFolders <
                  kOrganizationSchemaVersion,
              "the pre-folders version must remain readable, not equal");
}  // namespace seoul
