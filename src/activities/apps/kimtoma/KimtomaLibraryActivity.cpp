#include "KimtomaLibraryActivity.h"

#ifdef ENABLE_KIMTOMA_READING_SYNC

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../../../reading_sync/ReadingSyncCoordinator.h"
#include "../../../reading_sync/ReadingSyncCredentialStore.h"
#include "../../../reading_sync/ReadingSyncQueue.h"
#include "../../../util/TimeUtils.h"

namespace {
constexpr int kActionCount = 2;

void drawProfileGlyph(const GfxRenderer& renderer, const int x, const int y) {
  renderer.drawRoundedRect(x, y, 26, 26, 1, 7, true);
  renderer.drawRect(x + 4, y + 8, 7, 6, 2, true);
  renderer.drawRect(x + 15, y + 8, 7, 6, 2, true);
  renderer.drawLine(x + 11, y + 10, x + 15, y + 10, 2, true);
  renderer.fillRect(x + 10, y + 17, 2, 2, true);
  renderer.fillRect(x + 14, y + 17, 2, 2, true);
}
}  // namespace

void KimtomaLibraryActivity::onEnter() {
  Activity::onEnter();
  selectedAction_ = 0;
  showBusy_ = false;
  observedRunning_ = READING_SYNC.isRunning();
  if (!observedRunning_) {
    reloadSnapshot();
  }
  requestUpdate();
}

void KimtomaLibraryActivity::onExit() {
  READING_SYNC.requestCancel();
  READING_SYNC.waitUntilStopped();
  accepted_ = {};
  Activity::onExit();
}

void KimtomaLibraryActivity::reloadSnapshot() {
  if (READING_SYNC.isRunning()) {
    return;
  }

  configured_ = READING_SYNC_CREDENTIALS.hasToken();
  authenticationPaused_ = READING_SYNC_QUEUE.authenticationPaused();
  queueCorrupt_ = READING_SYNC_QUEUE.isCorrupt();
  pending_ = READING_SYNC_QUEUE.pending() != nullptr;
  coverPending_ = READING_SYNC_QUEUE.coverPending() != nullptr;
  connectionState_ = READING_SYNC.connectionTestState();

  const ReadingSyncAcceptedSummary* accepted = READING_SYNC_QUEUE.lastAccepted();
  hasAccepted_ = accepted != nullptr;
  accepted_ = hasAccepted_ ? *accepted : ReadingSyncAcceptedSummary{};

  lastSyncText_[0] = '\0';
  if (hasAccepted_ && TimeUtils::isClockValid(accepted_.acceptedAt)) {
    const std::string formatted = TimeUtils::formatDate(accepted_.acceptedAt);
    std::snprintf(lastSyncText_, sizeof(lastSyncText_), "%s", formatted.c_str());
  }
}

