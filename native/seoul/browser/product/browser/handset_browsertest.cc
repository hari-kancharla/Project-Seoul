// Project Seoul Handset - end-to-end device presentation.
//
// Every other Handset test is a unit test over pure functions: they prove the
// geometry and the User-Agent strings are right, not that a real page ever
// sees them. This file asserts the only thing that actually matters, which is
// what the page itself reports once it is presented as a phone: its screen,
// its pixel ratio, its identity, its media features and its touch support.
//
// It exists because every one of those four signals travels a different route
// into Blink - device emulation over a mojo remote, WebPreferences through
// Chromium's own recomputation, the User-Agent through the navigation entry -
// and a compiling, unit-tested implementation can still deliver none of them.

#include <string>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "seoul/browser/handset/handset_types.h"
#include "seoul/browser/handset/viewport_math.h"
#include "seoul/browser/product/browser/handset_mode.h"
#include "seoul/browser/product/browser/handset_picker_menu.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace seoul {
namespace {

class HandsetBrowserTest : public InProcessBrowserTest {
 public:
  HandsetBrowserTest() = default;
  ~HandsetBrowserTest() override = default;

 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // Two pages, because the difference between them is the point. A page that
    // declares width=device-width lays out at the device's width; a page that
    // declares nothing lays out at Blink's 980px fallback and is zoomed out,
    // which is what a real phone does with a site that never adapted. Serving
    // both lets the same suite assert that Seoul honours the declaration
    // rather than simply forcing every page to the device width.
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating([](const net::test_server::HttpRequest& request)
                                -> std::unique_ptr<net::test_server::HttpResponse> {
          auto response = std::make_unique<net::test_server::BasicHttpResponse>();
          response->set_content_type("text/html");
          if (request.relative_url == "/responsive.html") {
            response->set_content(
                "<!doctype html><meta name=viewport "
                "content='width=device-width, initial-scale=1'><title>r</title>"
                "<body>responsive");
          } else if (request.relative_url == "/unadapted.html") {
            response->set_content(
                "<!doctype html><title>u</title><body>unadapted");
          } else {
            return nullptr;
          }
          return response;
        }));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  content::WebContents* contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  // Handset mode reloads the page under the new identity, so every assertion
  // has to be made against the document that reload produced rather than the
  // one that was on screen when the mode was switched.
  void NavigateAndWaitForHandset(bool enable, const std::string& profile_id) {
    content::TestNavigationObserver observer(contents());
    if (enable) {
      ASSERT_TRUE(EnableHandsetMode(contents(), profile_id,
                                    HandsetOrientation::kPortrait,
                                    HandsetSnapMode::kSnapToProfile));
    } else {
      DisableHandsetMode(contents());
    }
    observer.Wait();
  }

  int EvalInt(const std::string& script) {
    return content::EvalJs(contents(), script).ExtractInt();
  }
  bool EvalBool(const std::string& script) {
    return content::EvalJs(contents(), script).ExtractBool();
  }
  std::string EvalString(const std::string& script) {
    return content::EvalJs(contents(), script).ExtractString();
  }
};

