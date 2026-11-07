// The legacy palette of the level entered (#296). D2R loads a level's palette (levels.txt Pal) only when a load screen
// ends, so a level whose Pal differs from the one before it, entered without a load screen, draws in the wrong
// palette: PD2's maps (Act 5 levels with Act 2-4 tiles, entered by portal from Harrogath) show red and cyan noise.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace level_palette {

// Reload only when the palette changes: the first level of a game had its load screen.
constexpr auto NeedsLoad(int32_t previousLevel, uint8_t previousPal, uint8_t pal) noexcept -> bool {
	return previousLevel > 0 && previousPal != pal;
}

// Listens for level changes on the UI thread. Returns the report for the log.
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

void Counters(char* out, size_t size) noexcept;

}  // namespace level_palette
