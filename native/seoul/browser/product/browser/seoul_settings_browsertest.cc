// Copyright 2026 The Project Seoul Authors
// Use of this source code is governed by the MPL-2.0 licence.

#include "seoul/browser/product/browser/seoul_settings_window.h"

#include <array>

#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_view_prefs.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/location_bar/location_icon_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view_base.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/mock_configuration_policy_provider.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/core/common/policy_types.h"
#include "components/policy/policy_constants.h"
#include "components/prefs/pref_service.h"
#include "components/security_state/core/security_state.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "seoul/browser/adblock/ad_block_service.h"
#include "seoul/browser/adblock/ad_block_service_factory.h"
#include "seoul/browser/adblock/ad_block_settings.h"
#include "seoul/browser/organization/organization_model.h"
#include "seoul/browser/organization/seoul_organization_service.h"
#include "seoul/browser/organization/seoul_organization_service_factory.h"
#include "seoul/browser/product/browser/seoul_runtime_service.h"
#include "seoul/browser/product/browser/seoul_runtime_service_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/test/event_generator.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image.h"
#include "ui/snapshot/snapshot.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_utils.h"
#include "ui/views/window/dialog_delegate.h"

namespace seoul {
namespace {

template <class T>
T* FindNamed(views::View* root, const std::u16string& name) {
  if (auto* typed = views::AsViewClass<T>(root);
      typed && root->GetViewAccessibility().GetCachedName() == name)
    return typed;
  for (const auto& child : root->children()) {
    if (auto* found = FindNamed<T>(child.get(), name))
      return found;
  }
  return nullptr;
}

class SeoulSettingsBrowserTest : public InProcessBrowserTest {
 protected:
  void TearDownOnMainThread() override {
    if (auto* widget = GetSeoulSettingsWidgetForTesting())
      widget->Close();
    base::RunLoop().RunUntilIdle();
    InProcessBrowserTest::TearDownOnMainThread();
  }
  views::View* Root() {
    auto* widget = GetSeoulSettingsWidgetForTesting();
    return widget ? widget->GetRootView() : nullptr;
  }
  template <class T>
  T* Find(const std::u16string& name) {
    return Root() ? FindNamed<T>(Root(), name) : nullptr;
  }
  void Click(const std::u16string& name) {
    const std::array<std::u16string, 6> panes = {u"General",  u"Appearance",
                                                 u"Profiles", u"Shortcuts",
                                                 u"Privacy",  u"Advanced"};
    for (size_t i = 0; i < panes.size(); ++i) {
      if (name == panes[i]) {
        ASSERT_TRUE(
            ActivateSeoulSettingsPaneForTesting(static_cast<SettingsPane>(i)));
        return;
      }
    }
    auto* button = Find<views::Button>(name);
    ASSERT_TRUE(button) << base::UTF16ToUTF8(name);
    ASSERT_TRUE(button->GetEnabled());
    views::test::ButtonTestApi(button).NotifyDefaultMouseClick();
  }
  void Select(const std::u16string& name, size_t index) {
    auto* choice = Find<views::Combobox>(name);
    ASSERT_TRUE(choice);
    ASSERT_TRUE(choice->GetEnabled());
    choice->MenuSelectionAt(index);
  }
  views::Widget* NameDialog(const std::u16string& title) {
    views::Widget* result = nullptr;
    const bool found = base::test::RunUntil([&] {
      for (views::Widget* widget : views::test::WidgetTest::GetAllWidgets()) {
        if (widget->IsVisible() && widget->widget_delegate() &&
            widget->widget_delegate()->GetWindowTitle() == title) {
          result = widget;
          return true;
        }
      }
      return false;
    });
    return found ? result : nullptr;
  }
  void AcceptName(views::Widget* dialog, const std::u16string& name) {
    ASSERT_TRUE(dialog);
    views::Textfield* field = nullptr;
    ASSERT_TRUE(base::test::RunUntil([&] {
      field = views::AsViewClass<views::Textfield>(
          dialog->GetFocusManager()->GetFocusedView());
      return field != nullptr;
    }));
    field->SelectAll(false);
    field->InsertOrReplaceText(name);
    dialog->widget_delegate()->AsDialogDelegate()->AcceptDialog();
    base::RunLoop().RunUntilIdle();
  }
};

IN_PROC_BROWSER_TEST_F(SeoulSettingsBrowserTest,
                       OneSiteControlsEntryAcrossEveryBrowserLayout) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/empty.html")));
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* prefs = browser()->profile()->GetPrefs();
  for (int layout : {0, 1, 2}) {
    prefs->SetInteger(kSeoulLayoutModePref, layout);
    ASSERT_TRUE(base::test::RunUntil([&] {
      browser_view->GetWidget()->LayoutRootViewIfNecessary();
      return static_cast<int>(browser_view->seoul_layout_mode()) == layout;
    }));
    auto* entry = FindNamed<views::Button>(browser_view, u"Site controls");
    ASSERT_TRUE(entry);
    EXPECT_TRUE(entry->IsDrawn());
    EXPECT_GT(entry->width(), 0);
    EXPECT_GT(entry->height(), 0);
    EXPECT_TRUE(entry->parent()->GetLocalBounds().Contains(entry->bounds()));
    for (const auto* removed :
         {u"Boost this site", u"Show this site as a phone",
          u"Shields for this site"}) {
      EXPECT_FALSE(FindNamed<views::Button>(browser_view, removed));
    }
  }
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  auto* entry = FindNamed<views::Button>(browser_view, u"Site controls");
  ASSERT_TRUE(entry);
  EXPECT_FALSE(entry->GetVisible());
}

IN_PROC_BROWSER_TEST_F(SeoulSettingsBrowserTest,
                       SiteInformationRemainsAccessibleWithoutDuplicateIcons) {
  net::EmbeddedTestServer https_server(net::EmbeddedTestServer::TYPE_HTTPS);
  https_server.ServeFilesFromSourceDirectory("chrome/test/data");
  ASSERT_TRUE(https_server.Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           https_server.GetURL("/empty.html")));
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* bar = browser_view->GetLocationBarView();
  ASSERT_EQ(bar->GetLocationBarModel()->GetSecurityLevel(),
            security_state::SECURE);
  for (int layout : {0, 1, 2}) {
    browser()->profile()->GetPrefs()->SetInteger(kSeoulLayoutModePref, layout);
    ASSERT_TRUE(base::test::RunUntil([&] {
      browser_view->GetWidget()->LayoutRootViewIfNecessary();
      return static_cast<int>(browser_view->seoul_layout_mode()) == layout;
    }));
    EXPECT_FALSE(bar->location_icon_view()->IsDrawn());
    auto* entry = FindNamed<views::Button>(browser_view, u"Site controls");
    ASSERT_TRUE(entry);
    ASSERT_TRUE(entry->IsDrawn());
  }
  auto* entry = FindNamed<views::Button>(browser_view, u"Site controls");
  ui::test::EventGenerator events(
      views::GetRootWindow(browser_view->GetWidget()));
  events.MoveMouseTo(entry->GetBoundsInScreen().CenterPoint());
  events.ClickLeftButton();
  for (int i = 0; i < 4; ++i)
    events.PressAndReleaseKey(ui::VKEY_DOWN, 0);
  events.PressAndReleaseKey(ui::VKEY_RETURN, 0);
  ASSERT_TRUE(base::test::RunUntil([] {
    return PageInfoBubbleViewBase::GetShownBubbleType() ==
           PageInfoBubbleViewBase::BUBBLE_PAGE_INFO;
  }));
  PageInfoBubbleViewBase::GetPageInfoBubbleForTesting()->GetWidget()->Close();
  base::RunLoop().RunUntilIdle();