// The whole feature, from the page's point of view. Asserted as one case
// because the signals are only meaningful together: a mobile User-Agent with a
// desktop viewport is exactly the half-applied state this is meant to catch.
IN_PROC_BROWSER_TEST_F(HandsetBrowserTest, PresentsThePageAsAPhone) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/responsive.html")));

  // Baseline: an ordinary desktop page. Captured rather than assumed, because
  // the bot's display is not known here - only that it is not a phone.
  const int desktop_width = EvalInt("screen.width");
  const std::string desktop_ua = EvalString("navigator.userAgent");
  EXPECT_FALSE(EvalBool("matchMedia('(pointer: coarse)').matches"));
  EXPECT_FALSE(EvalBool("matchMedia('(hover: none)').matches"));

  const HandsetProfile* const phone = FindHandsetProfile("iphone");
  ASSERT_TRUE(phone);

  NavigateAndWaitForHandset(/*enable=*/true, "iphone");
  ASSERT_TRUE(IsHandsetModeEnabled(contents()));

  // 1. The emulated screen. This is the signal with no WebPreferences
  //    equivalent, so it is the one that proves device emulation is live and
  //    not merely that the window was made narrow.
  EXPECT_EQ(phone->portrait_width_dip, EvalInt("screen.width"));
  EXPECT_EQ(phone->portrait_height_dip, EvalInt("screen.height"));
  EXPECT_EQ(phone->device_scale_factor, EvalInt("Math.round(devicePixelRatio)"));

  // 2. The viewport lays out at the device width, because this page asks for
  //    it. This is the signal that separates a real mobile viewport from a
  //    merely narrow window.
  EXPECT_EQ(phone->portrait_width_dip, EvalInt("innerWidth"));

  // 3. Identity. An iOS profile presents Safari and suppresses client hints.
  const std::string ua = EvalString("navigator.userAgent");
  EXPECT_NE(std::string::npos, ua.find("iPhone")) << ua;
  EXPECT_EQ(std::string::npos, ua.find("Macintosh")) << ua;
  EXPECT_NE(desktop_ua, ua);

  // 4. Media features. A site whose menu opens on hover is unusable without
  //    these, and no viewport width substitutes for them.
  EXPECT_TRUE(EvalBool("matchMedia('(pointer: coarse)').matches"));
  EXPECT_TRUE(EvalBool("matchMedia('(hover: none)').matches"));

  // 5. Touch. The regression this guards is subtle and worse than no touch at
  //    all: if the emulator is enabled in its injecting mode with nothing to
  //    inject, the page detects touch support, retires its mouse handlers and
  //    then never receives an event. maxTouchPoints proves support is real.
  EXPECT_TRUE(EvalBool("'ontouchstart' in window"));
  EXPECT_GT(EvalInt("navigator.maxTouchPoints"), 0);

  // Turning it off returns the page to the display it is actually on.
  NavigateAndWaitForHandset(/*enable=*/false, std::string());
  EXPECT_FALSE(IsHandsetModeEnabled(contents()));
  EXPECT_EQ(desktop_width, EvalInt("screen.width"));
  EXPECT_EQ(desktop_ua, EvalString("navigator.userAgent"));
  EXPECT_FALSE(EvalBool("matchMedia('(pointer: coarse)').matches"));
}

// The mode has to survive navigation. This is the failure the spec calls out as
// the quiet one: wiring only the first of Chromium's two preference
// recomputation hooks leaves the mode looking correct until the user follows a
// link, at which point the page silently returns to desktop layout.
IN_PROC_BROWSER_TEST_F(HandsetBrowserTest, SurvivesNavigation) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/responsive.html")));
  NavigateAndWaitForHandset(/*enable=*/true, "iphone");

  const HandsetProfile* const phone = FindHandsetProfile("iphone");
  ASSERT_TRUE(phone);
  ASSERT_EQ(phone->portrait_width_dip, EvalInt("screen.width"));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/responsive.html?second")));

  EXPECT_TRUE(IsHandsetModeEnabled(contents()));
  EXPECT_EQ(phone->portrait_width_dip, EvalInt("screen.width"));
  EXPECT_EQ(phone->portrait_width_dip, EvalInt("innerWidth"));
  EXPECT_TRUE(EvalBool("matchMedia('(pointer: coarse)').matches"));
  EXPECT_NE(std::string::npos,
            EvalString("navigator.userAgent").find("iPhone"));
}

