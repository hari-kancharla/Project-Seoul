// Project Seoul Library, Boards, and Live Collections.

#ifndef SEOUL_BROWSER_LIBRARY_LIBRARY_SERVICE_H_
#define SEOUL_BROWSER_LIBRARY_LIBRARY_SERVICE_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/values.h"
#include "seoul/browser/library/library_types.h"

namespace seoul {

class LibraryServiceObserver : public base::CheckedObserver {
 public:
  ~LibraryServiceObserver() override = default;
  virtual void OnLibraryChanged(uint64_t revision) = 0;
};

class LibraryService {
 public:
  using Clock = base::RepeatingCallback<base::Time()>;

  explicit LibraryService(
      Clock clock,
      base::RepeatingClosure changed = base::RepeatingClosure());
  LibraryService(const LibraryService&) = delete;
  LibraryService& operator=(const LibraryService&) = delete;
  ~LibraryService();

  LibraryResult<BoardId> CreateBoard(const std::string& name);
  LibraryStatusResult RenameBoard(const BoardId& id, const std::string& name);
  LibraryStatusResult SetBoardArchived(const BoardId& id, bool archived);
  LibraryStatusResult DeleteBoard(const BoardId& id);
  LibraryResult<BoardElementId> AddBoardElement(const BoardId& board_id,
                                                BoardElement element);
  LibraryStatusResult UpdateBoardElement(const BoardId& board_id,
                                         BoardElement element);
  LibraryStatusResult RemoveBoardElement(const BoardId& board_id,
                                         const BoardElementId& element_id);

  LibraryResult<LibraryArtifactId> AddArtifact(LibraryArtifact artifact);
  LibraryStatusResult RemoveArtifact(const LibraryArtifactId& id);

  LibraryResult<LiveCollectionId> CreateLiveCollection(
      LiveCollectionDefinition definition);
  LibraryStatusResult UpdateLiveCollection(LiveCollectionDefinition definition);
  LibraryStatusResult DeleteLiveCollection(const LiveCollectionId& id);
  // Starts a refresh and returns its monotonic generation token. Completion
  // accepts only the current token, so late network responses cannot replace a
  // newer result.
  LibraryResult<uint64_t> BeginLiveRefresh(const LiveCollectionId& id);
  // An error preserves the last good items and records a truthful error state.
  // A success validates every item before replacing the collection atomically.
  LibraryStatusResult CompleteLiveRefresh(
      const LiveCollectionId& id,
      uint64_t generation,
      std::vector<LiveCollectionItem> items,
      const std::optional<std::string>& error);
  bool IsRefreshDue(const LiveCollectionId& id, base::Time now) const;

  const BoardRecord* FindBoard(const BoardId& id) const;
  const LibraryArtifact* FindArtifact(const LibraryArtifactId& id) const;
  const LiveCollectionRecord* FindLiveCollection(
      const LiveCollectionId& id) const;
  std::vector<BoardId> Boards() const;
  std::vector<LibraryArtifactId> Artifacts() const;
  std::vector<LiveCollectionId> LiveCollections() const;

  base::DictValue TakePersistedState() const;
  void RestorePersistedState(const base::DictValue& state);

  void AddObserver(LibraryServiceObserver* observer);
  void RemoveObserver(LibraryServiceObserver* observer);

  size_t board_count() const { return boards_.size(); }
  size_t artifact_count() const { return artifacts_.size(); }
  size_t live_collection_count() const { return live_collections_.size(); }
  // Monotonic within this profile runtime. Canvas includes it in snapshots so
  // an older Mojo reply can never replace state from a newer mutation.
  uint64_t revision() const { return revision_; }

 private:
  base::Time Now() const;
  void NotifyChanged();
  LibraryStatusResult ValidateBoardElement(const BoardElement& element) const;
  LibraryStatusResult ValidateArtifact(const LibraryArtifact& artifact) const;
  LibraryStatusResult ValidateLiveCollection(
      const LiveCollectionDefinition& definition) const;
  LibraryStatusResult ValidateLiveItem(const LiveCollectionItem& item) const;

  Clock clock_;
  base::RepeatingClosure changed_;
  std::map<BoardId, BoardRecord> boards_;
  std::map<LibraryArtifactId, LibraryArtifact> artifacts_;
  std::map<LiveCollectionId, LiveCollectionRecord> live_collections_;
  base::ObserverList<LibraryServiceObserver> observers_;
  uint64_t revision_ = 0;
};

}  // namespace seoul

#endif  // SEOUL_BROWSER_LIBRARY_LIBRARY_SERVICE_H_