  // An insecure page retains its leading identity/security entry. Only the
  // redundant ordinary secure-page icon is consolidated into the menu.
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/empty.html")));
  browser_view->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_NE(bar->GetLocationBarModel()->GetSecurityLevel(),
            security_state::SECURE);
  EXPECT_TRUE(bar->location_icon_view()->IsDrawn());
}

IN_PROC_BROWSER_TEST_F(SeoulSettingsBrowserTest,
                       PreferenceMenusObserveExternalChangesWithoutRecreation) {
  ASSERT_TRUE(
      ShowSeoulSettings(browser()->profile(), SettingsPane::kAppearance));
  auto* prefs = browser()->profile()->GetPrefs();
  auto* layout = Find<views::Combobox>(u"Browser layout");
  ASSERT_TRUE(layout);
  Select(u"Browser layout", 1);
  EXPECT_EQ(prefs->GetInteger(kSeoulLayoutModePref), 1);
  prefs->SetInteger(kSeoulLayoutModePref, 2);
  EXPECT_EQ(Find<views::Combobox>(u"Browser layout"), layout);
  EXPECT_EQ(layout->GetSelectedIndex(), 2u);
  prefs->SetInteger(kSeoulLayoutModePref, 0);
  EXPECT_EQ(layout->GetSelectedIndex(), 0u);
}

