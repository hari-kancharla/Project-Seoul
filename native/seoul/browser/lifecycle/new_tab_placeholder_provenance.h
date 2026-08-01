// Project Seoul explicit synthetic new-tab placeholder provenance.

#ifndef SEOUL_BROWSER_LIFECYCLE_NEW_TAB_PLACEHOLDER_PROVENANCE_H_
#define SEOUL_BROWSER_LIFECYCLE_NEW_TAB_PLACEHOLDER_PROVENANCE_H_

namespace content {
class WebContents;
}

namespace seoul {

// Marks only the browser-created, default startup NTP that Seoul may use as
// inert chrome behind its floating omnibox. The marker lives on WebContents;
// Seoul's session metadata bridge persists one explicit provenance bit so the
// same placeholder cannot return as an ordinary visible about:blank tab.
void MarkSyntheticNewTabPlaceholder(content::WebContents* contents);

// Restored, command-line, preference, and user-created NTPs return false even
// when their URL and history happen to match the synthetic placeholder.
bool HasSyntheticNewTabPlaceholderProvenance(
    const content::WebContents* contents);

}  // namespace seoul

#endif  // SEOUL_BROWSER_LIFECYCLE_NEW_TAB_PLACEHOLDER_PROVENANCE_H_
