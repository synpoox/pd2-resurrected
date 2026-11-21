// The Invaders' legacy player art (invader_art.cpp, #1215).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks the legacy art path builders (DCC and the two COF builders); `report` gets one entry. Returns 1 when any is on.
auto InstallInvaderArt(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto InvaderArtCounters(char* out, size_t size) noexcept -> void;
