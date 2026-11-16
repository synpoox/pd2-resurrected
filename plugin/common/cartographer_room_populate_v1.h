// Copied from d2r-tools 5dc22af (plugins/cartographer/include/cartographer_room_populate_v1.h, #487), unchanged; #1032 (docs/cartographer/room-populate-service.md).
// cartographer.room-populate.v1, the part another plugin copies: the two structs and the names (issue #487). Nothing else is needed to acquire the service;
// contract (threads, flags, lifetime, versions): docs/cartographer/room-populate-service.md. New fields only ever go at the end.
#pragma once

#include <cstdint>

namespace carto {

inline constexpr const char* RoomPopulateServiceProvider = "cartographer";  // the loader plugin id
inline constexpr const char* RoomPopulateServiceName = "cartographer.room-populate.v1";
inline constexpr uint32_t RoomPopulateServiceVersion = 1;

// before() returns flags; this one skips the game's own populate of that room (its monsters). Cartographer's own spawns still happen.
inline constexpr uint32_t RoomPopulateSkipNative = 1;

// The callbacks a subscriber passes. Either may be null. `user` is passed back to both.
struct RoomPopulateCallbacksV1 {
	uint32_t structSize;  // sizeof(RoomPopulateCallbacksV1), set by the caller
	uint32_t(__cdecl* before)(void* game, void* room, int32_t levelId, void* user) noexcept;            // before the game's populate; returns flags
	void(__cdecl* after)(void* game, void* room, int32_t levelId, bool skipped, void* user) noexcept;   // after it (and after the Cartographer work)
};

// The function table a consumer acquires: provider "cartographer", service "cartographer.room-populate.v1", version 1.
struct RoomPopulateServiceV1 {
	uint32_t serviceSize;     // sizeof(RoomPopulateServiceV1)
	uint32_t serviceVersion;  // 1
	// Returns false (handle untouched) for a null pluginId, callbacks or handle, or a callbacks->structSize below sizeof(RoomPopulateCallbacksV1).
	bool(__cdecl* subscribe)(const char* pluginId, const RoomPopulateCallbacksV1* callbacks, void* user, uint32_t* handle) noexcept;
	bool(__cdecl* unsubscribe)(uint32_t handle) noexcept;  // false for an unknown handle
};

}  // namespace carto
