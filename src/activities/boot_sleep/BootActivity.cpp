#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#ifdef ENABLE_KOREAN_VERSION
#include "images/KimtomaMark120.h"
#else
#include "images/Logo120.h"
#endif

namespace {
#ifdef ENABLE_KOREAN_VERSION
constexpr const uint8_t* kBootMark = KIMTOMA_MARK_120;
constexpr StrId kBootBrand = StrId::STR_KIMTOMA_BRAND;
#else
constexpr const uint8_t* kBootMark = Logo120;
constexpr StrId kBootBrand = StrId::STR_CROSSPOINT;
#endif
}  // namespace

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(kBootMark, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, I18N.get(kBootBrand), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}
