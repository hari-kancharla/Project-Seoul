# Seoul Settings: research and replacement direction

Research date: September 8, 2026, America/New_York. Some source and verification timestamps are September 9 in UTC.

Implementation update: the [September 9 implementation record](seoul-settings-implementation-2026-09-09.md) tracks the subsequent native Settings work. The proposal and verification descriptions below remain the historical research baseline; they do not certify that later implementation.

**Decision: replace the primary Settings experience with a separate macOS Settings window.** The current implementation still presents Chromium's preferences architecture inside a browser tab. Renaming its heading, recoloring cards, and rearranging its category sidebar did not deliver the Arc-style panel requested. The September 8 screenshot is a valid rejection of that direction.

This document covers Settings, Profiles/Containers, and the command/creation controls visible in that screenshot. It does not certify the rest of Seoul, add new product scope, or claim the proposed interface has been integrated. The Settings replacement remains a proposal. The user subsequently prioritized existing feature reliability; the separately recorded native control repairs do not implement this Settings redesign.

## 1. What was checked

The review combined the supplied screenshots, the running disposable Seoul Verification application, current local source, Arc's official documentation and attached macOS screenshots, Zen's documentation and current public preferences source, and Apple's Settings guidance. Arc's reference images were opened and visually inspected; the conclusions below are not based only on article summaries.

The Arc references include historical images. The clearest Profiles screenshot is dated January 18, 2024 in its filename. It establishes the window structure but does not establish every pixel or feature in a current September 2026 Arc installation. An installed current Arc build was not inspected. Zen source was pinned to commit `fdf9588b7b1ed58d71254d8ee1a1c8b414728be5`, dated September 9, 2026 at 00:13 UTC, which is September 8 locally. Its development branch can differ from a released build. These limits matter when distinguishing a reference from a current compatibility claim.

## 2. What the references actually show

### Arc: a Settings window, not a second browser navigation system

Arc's official General screenshot shows a dedicated macOS window with traffic lights, a centered pane title, and a horizontal row of icon-and-label categories. The selected category has a restrained filled rectangle. The preference content begins beneath a divider. It is not surrounded by an address bar, browser tabs, or a second full-height category rail. [1](https://resources.arc.net/hc/article_attachments/23281349059351)

