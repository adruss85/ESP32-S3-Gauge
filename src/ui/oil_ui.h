// =====================================================================
// oil_ui.h - the digital oil temperature face
//
// Same layout as the boost gauge: 270 degree arc with six scale labels,
// a large value with decimal point on the baseline, an OIL °C caption,
// and the intake-air temperature under a thin rule.
// The arc scale is configurable (0.1 °C steps) and persisted in NVS.
// =====================================================================
#pragma once
#include "net/ohx_now.h"
#include "theme.h"

namespace oil_ui {

void begin();

void update(const ohxnow::Telemetry &t, const ohxnow::Stats &s);

bool applyScale(int16_t min10, int16_t max10);

void setScheme(theme::Scheme s, bool persist = true);

// Slide offset for mode transitions (swipe = slide, key/web = fade)
void setSlide(int dx);
int slide();

void setVisible(bool visible);
void setOpacity(uint8_t opacity);
lv_obj_t *root();

}  // namespace oil_ui