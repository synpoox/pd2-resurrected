// cartographer.open-extra.v1 (issue #485): the part another plugin copies. Contract: docs/cartographer/open-extra-service.md. New fields only ever go at the end.
#pragma once

#include <cstdint>

namespace carto {

inline constexpr const char* OpenExtraServiceProvider = "cartographer";
inline constexpr const char* OpenExtraServiceName = "cartographer.open-extra.v1";
inline constexpr uint32_t OpenExtraServiceVersion = 1;

struct OpenExtraResultV1 {
	uint32_t structSize;   // the caller sets sizeof(OpenExtraResultV1)
	int32_t level;         // the destination slot level when the portal opened, else -1
	int32_t monsterLevel;  // the destination's monster level, else 0
	char why[256];         // empty when opened, else the refusal sentence (also logged)
};

struct OpenExtraServiceV1 {
	uint32_t serviceSize;     // sizeof(OpenExtraServiceV1)
	uint32_t serviceVersion;  // 1
	// Game thread only. `player`: the player the portal is opened for; `nearUnit`: the unit to open it beside (may be `player`);
	// `sourceLevel`: the level the caller stands in (a slot level with a map open in this game). True when a portal opened.
	bool(__cdecl* openExtra)(void* game, void* player, void* nearUnit, int32_t sourceLevel, OpenExtraResultV1* out) noexcept;
};

}  // namespace carto
