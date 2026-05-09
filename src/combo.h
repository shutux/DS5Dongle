#ifndef DS5_BRIDGE_COMBO_H
#define DS5_BRIDGE_COMBO_H

#include <cstdint>

// Call every frame from the main loop with the latest DS5 report data (63 bytes).
// Returns true if a mode switch was triggered (config saved + USB reconnect needed).
bool combo_check(const uint8_t *ds5_report);

#endif