IN_PROC_BROWSER_TEST_F(SeoulSettingsBrowserTest,
                       NativeToolbarActionsOpenTheirRealPreferencePanes) {
  ASSERT_TRUE(
      ShowSeoulSettings(browser()->profile(), SettingsPane::kAppearance));
  Click(u"Profiles");
  EXPECT_TRUE(Find<views::ToggleButton>(u"Ask where to save each file"));
  Click(u"Advanced");
  EXPECT_TRUE(Find<views::ToggleButton>(u"Enable saved Boosts"));
  Click(u"General");
  EXPECT_TRUE(Find<views::Combobox>(u"On startup"));
  Click(u"Privacy");
  EXPECT_TRUE(Find<views::Combobox>(u"Ads and trackers"));
  Click(u"Appearance");
  EXPECT_TRUE(Find<views::Combobox>(u"Browser layout"));
}

IN_PROC_BROWSER_TEST_F(SeoulSettingsBrowserTest,
                       ContainerDialogValidatesAndPreservesBrowsingContext) {
  auto* model =
      &SeoulOrganizationServiceFactory::GetForProfile(browser()->profile())
           ->model();
  const auto before = model->ToSnapshot().workspaces.size();
  const auto tab_count = browser()->tab_strip_model()->count();
  auto* original = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(ShowSeoulSettings(browser()->profile(), SettingsPane::kProfiles));
  Click(u"Add container space…");
  auto* canceled = NameDialog(u"New container space");
  ASSERT_TRUE(canceled);
  EXPECT_FALSE(
      canceled->widget_delegate()->AsDialogDelegate()->IsDialogButtonEnabled(
          ui::mojom::DialogButton::kOk));
  canceled->Close();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(model->ToSnapshot().workspaces.size(), before);
  Click(u"Add container space…");
  AcceptName(NameDialog(u"New container space"), u"  Separate sign-ins  ");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return model->ToSnapshot().workspaces.size() == before + 1; }));
  bool isolated = false;
  for (const auto& workspace : model->ToSnapshot().workspaces) {
    if (workspace.name == "Separate sign-ins")
      isolated = workspace.isolated && !workspace.archived;
  }
  EXPECT_TRUE(isolated);
  EXPECT_EQ(browser()->tab_strip_model()->count(), tab_count);
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), original);
}

IN_PROC_BROWSER_TEST_F(SeoulSettingsBrowserTest,
                       CreateAndRenameProfileUseRealMetadata) {
  auto* original = browser()->profile();
  const auto original_name =
      original->GetPrefs()->GetString(prefs::kProfileName);
  ASSERT_TRUE(ShowSeoulSettings(original, SettingsPane::kProfiles));
  Click(u"Add profile…");
  AcceptName(NameDialog(u"New profile"), u"Settings created profile");
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = GetSeoulSettingsProfileForTesting();
    return selected && selected != original;
  }));
  auto* selected = GetSeoulSettingsProfileForTesting();
  EXPECT_EQ(selected->GetPrefs()->GetString(prefs::kProfileName),
            "Settings created profile");
  Click(u"Rename profile…");
  AcceptName(NameDialog(u"Rename profile"), u"Renamed work profile");
  EXPECT_EQ(selected->GetPrefs()->GetString(prefs::kProfileName),
            "Renamed work profile");
  EXPECT_TRUE(Find<views::Button>(u"Renamed work profile"));
  EXPECT_EQ(original->GetPrefs()->GetString(prefs::kProfileName),
            original_name);
}

