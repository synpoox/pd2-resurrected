// Grows D2R's Entity System Pool and Renderer Pool so PD2's large map presets fit (LP3, https://github.com/synpoox/pd2-resurrected/issues/213).
#pragma once

#include <cstddef>

// Adds one extra pool to each allocator once D2R has made it (a waiting thread).
auto InstallEntityPool(char* report, size_t reportSize) noexcept -> void;
// What happened to each: on with the size, off with why, or still waiting.
auto EntityPoolState() noexcept -> const char*;
