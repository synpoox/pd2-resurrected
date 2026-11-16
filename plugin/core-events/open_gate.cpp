// What the map opening and the uber entry share, split out of map_open.cpp (#943), unchanged (open_gate.h).
#include "open_gate.h"

#include "log.h"

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t UnitObject = 2;

std::atomic<uint32_t> g_openFailed {0};

auto OpenCounter(Game* game, uint32_t key) noexcept -> int32_t* {
	return d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

auto PlayerLevelId(Unit* player) noexcept -> int32_t {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(player);
	return room != nullptr ? d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room) : -1;
}

// Made as the portal maker 0x432ce0 makes one, but with no return portal; it finds the room holding (x, y) from `near`
auto OpenPortalAt(Game* game, void* near, int32_t row, int32_t x, int32_t y, int32_t level) noexcept -> Unit* {
	using namespace d2r::rva;
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ObjectRecord)(d2r::DataFlag(game),
	                                                                                            row);
	using SpawnFn = Unit* (*)(int32_t, int32_t, int32_t, int32_t, Game*, void*, int32_t, int32_t, int32_t,
	                          int32_t) noexcept;
	void* room = near != nullptr && record != nullptr
		? d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAtXY)(near, x, y)
		: nullptr;
	Unit* portal = room != nullptr ? d2r::Engine<SpawnFn>(SpawnUnit)(UnitObject, row, x, y, game, room, 1, 1, 0, 0)
	                               : nullptr;
	if (portal == nullptr) {
		plog::Warn("uber: no portal %d at (%d, %d)", row, x, y);
		return nullptr;
	}
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetObjectMode)(portal, 1);
	const int32_t open = At<int32_t>(game, GameFrame) + 1 + static_cast<int32_t>(At<uint32_t>(record, ObjOpenFrames) >> 8);
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(AddUnitEvent)(
		game, portal, UnitEventOpen, open, 0, 0, 0);
	// as the portal maker does: without the mark the room, portal and all, is freed once no player is near
	d2r::Engine<void (*)(void*, int32_t) noexcept>(RoomPortalMark)(room, 0);
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetPortalLevel)(portal, level);
	return portal;
}