// The size checks always run. Optional full-window screenshots require a
// visible compositor (run without --headless in an unlocked desktop session).
// Only this test's disposable Settings window is captured.
IN_PROC_BROWSER_TEST_F(SeoulSettingsBrowserTest,
                       AllPanesFitSupportedWindowSizes) {
  const std::u16string long_name =
      u"Research and development — a profile name that should stay readable "
      u"without widening the window";
  auto* manager = g_browser_process->profile_manager();
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->GenerateNextProfileDirectoryPath());
  profiles::UpdateProfileName(&other, long_name);
  ASSERT_TRUE(
      ShowSeoulSettings(browser()->profile(), SettingsPane::kAppearance));
  // The product specifies content size; macOS adds its native window frame.
  // Preserve the actual initial outer bounds as the default-size test case.
  const auto default_size =
      GetSeoulSettingsWidgetForTesting()->GetWindowBoundsInScreen().size();
  const auto directory =
      base::Environment::Create()->GetVar("SEOUL_SETTINGS_CAPTURE_DIR");
  const base::FilePath output(directory.value_or(""));
  if (directory) {
    base::ScopedAllowBlockingForTesting allow_io;
    ASSERT_TRUE(base::CreateDirectory(output));
  }
  auto save = [&](views::Widget* widget, const std::string& name) {
    if (!directory)
      return;
    base::test::TestFuture<gfx::Image> frame;
    ui::GrabWindowSnapshot(widget->GetNativeWindow(),
                           gfx::Rect(widget->GetWindowBoundsInScreen().size()),
                           frame.GetCallback());
    const auto captured = frame.Take();
    ASSERT_FALSE(captured.IsEmpty());
    const auto bitmap = captured.AsBitmap();
    const auto png = gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, true);
    ASSERT_TRUE(png.has_value());
    base::ScopedAllowBlockingForTesting allow_io;
    ASSERT_TRUE(base::WriteFile(output.AppendASCII(name + ".png"),
                                base::span<const uint8_t>(*png)));
  };
  for (int mode : {1, 2}) {
    browser()->profile()->GetPrefs()->SetInteger(prefs::kBrowserColorScheme,
                                                 mode);
    for (const gfx::Size size :
         {gfx::Size(680, 480), gfx::Size(760, 620), default_size}) {
      for (int pane = 0; pane < 6; ++pane) {
        SCOPED_TRACE(std::to_string(mode) + ":" + size.ToString() + ":" +
                     std::to_string(pane));
        ASSERT_TRUE(ShowSeoulSettings(browser()->profile(),
                                      static_cast<SettingsPane>(pane)));
        auto* widget = GetSeoulSettingsWidgetForTesting();
        widget->SetSize(size);
        widget->LayoutRootViewIfNecessary();
        base::RunLoop().RunUntilIdle();
        auto* scroll = Find<views::ScrollView>(u"");
        ASSERT_TRUE(scroll);
        ASSERT_TRUE(scroll->contents());
        // macOS delivers the resized content bounds asynchronously. Let the
        // scheduled layout run before checking the actual constrained width.
        ASSERT_TRUE(base::test::RunUntil([&] {
          widget->LayoutRootViewIfNecessary();
          return scroll->width() > 0 && scroll->contents()->width() > 0 &&
                 scroll->contents()->width() <= scroll->width();
        }));
        if (pane == static_cast<int>(SettingsPane::kAppearance)) {
          for (const auto* name : {u"Browser layout", u"Appearance"}) {
            auto* choice = Find<views::Combobox>(name);
            ASSERT_TRUE(choice);
            EXPECT_GT(choice->width(), 0);
            EXPECT_GT(choice->height(), 0);
            EXPECT_EQ(choice->GetVisibleBounds(), choice->GetLocalBounds());
          }
          if (size == default_size) {
            auto* fonts = Find<views::Button>(u"Open Fonts and page zoom");
            ASSERT_TRUE(fonts);
            EXPECT_EQ(fonts->GetVisibleBounds(), fonts->GetLocalBounds());
            EXPECT_LE(scroll->contents()->height(), scroll->height());
          }
        }
        if (pane == static_cast<int>(SettingsPane::kProfiles)) {
          auto* heading = Find<views::Label>(u"Your profiles");
          ASSERT_TRUE(heading);
          EXPECT_GT(heading->height(), 0);
          EXPECT_FALSE(heading->GetVisibleBounds().IsEmpty());
          auto* add_profile = Find<views::Button>(u"Add profile…");
          ASSERT_TRUE(add_profile);
          EXPECT_GT(add_profile->width(), 0);
          EXPECT_GT(add_profile->height(), 0);
          EXPECT_FALSE(add_profile->GetVisibleBounds().IsEmpty());
          auto* long_profile = Find<views::Button>(long_name);
          ASSERT_TRUE(long_profile);
          EXPECT_LE(long_profile->width(), 240);
          EXPECT_GT(long_profile->height(), 0);
          EXPECT_FALSE(long_profile->GetVisibleBounds().IsEmpty());
        }
        const std::string name = std::to_string(mode) + "-" +
                                 std::to_string(size.width()) + "-" +
                                 std::to_string(pane);
        save(widget, name);
      }
    }
  }
}