// A page that never adapted must still behave the way it would on a real
// phone: laid out at Blink's 980px fallback and zoomed out, not forced to the
// device width. Getting this wrong in the other direction - forcing every page
// to 393px - would reflow desktop-only sites into a broken single column and
// look like a rendering bug rather than a presentation.
IN_PROC_BROWSER_TEST_F(HandsetBrowserTest, UnadaptedPageUsesTheFallbackWidth) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/unadapted.html")));
  NavigateAndWaitForHandset(/*enable=*/true, "iphone");

  const HandsetProfile* const phone = FindHandsetProfile("iphone");
  ASSERT_TRUE(phone);

  // The screen is still the device's - that signal does not depend on the
  // page - but the layout viewport is the fallback, and the page is scaled
  // down to fit it on screen.
  EXPECT_EQ(phone->portrait_width_dip, EvalInt("screen.width"));
  EXPECT_EQ(980, EvalInt("innerWidth"));
  EXPECT_LT(EvalInt("Math.round(visualViewport.scale * 100)"), 100);
}

// The device picker, exercised through its real ExecuteCommand path against a
// live WebContents. Show() itself is not called - that pops an actual
// interactive, asynchronous native menu, which is a UI-manual-test concern,
// not a browser-test one. ExecuteCommandForTesting builds the same model
// Show() would and invokes the same delegate handler a click routes to, so
// what is under test is the real logic driving the menu, not a reimplemented
// stand-in for it.
IN_PROC_BROWSER_TEST_F(HandsetBrowserTest, PickerSelectsARealProfile) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/responsive.html")));

  const HandsetProfile* const android = FindHandsetProfile("android");
  ASSERT_TRUE(android);

  // Command ids are assigned 0..N in HandsetProfiles() catalogue order; find
  // "android"'s position rather than assuming one, so a future reorder of the
  // catalogue cannot silently make this test assert against the wrong device.
  int android_command = -1;
  int index = 0;
  for (const HandsetProfile& profile : HandsetProfiles()) {
    if (profile.id == "android") {
      android_command = index;
    }
    ++index;
  }
  ASSERT_NE(android_command, -1);

  // Selecting a device for the first time opens a dedicated tab for the
  // phone presentation rather than converting the tab being read, so the
  // reload this causes - a real device change, state's stored profile goes
  // from null to "android" - has to be awaited on whichever tab becomes
  // active, not on the one the command was invoked against; that one is
  // never touched.
  content::WebContents* const original = contents();
  const int original_tab_count = browser()->tab_strip_model()->count();

  HandsetPickerMenu picker;
  picker.ExecuteCommandForTesting(original, android_command);

  ASSERT_EQ(original_tab_count + 1, browser()->tab_strip_model()->count())
      << "picking a device for the first time must open a new tab";
  content::WebContents* const handset_tab = contents();
  ASSERT_NE(original, handset_tab)
      << "the new tab, not the one being read, must become the Handset";
  ASSERT_TRUE(content::WaitForLoadStop(handset_tab));

  EXPECT_FALSE(IsHandsetModeEnabled(original))
      << "the tab being read must stay exactly as it was";
  EXPECT_TRUE(IsHandsetModeEnabled(handset_tab));
  const HandsetProfile* const active = HandsetProfileFor(handset_tab);
  ASSERT_TRUE(active);
  EXPECT_EQ(active->id, "android");
  EXPECT_EQ(android->portrait_width_dip,
           content::EvalJs(handset_tab, "screen.width").ExtractInt());
  EXPECT_NE(std::string::npos,
           content::EvalJs(handset_tab, "navigator.userAgent")
               .ExtractString()
               .find("Android"));

  // The selected device's item is checked when the picker is reopened on the
  // tab that is now the Handset; an unselected one, such as whichever
  // command precedes it, is not.
  HandsetPickerMenu reopened_picker;
  reopened_picker.ExecuteCommandForTesting(handset_tab, android_command);
  EXPECT_TRUE(reopened_picker.IsCommandIdChecked(android_command));
  if (android_command > 0) {
    EXPECT_FALSE(reopened_picker.IsCommandIdChecked(android_command - 1));
  }
}

