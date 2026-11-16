// PD2's map event roll without engine types, so a host check covers it (plugin/tests/event_roll_check.cpp, #950).
#pragma once

#include <cstdint>

namespace pd2::event_roll {

inline constexpr int32_t NoEvent = 10, Altar = 19, Tomb = 21;
enum : int32_t { QualityNormal = 2, QualityMagic = 4 };  // D2R item qualities; anything else is rare or better

// PD2's draw (rand() % 12) -> event; 0 draws again (1.13c ids Mendeln 11 ... Tomb 21)
// Slot 10 was PD2's Invaders (20), which spawn nothing until MS10: it draws again, so its weight goes to the other events.
inline constexpr int32_t Draws[12] = {0, 11, 12, 0, 14, 15, 16, 0, 18, 19, 0, 21};

// The percent chance a map of `quality` carries an event
constexpr auto Chance(int32_t quality) noexcept -> int32_t { return quality == QualityNormal ? 11 : quality == QualityMagic ? 17 : 34; }

// `rand` is the CRT's rand(); a forced map always draws (up to 40 times) and never takes the Altar.
// `tombOpen` false (the Bitter Depths is not built, #965) re-draws the Tomb, so no portal opens to a level with no preset.
template <class Rand>
auto Pick(int32_t quality, bool forced, Rand&& rand, bool tombOpen = true) noexcept -> int32_t {
	int32_t event = 0;
	if (forced || rand() % 100 < Chance(quality)) {
		for (int draw = 0; draw < 40 && event == 0; ++draw) {
			const int32_t e = Draws[rand() % 12];
			event = (forced && e == Altar) || (!tombOpen && e == Tomb) ? 0 : e;
		}
	}
	return event != 0 ? event : NoEvent;
}

}  // namespace pd2::event_roll
