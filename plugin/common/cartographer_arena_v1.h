// Copied from d2r-tools 5a4dc4b (plugins/cartographer/include/cartographer_arena_v1.h, #488), unchanged; #958 (docs/cartographer/arena-service.md).
// cartographer.arena.v1, the part another plugin copies: the two structs and the names (issue #488). Nothing else is needed to acquire the service;
// contract (threads, versions, when to acquire): docs/cartographer/arena-service.md. New fields only ever go at the end.
#pragma once

#include <cstdint>

namespace carto {

inline constexpr const char* ArenaServiceProvider = "cartographer";  // the loader plugin id
inline constexpr const char* ArenaServiceName = "cartographer.arena.v1";
inline constexpr uint32_t ArenaServiceVersion = 1;

// What findSpot fills in. `structSize` is set by the caller to sizeof(ArenaSpotV1); new fields only ever go at the end.
struct ArenaSpotV1 {
	uint32_t structSize;
	int32_t subX, subY;                          // the spot, absolute subtiles (level origin and ring included)
	int32_t roomX, roomY, roomW, roomH;          // the room that holds it, absolute tiles
};

// The function table a consumer acquires: provider "cartographer", service "cartographer.arena.v1", version 1.
struct ArenaServiceV1 {
	uint32_t serviceSize;     // sizeof(ArenaServiceV1)
	uint32_t serviceVersion;  // 1
	bool(__cdecl* isArena)(int32_t levelId) noexcept;
	bool(__cdecl* findSpot)(int32_t levelId, const char* name, ArenaSpotV1* out) noexcept;
};

}  // namespace carto