void KimtomaLibraryActivity::loop() {
  const bool running = READING_SYNC.isRunning();
  if (observedRunning_ && !running) {
    reloadSnapshot();
    showBusy_ = false;
    requestUpdate();
  } else if (!observedRunning_ && running) {
    requestUpdate();
  }
  observedRunning_ = running;

  buttonNavigator_.onNext([this] {
    selectedAction_ = ButtonNavigator::nextIndex(selectedAction_, kActionCount);
    requestUpdate();
  });
  buttonNavigator_.onPrevious([this] {
    selectedAction_ = ButtonNavigator::previousIndex(selectedAction_, kActionCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelectedAction();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (mode_) {
      case KimtomaLibraryMode::Library:
        activityManager.goToApps();
        break;
      case KimtomaLibraryMode::Settings:
        finish();
        break;
    }
  }
}

void KimtomaLibraryActivity::activateSelectedAction() {
  if (READING_SYNC.isRunning()) {
    showBusy_ = true;
    requestUpdate();
    return;
  }

  switch (mode_) {
    case KimtomaLibraryMode::Library:
      if (selectedAction_ == 0) {
        const bool started = READING_SYNC.requestManualRetryAndStart();
        showBusy_ = !started && READING_SYNC.isRunning();
      } else {
        auto activity = makeUniqueNoThrow<KimtomaLibraryActivity>(renderer, mappedInput, KimtomaLibraryMode::Settings);
        if (!activity) {
          LOG_ERR("KML", "Could not allocate kimtoma settings activity");
          showBusy_ = true;
        } else {
          startActivityForResult(std::move(activity), [](const ActivityResult&) {});
        }
      }
      break;
    case KimtomaLibraryMode::Settings:
      if (selectedAction_ == 0) {
        const bool started = READING_SYNC.requestConnectionTest();
        showBusy_ = !started && READING_SYNC.isRunning();
      } else {
        const bool started = READING_SYNC.requestManualRetryAndStart();
        showBusy_ = !started && READING_SYNC.isRunning();
      }
      break;
  }
  requestUpdate();
}

StrId KimtomaLibraryActivity::actionLabel(const int index) const {
  switch (mode_) {
    case KimtomaLibraryMode::Library:
      return index == 0 ? StrId::STR_KIMTOMA_RETRY : StrId::STR_KIMTOMA_SETTINGS;
    case KimtomaLibraryMode::Settings:
      return index == 0 ? StrId::STR_KIMTOMA_CONNECTION_TEST : StrId::STR_KIMTOMA_RETRY;
  }
  return StrId::STR_KIMTOMA_RETRY;
}

StrId KimtomaLibraryActivity::stateLabel(const KimtomaSyncUiState state) const {
  switch (state) {
    case KimtomaSyncUiState::NotConfigured:
      return StrId::STR_KIMTOMA_NOT_CONFIGURED;
    case KimtomaSyncUiState::ReadyNoBook:
      return StrId::STR_KIMTOMA_READY_NO_BOOK;
    case KimtomaSyncUiState::Ready:
      return StrId::STR_KIMTOMA_READY;
    case KimtomaSyncUiState::Pending:
      return StrId::STR_KIMTOMA_PENDING;
    case KimtomaSyncUiState::Syncing:
      return StrId::STR_KIMTOMA_SYNCING;
    case KimtomaSyncUiState::AuthenticationRequired:
      return StrId::STR_KIMTOMA_AUTH_REQUIRED;
    case KimtomaSyncUiState::QueueCorrupt:
      return StrId::STR_KIMTOMA_QUEUE_CORRUPT;
  }
  return StrId::STR_KIMTOMA_NOT_CONFIGURED;
}

StrId KimtomaLibraryActivity::coverLabel() const {
  if (coverPending_) {
    return StrId::STR_KIMTOMA_COVER_PENDING;
  }
  if (!hasAccepted_) {
    return StrId::STR_KIMTOMA_COVER_NONE;
  }
  switch (accepted_.coverState) {
    case ReadingSyncCoverState::None:
      return StrId::STR_KIMTOMA_COVER_NONE;
    case ReadingSyncCoverState::Pending:
      return StrId::STR_KIMTOMA_COVER_PENDING;
    case ReadingSyncCoverState::Uploaded:
      return StrId::STR_KIMTOMA_COVER_UPLOADED;
  }
  return StrId::STR_KIMTOMA_COVER_NONE;
}

void KimtomaLibraryActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const bool running = READING_SYNC.isRunning();
  const KimtomaSyncUiState state = resolveKimtomaSyncUiState(
      {configured_, authenticationPaused_, running, pending_ || coverPending_, queueCorrupt_, hasAccepted_});
  const KimtomaTextRowLayout textRowLayout = makeKimtomaTextRowLayout(renderer.getLineHeight(UI_10_FONT_ID));

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 mode_ == KimtomaLibraryMode::Library ? tr(STR_KIMTOMA_LIBRARY) : tr(STR_KIMTOMA_INTEGRATION));
  drawProfileGlyph(renderer, metrics.contentSidePadding, metrics.topPadding + 7);

  const int contentLeft = metrics.contentSidePadding;
  const int contentWidth = pageWidth - metrics.contentSidePadding * 2;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  const char* status = I18N.get(stateLabel(state));
  const int statusWidth = std::min(contentWidth, renderer.getTextWidth(UI_10_FONT_ID, status) + 20);
  renderer.drawRoundedRect(contentLeft, y, statusWidth, textRowLayout.statusBoxHeight, 1, 6, true);
  renderer.drawText(UI_10_FONT_ID, contentLeft + 10, y + textRowLayout.statusTextOffsetY, status, true,
                    EpdFontFamily::BOLD);
  y += textRowLayout.statusBoxHeight + 14;

  if (hasAccepted_) {
    const GfxRenderer::ClipScope clip(renderer, contentLeft, y, contentWidth, 58);
    renderer.drawText(UI_12_FONT_ID, contentLeft, y, accepted_.title.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, contentLeft, y + 26, accepted_.author.c_str());
  } else {
    renderer.drawText(UI_12_FONT_ID, contentLeft, y, tr(STR_KIMTOMA_NO_SYNCED_BOOK), true, EpdFontFamily::BOLD);
  }
  y += 66;

  const uint8_t progress = hasAccepted_ ? std::min<uint8_t>(accepted_.progressPercent, 100) : 0;
  char progressText[8] = {};
  std::snprintf(progressText, sizeof(progressText), "%u%%", static_cast<unsigned>(progress));
  renderer.drawText(UI_10_FONT_ID, contentLeft, y, progressText, true, EpdFontFamily::BOLD);
  renderer.drawRect(contentLeft + 55, y + 2, std::max(1, contentWidth - 55), 12);
  const int fillWidth = std::max(0, contentWidth - 59) * progress / 100;
  if (fillWidth > 0) {
    renderer.fillRect(contentLeft + 57, y + 4, fillWidth, 8);
  }
  y += 32;

  renderer.drawText(SMALL_FONT_ID, contentLeft, y, tr(STR_KIMTOMA_LAST_SYNC), true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, contentLeft + 135, y,
                    lastSyncText_[0] == '\0' ? tr(STR_KIMTOMA_TIME_UNKNOWN) : lastSyncText_);
  y += 25;
  renderer.drawText(SMALL_FONT_ID, contentLeft, y, tr(STR_KIMTOMA_PENDING_RECORD), true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, contentLeft + 135, y,
                    pending_ ? tr(STR_KIMTOMA_PENDING) : tr(STR_KIMTOMA_NO_PENDING));
  y += 25;
  renderer.drawText(SMALL_FONT_ID, contentLeft, y, tr(STR_KIMTOMA_COVER_STATE), true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, contentLeft + 135, y, I18N.get(coverLabel()));

  if (mode_ == KimtomaLibraryMode::Settings) {
    y += 28;
    const char* connectionMessage = nullptr;
    switch (connectionState_) {
      case KimtomaConnectionTestState::Idle:
        break;
      case KimtomaConnectionTestState::Running:
        connectionMessage = tr(STR_KIMTOMA_WORK_IN_PROGRESS);
        break;
      case KimtomaConnectionTestState::Succeeded:
        connectionMessage = tr(STR_KIMTOMA_CONNECTION_SUCCEEDED);
        break;
      case KimtomaConnectionTestState::AuthenticationFailed:
        connectionMessage = tr(STR_KIMTOMA_AUTH_REQUIRED);
        break;
      case KimtomaConnectionTestState::NetworkFailed:
        connectionMessage = tr(STR_KIMTOMA_NETWORK_ERROR);
        break;
    }
    renderer.drawText(SMALL_FONT_ID, contentLeft, y,
                      connectionMessage == nullptr ? tr(STR_KIMTOMA_WEB_TOKEN_GUIDE) : connectionMessage);
  }

  if (showBusy_) {
    renderer.drawText(SMALL_FONT_ID, contentLeft, pageHeight - metrics.buttonHintsHeight - 112,
                      tr(STR_KIMTOMA_WORK_IN_PROGRESS), true, EpdFontFamily::BOLD);
  }

  const int actionsTop =
      pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - textRowLayout.actionRowPitch * kActionCount;
  for (int index = 0; index < kActionCount; ++index) {
    const int rowY = actionsTop + index * textRowLayout.actionRowPitch;
    if (index == selectedAction_) {
      renderer.fillRectDither(contentLeft, rowY, contentWidth, textRowLayout.actionBoxHeight, Color::LightGray);
    }
    renderer.drawRect(contentLeft, rowY, contentWidth, textRowLayout.actionBoxHeight, index == selectedAction_ ? 2 : 1,
                      true);
    renderer.drawText(UI_10_FONT_ID, contentLeft + 12, rowY + textRowLayout.actionTextOffsetY,
                      I18N.get(actionLabel(index)), true,
                      index == selectedAction_ ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

bool KimtomaLibraryActivity::preventAutoSleep() { return READING_SYNC.isRunning(); }

#endif
