#pragma once

// Hotbar layout shared by the HUD, the tick's selection keys and the
// startup/restore code in main.cpp. Moved out of main.cpp by T-M1 (pure code
// motion): the slot count and the bucket's slot index are unchanged.
//
// The hotbar is a 10-slot creative palette: keys 1..9 pick blocks, 0 picks the
// bucket (T-F1's minimal item form).

namespace opencraft::client {

inline constexpr int kBucketSlot = 9;
inline constexpr int kHotbarSlots = 10;

} // namespace opencraft::client