The official Profiles screenshot shows the same top navigation, then a short explanation and a two-column arrangement: a list headed “Your Profiles” on the left and settings for the selected profile on the right. Search and archive controls are compact form rows. Selection establishes which profile the controls affect. The hierarchy comes from alignment and spacing, rather than a stack of oversized cards. [2](https://resources.arc.net/hc/article_attachments/20678079725079)

Arc describes Profiles as separating authentication, passwords, autofill, history, cache/cookies, extensions, and several preferences. A profile can be assigned to multiple Spaces, and deleting a profile requires removing its Space assignments first. This is a stronger boundary than cookie containers. Seoul should explain its own boundary equally clearly, without assuming its organization model is identical to Arc's. [3](https://resources.arc.net/hc/en-us/articles/19227964556183-Profiles-Separate-Work-Personal-Browsing)

A separate official dark screenshot shows a small contextual selection menu beside the setting being edited. Its historical Notes integrations are not a feature recommendation for Seoul. The useful reference is the local placement and compact menu treatment. [4](https://resources.arc.net/hc/article_attachments/22557798819095)

### Zen: useful behavior and information architecture, a different Settings structure

Zen's Workspaces documentation distinguishes tab organization from container isolation: containers separate website sessions while some browser data, including history and extensions, remains shared. Its illustrated preferences screen retains a sidebar-based page. That is useful for understanding the container boundary but is not the Arc window structure requested here. [5](https://docs.zen-browser.app/user-manual/workspaces)

The pinned preferences source gives layout choices their own named controls and preview images. URL-bar behavior is a separate setting with normal, floating-on-type, and floating modes. Tab-management options live in another pane. These are semantic distinctions, not just a coat of color applied to one long list. [6](https://github.com/zen-browser/desktop/blob/fdf9588b7b1ed58d71254d8ee1a1c8b414728be5/src/browser/components/preferences/zenLooksAndFeel.inc.xhtml)[7](https://github.com/zen-browser/desktop/blob/fdf9588b7b1ed58d71254d8ee1a1c8b414728be5/src/browser/components/preferences/zenTabsManagement.inc.xhtml)

Zen's URL-bar documentation describes floating new-tab input over the current page. This can preserve context during navigation. It does not require an oval text field, a large “Actions” chip, or partially clipped command rows. Those are separate Seoul design decisions. [8](https://docs.zen-browser.app/user-manual/urlbar)

### Apple: the platform convention supports the requested change

Apple's macOS guidance calls for the app-menu Settings command to open a custom Settings window, with toolbar buttons for preference panes and a clear selected state. The window title identifies the current pane, and reopening should return to the last-viewed pane. More broadly, task-specific options belong near the task; Settings should hold less frequent configuration with sensible defaults. This supports both a separate Settings window and a small creation menu at the sidebar control. [9](https://developer.apple.com/design/human-interface-guidelines/settings)

## 3. Problems confirmed in Seoul

This table records the research baseline. The subsequent [reliability pass](seoul-reliability-verification-2026-09-08.md) repairs the address-field focus path, partial command rows, and footer creation menu. The dedicated Settings window and broader design acceptance remain outstanding.

| Observed problem | Evidence | Required correction |
| --- | --- | --- |
| Settings remains a browser page with Chromium's hierarchy | The current shell callback still calls `chrome::ShowSettings(browser)` in `native/seoul/browser/shell/shell_service.cc`; the supplied screenshot shows the result | Introduce a dedicated Settings window and route the app menu and shortcut to it |
| Oval focus outline over rectangular geometry | `LocationBarView` installed `InstallPillHighlightPathGenerator(this)` at the research baseline | Make the focus path agree with the actual field bounds and corner radius in every layout |
| An option is visibly cut off in the command surface | `seoul_command_launcher_view.cc` set the viewport to four and a half result heights at the research baseline | Display whole rows at rest; use a bounded scrollable list when required |
| Footer “+” opens a large general command surface | `SeoulShellFooterView::OnCreateNewPressed()` called `ShowCommandLauncher()` at the research baseline | Open a small anchored creation menu |
| Unrelated actions share a generic launch icon | The command view has a generic fallback for multiple catalog actions | Assign semantic icons consistently; do not use an external-link glyph for local creation |
| Layout terminology changes between surfaces | Catalog labels use single/multiple/collapsed toolbar terminology while Settings uses Sidebar/Top toolbar/Compact sidebar | Use one set of user-facing names across Settings, menus, commands, and accessibility labels |

The issue is structural as well as visual. A successful compile, working preference write, or absence of horizontal overflow does not demonstrate that the design is suitable. The previous Settings direction is superseded by this review.

## 4. Proposed Seoul structure

### Window and categories

Use a dedicated, modestly sized Settings window with a stable top category bar: **General, Appearance, Profiles, Shortcuts, Privacy, Advanced**. These six categories are a Seoul proposal, not a claim that Arc uses precisely this set. Exclude Arc's internal categories and product-specific integrations that Seoul does not offer.

`⌘,` should bring the existing Settings window forward rather than open duplicate tabs or windows. The window title follows the selected pane. Closing Settings must leave browser tabs intact. Reopening restores the last pane. The native macOS titlebar owns window controls and keyboard behavior; the interface preview only illustrates their placement.

General contains launch and everyday browser defaults. Appearance contains layout and display choices. Profiles contains identity selection and profile-scoped preferences. Shortcuts exposes actual supported bindings. Privacy contains global protection defaults, with site-specific exceptions still beside the address bar. Advanced groups infrequent compatibility options. The initial implemented inventory must include only controls with a real supported backend and accurate state; the preview is not authority to add every illustrated option.

Search is not the dominant header. First establish a small, coherent category inventory. If discovery testing shows a need for Settings search, it should navigate to and reveal the real setting with its current profile scope, rather than duplicate the entire Chromium settings tree.

### Profiles and Containers

Profiles use a persistent left list and a detail area headed by the selected profile's name. Search and download preferences belong to that selection. The implementation must never display one profile while writing to another browser context.

Container management is a clearly labeled section inside the selected profile. Its explanation states the boundary in ordinary language: separate website sign-ins and site storage, shared profile-level history, passwords, and extensions. A new container space starts with fresh storage. Renaming or moving a tab must not pretend to change the storage partition of an already-loaded document.

Seoul's existing model owns Spaces within a profile. Copying Arc's profile-assignment UI directly would imply cross-profile moves that require separate migration and lifecycle design. Keep the first revision aligned with Seoul's actual ownership model. Existing data must remain discoverable through the new window; it is not acceptable to create a second settings store for presentation convenience.

The preview uses fictional Default and Work profiles to demonstrate selection, preference scope, and container creation. Its data changes are local to the preview. It neither reads nor edits real browser profiles.

### Creation and command controls

The sidebar creation control opens three actions: **New tab, New space, New container space**. The menu is anchored to that control and contained within the window. Each action gets a meaningful icon. A naming dialog opens only after the menu has released mouse capture. Escape closes it and restores focus.

Command search remains a separate browsing tool. Its field, result rows, and outer surface use coordinated rounded rectangles. Remove the redundant “Actions” pill and the oversized dark selection banner. Use quiet sky-blue selection with readable text, complete rows, arrow-key selection, Enter to activate, and Escape to dismiss. Search filtering must retain a sensible active item and accessible announcement without flooding VoiceOver.

## 5. Visual and interaction specifications

The following values are starting specifications for Seoul, **not measurements taken from Arc**:

| Element | Starting specification | Reason |
| --- | --- | --- |
| Settings window | Approximately 780 × 620 points, adjusted for content | Keeps configuration separate and readable without dominating the desktop |
| Main content inset | 24–28 points | Gives stable alignment across panes |
| Profile list | Approximately 180 points wide | Holds a name and a short secondary line without wasting the detail area |
| Form rows | 36–44 points | Compact scanning with adequate target separation |
| Body and secondary text | System font, roughly 13–14 and 12 points | Fits macOS conventions and supports legible hierarchy |
| Field/button corners | 6–8 points | Clear rounded rectangles, consistent with the requested shape language |
| Window/command surface corners | 12–14 points | Distinguishes the outer surface without pill-shaped fields |
| Accent | Sky blue, neutral backgrounds | Matches the requested identity while preserving content contrast |
| Interaction transitions | Approximately 120–160 ms where useful | Immediate feedback; disable nonessential motion for Reduce Motion |

Use the same corner path for a field background, border, hover treatment, and focus indication. Give selected rows a light accent fill in light mode and an appropriately dark accent fill in dark mode. Keep separators subtle, shadows restrained, and text contrast independent of selection color. Native accessibility focus and platform control behavior take precedence over a decorative effect.

A narrow preview should reflow rather than shrink text or clip controls. The native desktop window needs its own minimum-size, localization, text-scaling, and multi-display checks; responsive HTML preview behavior does not prove native window correctness.

## 6. Implementation sequence and acceptance gates

1. **Window ownership and routing.** Add the dedicated Settings window, establish its profile binding, and route `⌘,`, app-menu Settings, and Seoul entry points consistently. Verify singleton behavior, close/reopen, last pane, profile teardown, and keyboard focus. Keep Chromium preference services authoritative.
2. **Profiles and Appearance first.** Implement those two panes against real profile metadata and preference handlers. Verify writes, persistence after restart, policy-controlled states, profile switching, and container boundaries. Do not invent successful OS default-browser or file-picker responses.
3. **Creation menu and command geometry.** Replace the footer launcher routing, coordinate dialog lifecycle, correct focus paths and icons, and stop resting on half rows. Verify at every browser layout and supported window size.
4. **Remaining pane migration.** Inventory current settings and map them to the new categories. Preserve access to advanced browser controls while their presentation is migrated; do not hide working features behind dead buttons or falsely label stock pages as completed custom panels.
5. **Native verification after the build finishes.** Run the relevant behavioral suites against one settled build. Include patch overlap and apply/reverse integrity checks. Then inspect the actual macOS application in light/dark modes, with keyboard and VoiceOver, long labels, text scaling, and multiple profiles. Preserve repeatable evidence.

Acceptance requires both behavior and visual review. In particular: no second browser rail inside Settings; no unintended pill focus path; no clipped resting command row; no duplicate Settings windows; no profile label/data mismatch; no simulated container isolation; no silent preference failures; no crash when a menu opens a dialog; and no loss of existing settings access. No finite test suite proves the absence of every browser defect, so the final report must identify tested flows and remaining limits precisely.

## 7. Proposal verification and scope change

The interactive proposals passed 113 checks covering category navigation, profile-specific state, local container creation, command filtering, Enter/Escape behavior, theme selection, and horizontal overflow at 736 and 360 pixels in light and dark appearances. No JavaScript runtime errors were observed. Generated screenshots were visually reviewed for Profiles and the command/creation controls; additional Appearance and container-form screenshots are retained for inspection. Evidence is under `native/evidence/2026-09-08/settings-design-review/`. These are preview checks, not native browser test results.

The user subsequently redirected active work to reliability of existing features: tab/menu selection, handset behavior and catalog currency, Boost, ad blocking, and memory use. This proposal is retained as research; implementation of the Settings redesign is deferred behind that reliability work. No production UI code was changed for this proposal.

## 8. Sources

All links below were used for the findings above. Image links point directly to official reference images that were visually inspected.

1. Arc, [Update Arc for Desktop](https://resources.arc.net/hc/en-us/articles/21489650267031-Update-Arc-for-Desktop), [General Settings reference image](https://resources.arc.net/hc/article_attachments/23281349059351).
2. Arc, [Auto Archive: Clean as you go](https://resources.arc.net/hc/en-us/articles/19228855311127-Auto-Archive-Clean-as-you-go), [Profiles Settings reference image](https://resources.arc.net/hc/article_attachments/20678079725079). Historical screenshot dated January 18, 2024.
3. Arc, [Profiles: Separate Work & Personal Browsing](https://resources.arc.net/hc/en-us/articles/19227964556183-Profiles-Separate-Work-Personal-Browsing).
4. Arc, [Customize Default Notes App](https://resources.arc.net/hc/en-us/articles/22557798824855-Customize-Default-Notes-App), [dark contextual menu reference](https://resources.arc.net/hc/article_attachments/22557798819095). Historical interaction reference only.
5. Zen, [Workspaces](https://docs.zen-browser.app/user-manual/workspaces), [container preferences reference image](https://docs.zen-browser.app/_next/static/media/enable-container-tabs.86d192e6.png).
6. Zen, [Look and Feel preferences at pinned commit](https://github.com/zen-browser/desktop/blob/fdf9588b7b1ed58d71254d8ee1a1c8b414728be5/src/browser/components/preferences/zenLooksAndFeel.inc.xhtml).
7. Zen, [Tab Management preferences at pinned commit](https://github.com/zen-browser/desktop/blob/fdf9588b7b1ed58d71254d8ee1a1c8b414728be5/src/browser/components/preferences/zenTabsManagement.inc.xhtml).
8. Zen, [URL Bar & Search Functions](https://docs.zen-browser.app/user-manual/urlbar).
9. Apple, [Human Interface Guidelines: Settings](https://developer.apple.com/design/human-interface-guidelines/settings). The live page content was read through the browser because the text-fetch tool returned only its JavaScript shell.
