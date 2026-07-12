#pragma once

#include "../../util/ButtonNavigator.h"
#include "../Activity.h"

// Apps menu — entry-point for non-reader sub-apps (Reading Stats, WeRead, Standby).
// The list is the constexpr `kAppEntries` table in AppsMenuActivity.cpp; add a new app by
// appending one row there and a goTo<App>() in ActivityManager. See README.md.
class AppsMenuActivity final : public Activity {
 public:
  AppsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AppsMenu", renderer, mappedInput) {}
  ~AppsMenuActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selected = 0;
};
