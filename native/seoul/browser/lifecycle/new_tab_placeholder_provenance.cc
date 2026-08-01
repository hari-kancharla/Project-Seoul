// Project Seoul explicit synthetic new-tab placeholder provenance.

#include "seoul/browser/lifecycle/new_tab_placeholder_provenance.h"

#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"

namespace seoul {

namespace {

class SyntheticNewTabPlaceholderMarker
    : public content::WebContentsUserData<SyntheticNewTabPlaceholderMarker> {
 public:
  SyntheticNewTabPlaceholderMarker(const SyntheticNewTabPlaceholderMarker&) =
      delete;
  SyntheticNewTabPlaceholderMarker& operator=(
      const SyntheticNewTabPlaceholderMarker&) = delete;
  ~SyntheticNewTabPlaceholderMarker() override = default;

 private:
  explicit SyntheticNewTabPlaceholderMarker(content::WebContents* contents)
      : content::WebContentsUserData<SyntheticNewTabPlaceholderMarker>(
            *contents) {}

  friend class content::WebContentsUserData<SyntheticNewTabPlaceholderMarker>;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(SyntheticNewTabPlaceholderMarker);

}  // namespace

void MarkSyntheticNewTabPlaceholder(content::WebContents* contents) {
  if (contents) {
    SyntheticNewTabPlaceholderMarker::CreateForWebContents(contents);
  }
}

bool HasSyntheticNewTabPlaceholderProvenance(
    const content::WebContents* contents) {
  return contents &&
         SyntheticNewTabPlaceholderMarker::FromWebContents(contents);
}

}  // namespace seoul
