// Log once per key (#2094): a line a retry loop reaches again and again (a tidy pass, a stuck item) is written the first time
// only. D2RLoader flushes each plugin line to disk (~5 ms) on the game thread, so a line per pass drops FPS. No engine types,
// so tests/log_once_check.cpp builds it on the host.
#pragma once

#include <cstddef>
#include <cstdint>

namespace pd2 {

// True the first time `key` is seen. Keeps the last 64 keys (the oldest is forgotten first, so a key may log again after
// 64 other keys): game thread only, one instance per call site.
class LogOnce {
public:
	auto First(uint64_t key) noexcept -> bool {
		for (const uint64_t seen : keys_) {
			if (seen == key) {
				return false;
			}
		}
		keys_[next_++ % Size] = key;
		return true;
	}

private:
	static constexpr size_t Size = 64;
	uint64_t keys_[Size] = {};  // shortcut: key 0 reads as seen at start, so callers never pass 0
	size_t next_ = 0;
};

constexpr auto Key(uint32_t a, uint32_t b) noexcept -> uint64_t {
	return (static_cast<uint64_t>(a) << 32) | b | (a == 0 && b == 0 ? 1u : 0u);
}

}  // namespace pd2
