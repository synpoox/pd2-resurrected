// Log lines in hot paths (per hit, kill, drop, missile, arrow or frame) write nothing in normal play. D2RLoader writes
// each plugin line on the calling thread and flushes it to disk (~5 ms), so even a first-20 burst stalls a Multiple
// Shot volley (#1609). `debug_mode` (the console command that turns cheats on, D2RCore's IsDebugModeEnabled) turns
// the lines on for a game check: then the first 20, and one in 500 after.
#pragma once

#include <atomic>
#include <cstdint>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near  // windows.h's 16-bit leftovers: the plugins name variables near and far
#undef far
#include <D2RLPlugin/core_exports.h>

namespace pd2 {

// True while the loader's debug mode is on. The export is looked up once; false when D2RCore or the export is missing.
inline auto DebugOn() noexcept -> bool {
	static const auto fn = [] {
		const HMODULE core = GetModuleHandleA(D2RL::CoreExports::CoreDllName);
		return core ? reinterpret_cast<D2RL::CoreExports::IsDebugModeEnabledFn>(reinterpret_cast<void*>(
		                  GetProcAddress(core, D2RL::CoreExports::IsDebugModeEnabledInfo.name)))
		            : nullptr;
	}();
	return fn != nullptr && fn();
}

// Debug mode on, and the first 20 calls (the lines a game check reads), then one in 500.
inline auto HotSample(uint32_t n) noexcept -> bool {
	return (n <= 20 || n % 500 == 0) && DebugOn();
}

// Counts the call, then as above: the hot-path stand-in for plog::Sample(counter).
inline auto HotSample(std::atomic<uint32_t>& counter) noexcept -> bool {
	return HotSample(++counter);
}

}  // namespace pd2