IN_PROC_BROWSER_TEST_F(SeoulSettingsBrowserTest,
                       PrimaryEntryReusesWindowAndPreservesTabs) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("about:blank#settings-source")));
  auto* tabs = browser()->tab_strip_model();
  const int count = tabs->count();
  auto* source = tabs->GetActiveWebContents();
  chrome::ShowSettings(browser());
  auto* widget = GetSeoulSettingsWidgetForTesting();
  ASSERT_TRUE(widget);
  ASSERT_TRUE(widget->IsVisible());
  EXPECT_EQ(GetSeoulSettingsProfileForTesting(), browser()->profile());
  EXPECT_TRUE(Find<views::Combobox>(u"Browser layout"));
  chrome::ShowSettings(browser());
  EXPECT_EQ(GetSeoulSettingsWidgetForTesting(), widget);
  EXPECT_EQ(tabs->count(), count);
  EXPECT_EQ(tabs->GetActiveWebContents(), source);
  EXPECT_EQ(source->GetLastCommittedURL(), GURL("about:blank#settings-source"));
  Click(u"Privacy");
  ASSERT_TRUE(Find<views::Combobox>(u"Fingerprinting protection"));
  widget->Close();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !GetSeoulSettingsWidgetForTesting(); }));
  chrome::ShowSettings(browser());
  EXPECT_TRUE(Find<views::Combobox>(u"Fingerprinting protection"));
  EXPECT_EQ(tabs->count(), count);
}

IN_PROC_BROWSER_TEST_F(SeoulSettingsBrowserTest,
                       AppearanceWritesNativeWindowAndObservesExternalChanges) {
  ASSERT_TRUE(
      ShowSeoulSettings(browser()->profile(), SettingsPane::kAppearance));
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* prefs = browser()->profile()->GetPrefs();
  for (size_t layout = 0; layout < 3; ++layout) {
    Select(u"Browser layout", layout);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return static_cast<size_t>(browser_view->seoul_layout_mode()) == layout;
    }));
    EXPECT_EQ(prefs->GetInteger(kSeoulLayoutModePref),
              static_cast<int>(layout));
  }
  auto* toggle =
      Find<views::ToggleButton>(u"Show only the site in the sidebar");
  ASSERT_TRUE(toggle);
  const bool initial =
      prefs->GetBoolean(kSeoulUrlbarShowDomainOnlyInSidebarPref);
  Click(u"Show only the site in the sidebar");
  EXPECT_EQ(prefs->GetBoolean(kSeoulUrlbarShowDomainOnlyInSidebarPref),
            !initial);
  prefs->SetBoolean(kSeoulUrlbarShowDomainOnlyInSidebarPref, initial);
  EXPECT_EQ(Find<views::ToggleButton>(u"Show only the site in the sidebar"),
            toggle);
  EXPECT_EQ(toggle->GetIsOn(), initial);
  Select(u"Appearance", 2);
  EXPECT_EQ(prefs->GetInteger(prefs::kBrowserColorScheme), 2);
  prefs->SetInteger(prefs::kBrowserColorScheme, 1);
  EXPECT_EQ(Find<views::Combobox>(u"Appearance")->GetSelectedIndex(), 1u);
}

