// Project Seoul product runtime: the Context Thread service.
// Production owner of context threads: create, rename, archive, reopen,
// delete, and typed attach/detach operations. Nothing is captured silently:
// every item is added by an explicit user action carried through a typed
// call, and the underlying thread model rejects sensitive classes outright.
//
// STATE OWNERSHIP
//   owner:        one ThreadService per profile runtime.
//   lifetime:     the profile runtime.
//   persistence:  serialized through TakePersistedState()/Restore (the
//                 runtime service owns the pref write).
//   recovery:     corrupt entries are skipped on restore, never guessed at.
//   teardown:     dropped with the runtime.
//   bounds:       kMaxThreads threads, model-enforced items per thread.
//   isolation:    per profile.

#ifndef SEOUL_BROWSER_PRODUCT_THREAD_SERVICE_H_
#define SEOUL_BROWSER_PRODUCT_THREAD_SERVICE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/values.h"
#include "seoul/browser/context/context_thread.h"

namespace seoul {

inline constexpr size_t kMaxThreads = 100;

struct ThreadSummary {
  ThreadSummary();
  ThreadSummary(const ThreadSummary&);
  ThreadSummary(ThreadSummary&&);
  ThreadSummary& operator=(const ThreadSummary&);
  ThreadSummary& operator=(ThreadSummary&&);
  ~ThreadSummary();

  std::string id;
  std::string name;
  std::string workspace_id;
  bool archived = false;
  size_t item_count = 0;
};

class ThreadService {
 public:
  explicit ThreadService(base::RepeatingCallback<base::Time()> clock,
                         base::RepeatingClosure changed = {});
  ThreadService(const ThreadService&) = delete;
  ThreadService& operator=(const ThreadService&) = delete;
  ~ThreadService();

  // Returns the new thread id, or empty at the bound / invalid name.
  std::string CreateThread(const std::string& name,
                           const std::string& workspace_id = std::string());
  bool RenameThread(const std::string& thread_id, const std::string& name);
  bool ArchiveThread(const std::string& thread_id);
  bool ReopenThread(const std::string& thread_id);
  bool DeleteThread(const std::string& thread_id);

  // Typed attachment. The item's kind-specific validation (and the sensitive
  // rejection) happens in the thread model. Returns the item id.
  ContextResult<std::string> AttachItem(const std::string& thread_id,
                                        ContextItem item);
  bool DetachItem(const std::string& thread_id, const std::string& item_id);

  const ContextThread* FindThread(const std::string& thread_id) const;
  std::vector<ThreadSummary> Summaries() const;

  base::DictValue TakePersistedState() const;
  void RestorePersistedState(const base::DictValue& state);

  size_t size() const { return threads_.size(); }

 private:
  base::RepeatingCallback<base::Time()> clock_;
  base::RepeatingClosure changed_;
  std::map<std::string, std::unique_ptr<ContextThread>> threads_;
  std::map<std::string, std::string> thread_workspaces_;
  uint64_t next_id_ = 1;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_PRODUCT_THREAD_SERVICE_H_
