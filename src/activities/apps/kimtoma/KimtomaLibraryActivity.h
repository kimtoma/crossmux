#pragma once

#include <I18n.h>

#include <cstdint>

#include "../../../reading_sync/ReadingSyncUiPolicy.h"
#include "../../../util/ButtonNavigator.h"
#include "../../Activity.h"

enum class KimtomaLibraryMode : uint8_t { Library, Settings };

class KimtomaLibraryActivity final : public Activity {
 public:
  KimtomaLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, KimtomaLibraryMode mode)
      : Activity("KimtomaLibrary", renderer, mappedInput), mode_(mode) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;

 private:
  void reloadSnapshot();
  void activateSelectedAction();
  StrId actionLabel(int index) const;
  StrId stateLabel(KimtomaSyncUiState state) const;
  StrId coverLabel() const;

  ButtonNavigator buttonNavigator_;
  KimtomaLibraryMode mode_;
  ReadingSyncAcceptedSummary accepted_;
  KimtomaConnectionTestState connectionState_ = KimtomaConnectionTestState::Idle;
  int selectedAction_ = 0;
  bool configured_ = false;
  bool authenticationPaused_ = false;
  bool queueCorrupt_ = false;
  bool pending_ = false;
  bool coverPending_ = false;
  bool hasAccepted_ = false;
  bool observedRunning_ = false;
  bool showBusy_ = false;
  char lastSyncText_[24] = {};
};
