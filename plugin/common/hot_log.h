// Log lines in hot paths (per hit, kill, drop, missile, arrow or frame) write nothing in normal play. D2RLoader writes
// each plugin line on the calling thread and flushes it to disk (~5 ms), so even a first-20 burst stalls a Multiple
// Shot volley (#1609). The environment variable PD2R_HOT_LOGS=1, set before the game starts, turns the lines on for a
// game check (not `debug_mode`: cheats turn that on): then the first 20, and one in 500 after.
#pragma once

#include <atomic>
#include <cstdint>

#include <cstdlib>

namespace pd2 {

// True when the game was started with PD2R_HOT_LOGS=1 (read once). Not `debug_mode`: cheats turn that on, and a cheat test
// (zombies by `cheat mspawn`) must run without the log stalls.
inline auto HotLogsOn() noexcept -> bool {
	static const bool on = [] {
		char v[8] = {};
		size_t n = 0;
		return getenv_s(&n, v, sizeof(v), "PD2R_HOT_LOGS") == 0 && n > 0 && v[0] == '1';
	}();
	return on;
}

// PD2R_HOT_LOGS=1, and the first 20 calls (the lines a game check reads), then one in 500.
inline auto HotSample(uint32_t n) noexcept -> bool {
	return (n <= 20 || n % 500 == 0) && HotLogsOn();
}

// Counts the call, then as above: the hot-path stand-in for plog::Sample(counter).
inline auto HotSample(std::atomic<uint32_t>& counter) noexcept -> bool {
	return HotSample(++counter);
}

}  // namespace pd2
