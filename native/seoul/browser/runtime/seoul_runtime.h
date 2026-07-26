// Project Seoul runtime composition.
//
// STATE OWNERSHIP (product-wide state rules):
//   owner:        one SeoulRuntime per regular profile, owned by the
//                 profile-keyed SeoulRuntimeService.
//   lifetime:     the profile.
//   persistence:  per-profile ephemeral for the live registries; durable
//                 product state (scenes, workflows, organization) lives in its
//                 own stores and is referenced by id, not duplicated here.
//   recovery:     registries are rebuilt on construction; connectors re-import
//                 their capabilities on connect.
//   teardown:     Shutdown() disconnects connectors (removing their
//                 capabilities) and drops the registries in a defined order.
//   bounds:       registries are individually bounded (see their limits).
//   isolation:    per profile; never process-global.
//
// This is the single composition point that gives every product subsystem a
// production owner: the capability graph, the connector registry (which mirrors
// connector tools into the capability graph), the scene registry, and the
// reasoning-router policy. The task executor, the Canvas, and the shell reach
// these through the runtime rather than through free globals.

#ifndef SEOUL_BROWSER_RUNTIME_SEOUL_RUNTIME_H_
#define SEOUL_BROWSER_RUNTIME_SEOUL_RUNTIME_H_

#include <memory>

#include "seoul/browser/connectors/connector_registry.h"
#include "seoul/browser/intelligence/reasoning_router.h"
#include "seoul/browser/scenes/scene_registry.h"
#include "seoul/browser/tools/tool_registry.h"

namespace seoul {

class SeoulRuntime {
public:
  // `scene_resolvers` bridge the scene registry to the organization/theme/
  // site-layer stores by id (the runtime does not own those stores).
  explicit SeoulRuntime(SceneResolvers scene_resolvers);
  SeoulRuntime(const SeoulRuntime &) = delete;
  SeoulRuntime &operator=(const SeoulRuntime &) = delete;
  ~SeoulRuntime();

  ToolRegistry &capabilities() { return capabilities_; }
  ConnectorRegistry &connectors() { return connectors_; }
  const ConnectorRegistry &connectors() const { return connectors_; }
  SceneRegistry &scenes() { return scenes_; }
  const SceneRegistry &scenes() const { return scenes_; }

  // The reasoning policy for a task, derived from the runtime's provider
  // availability and the caller's budget input.
  RoutingPolicy MakeRoutingPolicy(bool cloud_enabled, bool local_available,
                                  int64_t remaining_budget_microdollars) const;

  // Disconnects connectors and clears live registrations in a defined order.
  void Shutdown();

private:
  // Construction-only: callers cannot accidentally perform a second,
  // partially ignored registration pass.
  void RegisterBuiltinCapabilities();

  ToolRegistry capabilities_;
  ConnectorRegistry connectors_;
  SceneRegistry scenes_;
  bool shutting_down_ = false;
};

} // namespace seoul

#endif // SEOUL_BROWSER_RUNTIME_SEOUL_RUNTIME_H_