IN_PROC_BROWSER_TEST_F(SeoulSettingsBrowserTest,
                       ProfileSelectionWritesOnlySelectedProfile) {
  auto* manager = g_browser_process->profile_manager();
  Profile& work = profiles::testing::CreateProfileSync(
      manager, manager->GenerateNextProfileDirectoryPath());
  profiles::UpdateProfileName(&work, u"Settings Work");
  browser()->profile()->GetPrefs()->SetBoolean(prefs::kPromptForDownload,
                                               false);
  work.GetPrefs()->SetBoolean(prefs::kPromptForDownload, false);
  ASSERT_TRUE(ShowSeoulSettings(browser()->profile(), SettingsPane::kProfiles));
  auto* widget = GetSeoulSettingsWidgetForTesting();
  Click(u"Settings Work");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return GetSeoulSettingsProfileForTesting() == &work; }));
  EXPECT_EQ(GetSeoulSettingsWidgetForTesting(), widget);
  Click(u"Ask where to save each file");
  EXPECT_TRUE(work.GetPrefs()->GetBoolean(prefs::kPromptForDownload));
  EXPECT_FALSE(
      browser()->profile()->GetPrefs()->GetBoolean(prefs::kPromptForDownload));
  ASSERT_TRUE(ShowSeoulSettings(browser()->profile(), SettingsPane::kProfiles));
  EXPECT_EQ(GetSeoulSettingsWidgetForTesting(), widget);
  EXPECT_FALSE(
      Find<views::ToggleButton>(u"Ask where to save each file")->GetIsOn());
}

IN_PROC_BROWSER_TEST_F(SeoulSettingsBrowserTest,
                       ClosingReleasesTheSelectedProfileKeepAlive) {
  auto* manager = g_browser_process->profile_manager();
  const auto path = browser()->profile()->GetPath();
  auto count = [&] {
    const auto counts = manager->GetKeepAlivesByPath(path);
    auto it = counts.find(ProfileKeepAliveOrigin::kSeoulSettingsWindow);
    return it == counts.end() ? 0 : it->second;
  };
  EXPECT_EQ(count(), 0);
  ASSERT_TRUE(ShowSeoulSettings(browser()->profile()));
  EXPECT_EQ(count(), 1);
  GetSeoulSettingsWidgetForTesting()->Close();
  ASSERT_TRUE(base::test::RunUntil([&] { return count() == 0; }));
  EXPECT_FALSE(GetSeoulSettingsWidgetForTesting());
  EXPECT_FALSE(GetSeoulSettingsProfileForTesting());
}

IN_PROC_BROWSER_TEST_F(SeoulSettingsBrowserTest,
                       WindowOwnershipFollowsProfileAndStopsAtShutdown) {
  auto* original = browser()->profile();
  auto* manager = g_browser_process->profile_manager();
  Profile& work = profiles::testing::CreateProfileSync(
      manager, manager->GenerateNextProfileDirectoryPath());
  profiles::UpdateProfileName(&work, u"Owned work profile");
  ASSERT_TRUE(ShowSeoulSettings(original, SettingsPane::kProfiles));
  auto* original_runtime = SeoulRuntimeServiceFactory::GetForProfile(original);
  auto* work_runtime = SeoulRuntimeServiceFactory::GetForProfile(&work);
  auto* owner = original_runtime->settings_window();
  ASSERT_TRUE(owner);
  auto* widget = GetSeoulSettingsWidgetForTesting();
  Click(u"Owned work profile");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return GetSeoulSettingsProfileForTesting() == &work; }));
  EXPECT_FALSE(original_runtime->settings_window());
  EXPECT_EQ(work_runtime->settings_window(), owner);
  original_runtime->Shutdown();
  EXPECT_EQ(GetSeoulSettingsWidgetForTesting(), widget);
  EXPECT_EQ(GetSeoulSettingsProfileForTesting(), &work);
  work_runtime->Shutdown();
  EXPECT_FALSE(GetSeoulSettingsWidgetForTesting());
  EXPECT_FALSE(work_runtime->settings_window());
  EXPECT_FALSE(ShowSeoulSettings(&work));
}

IN_PROC_BROWSER_TEST_F(SeoulSettingsBrowserTest,
                       ShieldsDefaultsPreserveSiteOverrides) {
  auto* service =
      adblock::AdBlockServiceFactory::GetForProfile(browser()->profile());
  const GURL site("https://example.test/");
  service->SetSiteMode(site, adblock::AdBlockMode::kOff);
  ASSERT_TRUE(ShowSeoulSettings(browser()->profile(), SettingsPane::kPrivacy));
  Select(u"Ads and trackers", 2);
  EXPECT_EQ(
      service->GetSiteSettings(GURL("https://other.test/")).effective_mode,
      adblock::AdBlockMode::kAggressive);
  EXPECT_EQ(service->GetSiteSettings(site).effective_mode,
            adblock::AdBlockMode::kOff);
  Select(u"Fingerprinting protection", 2);
  EXPECT_EQ(service->GetDefaultFingerprintMode(),
            adblock::FingerprintMode::kStrict);
}

