// Gold picks itself up near the player, as in PD2 (#1863).
//
// PD2's client (ProjectDiablo.dll 0x102ec230, every client frame from 0x102eb6d0) sends the pickup message 0x16 for the
// first gold item (code `gld `) with no player owner (1.13c unit +0x94 type != 0 or +0x98 id == 0) in the player's own
// room (its near-room loop exits after that first pass), not a town room (D2Common #10057), within distance 3 (D2Common #10522, the edge distance) while the
// player is alive (mode not 0 or 17), not uninterruptable (state 54, the same row in PD2's raw table and ours) and
// below the gold cap (stat 14 < D2Common #10049, 5,000,000 as PD2 patches it; here D2R's cap function 0x34b320, which gold_cap.cpp
// rewrites to return it, #2027). PD2's handler for 0x16 (0x10394370) then takes that
// gold at once up to distance 5 without the wall test. Its potion branch (item types 76-78) is not ported: the client
// never sends it from range, and live PD2 picks up only gold from range (owner, #1863).
//
// Here the server does the scan: D2R's client walks to the item before it sends a pickup, so the scan sits on D2R's
// per-frame player regen event (0x42e600, every player, joined clients too) and takes the gold through D2R's own ground
// pickup (0x471950) with no range or wall test, as PD2's handler does. No SDK service covers ground items or a
// per-frame player tick, hence the hook; the distance (0x324fc0, #10522) and the room's unit walk (0x471950's own
// calls) are found by signature.
#include "item_rules.h"
#include "skill/common.h"

#include "pd2_engine.h"
#include "log.h"
#include "sig.h"

#include <D2RLPlugin/item.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t AutoRange = 3;                 // PD2's client scan: #10522 distance <= 3
constexpr int32_t StatGold = 14;
constexpr int32_t StateUninterruptable = 54;
constexpr int32_t ModeDeath = 0, ModeDead = 17;
const uint32_t GoldCode = D2RL::Items::MakeItemCode("gld");

// D2R's ground pickup: (player, item id, to cursor, max distance, wall test, 0) -> true when taken. Its entry also
// walks a room's units: the first-unit and next-unit calls are read from it.
constexpr const char* PickupPattern =
    "48 89 5c 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8d 6c 24 e9 48 81 ec a0 00 00 00 45 33 e4 44 88 45 c7 4c 89 65 "
    "ff 45 8b e9 44 89 65 cf 44 8b fa 4c 8b f1 89 55 f7 e8 ?? ?? ?? ?? 48 8b c8 4c 8d 45 cf 48 8d 55 ff e8 ?? ?? ?? ?? "
    "41 8b f4 41 8b fc 39 75 cf 0f 86 ?? ?? ?? ?? 0f 1f 44 00 00 48 8b 4d ff 8b c7 48 8b 0c c1 e8 ?? ?? ?? ?? 48 8b d8 "
    "48 85 c0 74 ?? 48 8b cb e8 ?? ?? ?? ?? 83 f8 04 75 ?? 41 b8 dd 04 00 00 48 8d 15 ?? ?? ?? ?? 48 8b cb e8 ?? ?? ?? "
    "?? 41 3b c7 48 0f 44 f3 48 8b cb e8";
constexpr uint32_t FirstUnitCall = 0x6a, NextUnitCall = 0xa3;
// The edge distance #10522 (0x324fc0, (unit, unit)): its size-halving tail, 0x113 into the function
constexpr const char* DistancePattern =
    "48 8b cb e8 ?? ?? ?? ?? 48 8b cf 8b d8 e8 ?? ?? ?? ?? 99 45 2b f7 2b c2 d1 f8 f7 d8 8b c8 8b c3 99 2b c2 d1 f8 2b c8";
constexpr uint32_t DistanceTail = 0x113;
constexpr uint8_t DistanceEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18};

using RegenFn = void (*)(Game*, Unit*, int32_t, int32_t) noexcept;
using PickupFn = bool (*)(Unit*, uint32_t, bool, int32_t, bool, bool) noexcept;
using UnitStepFn = Unit* (*)(void*) noexcept;
using DistanceFn = int32_t (*)(Unit*, Unit*) noexcept;
using GoldCapFn = int32_t (*)(Unit*) noexcept;
RegenFn g_regen = nullptr;
PickupFn g_pickup = nullptr;
UnitStepFn g_firstUnit = nullptr, g_nextUnit = nullptr;
DistanceFn g_distance = nullptr;
GoldCapFn g_goldCap = nullptr;  // D2R's max inventory gold (0x34b320), 5,000,000 once gold_cap.cpp's patch is on
std::atomic<uint32_t> g_taken {0};

