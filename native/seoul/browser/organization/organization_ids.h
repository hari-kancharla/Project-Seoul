// Project Seoul native organization engine.
// Strongly-typed identifiers for organization objects. Identities are UUIDs
// (base::Uuid) wrapped in distinct types so a WorkspaceId can never be passed
// where an EssentialId is expected. Identifiers are stable for the lifetime of
// the object and are never derived from user-visible names.

#ifndef SEOUL_BROWSER_ORGANIZATION_ORGANIZATION_IDS_H_
#define SEOUL_BROWSER_ORGANIZATION_ORGANIZATION_IDS_H_

#include <compare>
#include <string>
#include <string_view>

#include "base/uuid.h"

namespace seoul {

// Defines a strong id type over base::Uuid. Default-constructed ids are
// invalid. Ordering is by the underlying lowercase UUID string, giving
// deterministic iteration when these are used as std::map keys.
#define SEOUL_DEFINE_UUID_ID(TypeName)                                     \
  class TypeName {                                                         \
   public:                                                                 \
    TypeName() = default;                                                  \
    static TypeName GenerateNew() {                                        \
      TypeName id;                                                         \
      id.uuid_ = base::Uuid::GenerateRandomV4();                           \
      return id;                                                           \
    }                                                                      \
    static TypeName FromString(std::string_view s) {                       \
      TypeName id;                                                         \
      id.uuid_ = base::Uuid::ParseLowercase(s);                            \
      return id;                                                           \
    }                                                                      \
    bool is_valid() const {                                                \
      return uuid_.is_valid();                                             \
    }                                                                      \
    std::string value() const {                                            \
      return uuid_.is_valid() ? uuid_.AsLowercaseString() : std::string(); \
    }                                                                      \
    friend bool operator==(const TypeName& a, const TypeName& b) {         \
      return a.uuid_ == b.uuid_;                                           \
    }                                                                      \
    friend bool operator<(const TypeName& a, const TypeName& b) {          \
      return a.uuid_ < b.uuid_;                                            \
    }                                                                      \
                                                                           \
   private:                                                                \
    base::Uuid uuid_;                                                      \
  }

SEOUL_DEFINE_UUID_ID(WorkspaceId);
SEOUL_DEFINE_UUID_ID(EssentialId);
SEOUL_DEFINE_UUID_ID(TabMembershipId);
SEOUL_DEFINE_UUID_ID(FolderId);
SEOUL_DEFINE_UUID_ID(SplitGroupId);
SEOUL_DEFINE_UUID_ID(RoutingRuleId);

#undef SEOUL_DEFINE_UUID_ID

}  // namespace seoul

#endif  // SEOUL_BROWSER_ORGANIZATION_ORGANIZATION_IDS_H_
