# Downloads: next-pass research notes

Status: preliminary source audit, not an implemented redesign or visual approval. Finish the native Settings review before replacing another primary browser surface.

The current footer callback in `native/seoul/browser/shell/shell_service.cc` calls `chrome::ShowDownloads`. In the pinned Chromium checkout, that function hides the transient download bubble and opens `chrome://downloads`. Patch 0048 changes the page's styling, but retains that page structure and entry behavior. The user's complaint about this still feeling like Chromium has a concrete architectural cause.

Arc's macOS documentation distinguishes a full Library containing completed and in-progress downloads from quick access to recent files at its sidebar icon. It also describes opening and dragging recent files. These are useful task distinctions. The article does not establish exact current pixel geometry, and its references to Notes are historical product context rather than a feature requirement for Seoul. [Arc Library documentation](https://resources.arc.net/hc/en-us/articles/19230634389911-Library-A-home-for-your-downloads-archived-tabs-easels-and-more), checked September 9, 2026.

Zen's current documentation search located its Downloads shortcut but did not establish a dedicated Downloads layout reference. Do not claim a visually inspected Zen download design from that evidence. [Zen keyboard shortcuts](https://docs.zen-browser.app/user-manual/shortcuts), checked September 9, 2026.

## Engineering boundary

Chromium's `chrome/browser/download/download_ui_model.h` already owns download presentation state, progress/status messages, interruption details, and danger patterns. Its warning states are explicitly meant to remain consistent between the bubble and full page. A Seoul surface must use these models and commands instead of reducing every item to a filename and progress percentage.

The next implementation investigation should inspect `DownloadItemModel`, `DownloadCommands`, the bubble controller and row view, and profile/OTR ownership. Required journeys include active transfer, unknown total size, pause/resume, interruption/retry, cancellation, completed/open, reveal in Finder, removed file, and blocked/dangerous download. Commands must be enabled from actual model state; warning actions cannot silently bypass the existing decision path.

The additional pinned-source audit confirms that `DownloadCommands` exposes visibility, enabled state, checked state, and execution separately. `DownloadBubbleUIController` is owned per browser window and has a weak-pointer interface. Its main view is a recent-download collection (finished within the last 24 hours), while its partial view represents in-progress and uninteracted downloads. Neither should be relabeled as complete history. Its action handler also owns warning and retry behavior, so custom buttons require that lifecycle and command path rather than direct state mutation. The native row view and regular/private-profile selection still need investigation.

## Design direction to validate

Use a small anchored recent-downloads surface for immediate access and a distinct searchable full history for retrieval. Keep the file name, origin, progress or failure, and next useful action readable. Preserve keyboard access even if hover offers a shortcut. Do not trigger file opening from hover. Distinguish removing a history entry from deleting the file.

Before implementation: inspect current official visual references or installed reference browsers; map every state to its actual backend; settle the relationship with Seoul's existing Library. After implementation: test real local-server downloads, interruption and restart, private/regular profiles, policy/warning flows, narrow/light/dark layouts, keyboard navigation, and native file interactions. This note alone does not satisfy that research or acceptance gate.