auto CallTarget(uint64_t at) noexcept -> uint64_t {
	const auto* call = reinterpret_cast<const uint8_t*>(at);
	if (call[0] != 0xe8) {
		return 0;
	}
	int32_t rel = 0;
	std::memcpy(&rel, call + 1, sizeof(rel));
	return at + 5 + static_cast<int64_t>(rel);
}

auto IsTownLevel(int32_t level) noexcept -> bool {
	return level == 1 || level == 40 || level == 75 || level == 103 || level == 109;  // D2Common #10057's switch
}

auto IsFreeGold(Unit* unit) noexcept -> bool {
	if (At<int32_t>(unit, d2r::rva::UnitType) != 4 ||
	    (At<int32_t>(unit, d2r::rva::UnitOwnerType) == 0 && At<uint32_t>(unit, d2r::rva::UnitOwnerId) != 0)) {
		return false;
	}
	const uint8_t bank = d2r::Engine<uint8_t (*)(Unit*) noexcept>(d2r::sig::UnitDataFlag)(unit);
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::sig::ItemRecord)(
	    bank, At<int32_t>(unit, d2r::rva::UnitClassId));
	return record != nullptr && At<uint32_t>(record, d2r::rva::ItCode) == GoldCode;
}

void PickUpNearGold(Unit* player) noexcept {
	const int32_t mode = At<int32_t>(player, d2r::rva::UnitMode);
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat);
	if (mode == ModeDeath || mode == ModeDead ||
	    d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(d2r::sig::UnitHasState)(player, StateUninterruptable) != 0 ||
	    stat(player, StatGold, 0) >= g_goldCap(player)) {
		return;
	}
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::UnitRoom)(player);
	if (room == nullptr || IsTownLevel(d2r::Engine<int32_t (*)(void*) noexcept>(d2r::sig::RoomLevelId)(room))) {
		return;
	}
	for (Unit* unit = g_firstUnit(room); unit != nullptr; unit = g_nextUnit(unit)) {
		if (!IsFreeGold(unit) || g_distance(unit, player) > AutoRange) {
			continue;
		}
		const auto id = static_cast<uint32_t>(
		    d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(d2r::sig::UnitId)(unit, __FILE__, __LINE__));
		// One a frame, as PD2's scan sends one message: the pickup changes the room's unit list. D2R's pickup refuses
		// while an item is on the player's cursor; the scan then tries again each frame.
		if (g_pickup(player, id, false, 0x7fffffff, false, false)) {
			++g_taken;
		}
		return;
	}
}

void RegenTick(Game* game, Unit* player, int32_t a, int32_t b) noexcept {
	g_regen(game, player, a, b);
	if (player != nullptr && At<int32_t>(player, d2r::rva::UnitType) == 0) {
		PickUpNearGold(player);
	}
}

}  // namespace

auto InstallGoldPickup(const D2RL::PluginContext* context) noexcept -> bool {
	for (const sigscan::Entry* e : {&d2r::sig::UnitDataFlag, &d2r::sig::ItemRecord, &d2r::sig::UnitStat, &d2r::sig::UnitHasState,
	                                &d2r::sig::UnitRoom, &d2r::sig::RoomLevelId, &d2r::sig::UnitId, &d2r::sig::PlayerRegenEvent}) {
		if (e->state != sigscan::State::Ok) {
			return false;
		}
	}
	uint64_t pickup = 0, tail = 0;
	if (sigscan::ScanUnique(context->exeBase, PickupPattern, 0, &pickup) != 1 ||
	    sigscan::ScanUnique(context->exeBase, DistancePattern, 0, &tail) != 1 || tail < DistanceTail ||
	    std::memcmp(reinterpret_cast<const void*>(context->exeBase + tail - DistanceTail), DistanceEntry, sizeof(DistanceEntry)) != 0) {
		return false;
	}
	const uint64_t at = context->exeBase + pickup;
	g_firstUnit = reinterpret_cast<UnitStepFn>(CallTarget(at + FirstUnitCall));
	g_nextUnit = reinterpret_cast<UnitStepFn>(CallTarget(at + NextUnitCall));
	g_pickup = reinterpret_cast<PickupFn>(at);
	const uint64_t cap = GoldCapRva(context);
	g_goldCap = cap != 0 ? reinterpret_cast<GoldCapFn>(context->exeBase + cap) : nullptr;
	g_distance = reinterpret_cast<DistanceFn>(context->exeBase + tail - DistanceTail);
	return g_firstUnit != nullptr && g_nextUnit != nullptr && g_goldCap != nullptr &&
	       context->InstallInlineHook(d2r::sig::PlayerRegenEvent.rva, d2r::rva::PlayerRegenEventEntry,
	                                  sizeof(d2r::rva::PlayerRegenEventEntry), RegenTick, &g_regen);
}

auto GoldPickupCount() noexcept -> uint32_t {
	return g_taken.load();
}