IN_PROC_BROWSER_TEST_F(HandsetBrowserTest, PickerRotateFlipsRealDimensions) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/responsive.html")));
  NavigateAndWaitForHandset(/*enable=*/true, "iphone");

  const HandsetProfile* const phone = FindHandsetProfile("iphone");
  ASSERT_TRUE(phone);
  ASSERT_EQ(phone->portrait_width_dip, EvalInt("screen.width"));

  // Rotate does not reload: EnableHandsetMode only reloads when
  // state->profile() actually changes (a real device swap), and rotating
  // keeps the same profile pointer with a different orientation - correctly,
  // since the emulated screen and view size are pushed live over the device
  // presentation mojo remote and take effect without a navigation. Waiting on
  // a TestNavigationObserver here would hang forever for a page load that
  // never happens; the real signal is the screen dimensions changing
  // synchronously with the call.
  HandsetPickerMenu picker;
  picker.ExecuteCommandForTesting(contents(),
                                  kHandsetPickerCommandRotate);

  // A real rotation, not a relabel: width and height genuinely change in the
  // live page to the device's own landscape dimensions.
  EXPECT_EQ(HandsetWidthForOrientation(*phone, HandsetOrientation::kLandscape),
           EvalInt("screen.width"));
  EXPECT_EQ(
      HandsetHeightForOrientation(*phone, HandsetOrientation::kLandscape),
      EvalInt("screen.height"));
}

IN_PROC_BROWSER_TEST_F(HandsetBrowserTest, PickerTurnOffRestoresDesktop) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/responsive.html")));
  const int desktop_width = EvalInt("screen.width");
  const std::string desktop_ua = EvalString("navigator.userAgent");

  NavigateAndWaitForHandset(/*enable=*/true, "iphone");
  ASSERT_TRUE(IsHandsetModeEnabled(contents()));

  content::TestNavigationObserver observer(contents());
  HandsetPickerMenu picker;
  picker.ExecuteCommandForTesting(contents(),
                                  kHandsetPickerCommandTurnOff);
  observer.Wait();

  EXPECT_FALSE(IsHandsetModeEnabled(contents()));
  EXPECT_EQ(desktop_width, EvalInt("screen.width"));
  EXPECT_EQ(desktop_ua, EvalString("navigator.userAgent"));
}

// The regression this guards: EnableHandsetMode's free_width_dip/
// free_height_dip parameters exist so a caller - the picker's custom-size
// dialog - can actually request an arbitrary size. Before this session's
// change, the parameters did not exist at all and kFree always fell back to
// the profile's own dimensions no matter what a caller wanted; this proves
// numbers a human typed reach the real page, not just that the call compiles.
IN_PROC_BROWSER_TEST_F(HandsetBrowserTest, ArbitraryCustomSizeReachesThePage) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/responsive.html")));

  // A fresh WebContents has no prior device, so this is a real device change
  // and EnableHandsetMode reloads the page internally. Evaluating JS before
  // that reload commits hits the outgoing RenderFrame mid-teardown - an
  // intermittent "RenderFrame deleted" crash, not a deterministic failure,
  // which is what let this pass twice before crashing on a third run.
  {
    content::TestNavigationObserver observer(contents());
    ASSERT_TRUE(EnableHandsetMode(contents(), "iphone",
                                  HandsetOrientation::kPortrait,
                                  HandsetSnapMode::kFree,
                                  /*free_width_dip=*/500,
                                  /*free_height_dip=*/920));
    observer.Wait();
  }

  EXPECT_EQ(500, EvalInt("screen.width"));
  EXPECT_EQ(920, EvalInt("screen.height"));
  EXPECT_EQ(500, EvalInt("innerWidth"));

  // The typed size survives a navigation exactly like a catalogue device's
  // does - it is stored state, not a one-shot argument that a re-sync would
  // silently drop back to the profile default.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/responsive.html?second")));
  EXPECT_EQ(500, EvalInt("screen.width"));
  EXPECT_EQ(920, EvalInt("screen.height"));
}

}  // namespace
}  // namespace seoul