IN_PROC_BROWSER_TEST_F(SeoulSettingsBrowserTest,
                       PRE_PreferencesAndPaneSurviveRestart) {
  ASSERT_TRUE(
      ShowSeoulSettings(browser()->profile(), SettingsPane::kAppearance));
  Select(u"Browser layout", 1);
  Select(u"Appearance", 2);
  Click(u"Profiles");
  auto* pref = browser()->profile()->GetPrefs();
  if (!pref->GetBoolean(prefs::kPromptForDownload))
    Click(u"Ask where to save each file");
  GetSeoulSettingsWidgetForTesting()->Close();
  pref->CommitPendingWrite();
}

IN_PROC_BROWSER_TEST_F(SeoulSettingsBrowserTest,
                       PreferencesAndPaneSurviveRestart) {
  chrome::ShowSettings(browser());
  ASSERT_TRUE(Find<views::ToggleButton>(u"Ask where to save each file"));
  EXPECT_TRUE(
      Find<views::ToggleButton>(u"Ask where to save each file")->GetIsOn());
  EXPECT_EQ(browser()->profile()->GetPrefs()->GetInteger(kSeoulLayoutModePref),
            1);
  EXPECT_EQ(
      browser()->profile()->GetPrefs()->GetInteger(prefs::kBrowserColorScheme),
      2);
  EXPECT_EQ(
      BrowserView::GetBrowserViewForBrowser(browser())->seoul_layout_mode(),
      SeoulLayoutMode::kMultiple);
}

class SeoulSettingsPolicyBrowserTest : public SeoulSettingsBrowserTest {
 protected:
  void SetUpInProcessBrowserTestFixture() override {
    provider_.SetDefaultReturns(true, true);
    policy::BrowserPolicyConnector::SetPolicyProviderForTesting(&provider_);
  }
  testing::NiceMock<policy::MockConfigurationPolicyProvider> provider_;
};

IN_PROC_BROWSER_TEST_F(SeoulSettingsPolicyBrowserTest,
                       ManagedDownloadsCannotBeChangedAndUnlockLive) {
  policy::PolicyMap policy;
  policy.Set(policy::key::kPromptForDownloadLocation,
             policy::POLICY_LEVEL_MANDATORY, policy::POLICY_SCOPE_MACHINE,
             policy::POLICY_SOURCE_CLOUD, base::Value(true), nullptr);
  provider_.UpdateChromePolicy(policy);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return browser()->profile()->GetPrefs()->IsManagedPreference(
        prefs::kPromptForDownload);
  }));
  ASSERT_TRUE(ShowSeoulSettings(browser()->profile(), SettingsPane::kProfiles));
  auto* toggle = Find<views::ToggleButton>(u"Ask where to save each file");
  ASSERT_TRUE(toggle);
  EXPECT_TRUE(toggle->GetIsOn());
  EXPECT_FALSE(toggle->GetEnabled());
  // Force the callback as a stale event would: the write path must also check
  // policy, rather than relying solely on the disabled visual state.
  views::test::ButtonTestApi(toggle).NotifyDefaultMouseClick();
  EXPECT_TRUE(
      browser()->profile()->GetPrefs()->GetBoolean(prefs::kPromptForDownload));
  provider_.UpdateChromePolicy(policy::PolicyMap());
  ASSERT_TRUE(base::test::RunUntil([&] { return toggle->GetEnabled(); }));
  EXPECT_EQ(Find<views::ToggleButton>(u"Ask where to save each file"), toggle);
  const bool unmanaged_value =
      browser()->profile()->GetPrefs()->GetBoolean(prefs::kPromptForDownload);
  Click(u"Ask where to save each file");
  EXPECT_EQ(
      browser()->profile()->GetPrefs()->GetBoolean(prefs::kPromptForDownload),
      !unmanaged_value);
}

}  // namespace
}  // namespace seoul
