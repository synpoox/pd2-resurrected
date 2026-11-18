// Map boss loot follows the map tier (#1423): the treasure classes a map boss rolls, by the open map's tier.
// No engine types: tests/map_boss_tier_check.cpp checks it on the host.
#pragma once

#include <cstddef>
#include <cstring>

namespace pd2::mapboss {

constexpr int Tiers = 3;
// treasureclassex rows: a boss's TC cell names one of these (superuniques.txt TC, TC(N), TC(H) Desecrated: plain; TC(H): Blood)
constexpr const char* TcName[2][Tiers] = {{"Map Boss T1", "Map Boss T2", "Map Boss T3"},
                                          {"Map Boss T1 Blood", "Map Boss T2 Blood", "Map Boss T3 Blood"}};

// The tier a `configured` boss TC (a TcName) is rolled as in a map of `tier` (1-3). Any other name, or a tier outside 1-3: nullptr.
constexpr auto TcForTier(const char* configured, int tier) noexcept -> const char* {
	if (configured == nullptr || tier < 1 || tier > Tiers) {
		return nullptr;
	}
	for (const auto& family : TcName) {
		for (const char* name : family) {
			if (std::strcmp(name, configured) == 0) {
				return &family == &TcName[0] ? TcName[0][tier - 1] : TcName[1][tier - 1];
			}
		}
	}
	return nullptr;
}

}  // namespace pd2::mapboss
