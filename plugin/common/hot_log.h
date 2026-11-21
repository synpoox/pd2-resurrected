// Sampling for log lines in hot paths (per hit, kill, drop, missile or frame). D2RLoader writes each plugin line on the
// calling thread and flushes it to disk (~5 ms), so a line per hit stalls the game thread (#1609).
#pragma once

#include <cstdint>

namespace pd2 {

// The first 20 calls (the lines a game check reads), then one in 500.
constexpr auto HotSample(uint32_t n) noexcept -> bool {
	return n <= 20 || n % 500 == 0;
}

}  // namespace pd2
