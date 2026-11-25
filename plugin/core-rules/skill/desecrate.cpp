// desecrate
#include "common.h"
#include "registry.h"

#include "ai_registry.h"
#include "../item_rules.h"
#include "line_spread.h"
#include "log.h"
#include "../missile_functions.h"
#include "parry_roll.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"
#include "skill_router.h"
#include "../skill_functions.h"
#include "../core-events/arena_spots.h"

#include <D2RLPlugin/mutations.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <map>
#include <mutex>
#include <span>

using namespace pd2r::skills;

namespace pd2r::skills {

// --- Desecrate (PD2 srvdofunc 161 -> router 190, cltdofunc 101 -> 121) -------------------------------------------
// Read from ProjectDiablo.dll: srvdo 161 at 0x103b1fa0, cltdo 101 at 0x103ab630 (docs/research/skills/necromancer-w13.md).
std::atomic<uint32_t> g_desecrates {0};

std::atomic<uint32_t> g_desecrateClients {0};

std::atomic<uint32_t> g_desecrateHidden {0};


}  // namespace pd2r::skills

namespace {

constexpr int32_t Desecrate = 83;

constexpr int32_t CorpseFrames = 400;  // PD2's timer: the corpse can't be used 16 seconds after it falls
// Monsters PD2 never raises, as merged monstats ids: its 40 hcIdx (ProjectDiablo.dll 0x10190990 fills the set
// 0x36d600 reads) and, by name, frogdemonThrone, firetowerMarket and every *Maus, *Rat, *Rat2 and void* monster.

// Monsters PD2 never raises, as merged monstats ids: its 40 hcIdx (ProjectDiablo.dll 0x10190990 fills the set
// 0x36d600 reads) and, by name, frogdemonThrone, firetowerMarket and every *Maus, *Rat, *Rat2 and void* monster.
constexpr int32_t NeverRaised[] = {247,  248,  249,  540,  541,  542,  841,  862,  863,  864,  865,  866,  867,
                                   868,  869,  933,  934,  957,  958,  959,  960,  963,  976,  985,  986,  987,
                                   988,  989,  990,  991,  992,  993,  994,  995,  996,  997,  998,  999,  1000,
                                   1001, 1002, 1003, 1123, 1124, 1159, 1160, 1161, 1162, 1163, 1164, 1180};
// Where PD2 picks monsters when the cast level has none: Black Marsh, a Tal Rasha's Tomb, Spider Forest, City of the
// Damned, the Worldstone Chamber

// Where PD2 picks monsters when the cast level has none: Black Marsh, a Tal Rasha's Tomb, Spider Forest, City of the
// Damned, the Worldstone Chamber
constexpr int32_t FallbackLevels[] = {6, 66, 76, 106, 132};

constexpr int ListSize = 25;

// PD2's list for a level: its 25 monsters (nmon in nightmare and hell), -1 for each one Desecrate can't raise.

// PD2's list for a level: its 25 monsters (nmon in nightmare and hell), -1 for each one Desecrate can't raise.
auto RaiseList(uint32_t dataFlag, const uint8_t* tables, int32_t levelId, bool normal, int16_t* list) noexcept -> bool {
	using namespace d2r::rva;
	if (levelId < 0 || static_cast<uint32_t>(levelId) >= At<uint32_t>(tables, TablesLevelCount)) {
		return false;
	}
	const uint8_t* level = At<const uint8_t*>(tables, TablesLevels) + static_cast<int64_t>(levelId) * LevelRecordSize;
	bool any = false, ended = false;
	for (int i = 0; i < ListSize; ++i) {
		int16_t mon = ended ? -1 : At<int16_t>(level, (normal ? LvlMon1 : LvlNMon1) + i * 2);
		ended = ended || mon < 0;
		const uint8_t* stats = mon < 0 ? nullptr
		                               : d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
		                                     dataFlag, mon);
		if (stats == nullptr || At<uint8_t>(stats, MonPartyMin) > 0 || At<uint8_t>(stats, MonPartyMax) > 0 ||
		    At<int16_t>(stats, MonSpawn) != -1 ||
		    std::find(std::begin(NeverRaised), std::end(NeverRaised), mon) != std::end(NeverRaised)) {
			mon = -1;
		}
		list[i] = mon;
		any = any || mon >= 0;
	}
	return any;
}

// The corpse's timer ran out: hide it (PD2's callback 0x1036c6d0 sets corpse_nodraw). The list never set the state, so
// the expiry call only does D2R's list bookkeeping. corpse_nodraw is then a bare state: SetStateBit (the bit
// UnitHasState reads), then SetStateGfxFlag, as D2R's 0x56bfe0 (#623).

// The corpse's timer ran out: hide it (PD2's callback 0x1036c6d0 sets corpse_nodraw). The list never set the state, so
// the expiry call only does D2R's list bookkeeping. corpse_nodraw is then a bare state: SetStateBit (the bit
// UnitHasState reads), then SetStateGfxFlag, as D2R's 0x56bfe0 (#623).
void HideCorpse(void* list, Unit* unit, int32_t state) noexcept {
	d2r::Engine<void (*)(void*, Unit*, int32_t) noexcept>(d2r::rva::SkillStateExpired)(list, unit, state);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::SetStateBit)(unit, CorpseNoDraw, 1);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::SetStateGfxFlag)(unit, CorpseNoDraw, 1);
	g_desecrateHidden.fetch_add(1);
}

// calc1 corpses of monsters from the target point's level, placed around the point, owned by the caster, each hidden
// after CorpseFrames; then srvmissilea at the point. PD2 evaluates petmax too and throws it away; so does this.
// shortcut: PD2's timer is a game event; this is a state list on the corpse with HideCorpse as its expiry. Upgrade if
// the game check shows corpses that never hide (docs/deviations.md).

// calc1 corpses of monsters from the target point's level, placed around the point, owned by the caster, each hidden
// after CorpseFrames; then srvmissilea at the point. PD2 evaluates petmax too and throws it away; so does this.
// shortcut: PD2's timer is a game event; this is a state list on the corpse with HideCorpse as its expiry. Upgrade if
// the game check shows corpses that never hide (docs/deviations.md).
auto SrvDoDesecrate(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
	int32_t x = 0, y = 0;
	if (skill == nullptr || tables == nullptr || d2r::api.skillTargetXY(game, unit, &x, &y) == 0) {
		return 0;
	}
	void* room = d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAtXY)(
		d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit), x, y);
	if (room == nullptr) {
		return 0;
	}
	const int32_t levelId = d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room);
	const bool normal = At<uint8_t>(game, GameDifficulty) == 0;
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	int16_t list[ListSize];
	// PD2 retries random fallback levels until one has a monster; bounded here so bad data can't hang the server
	bool any = RaiseList(dataFlag, tables, levelId, normal, list);
	for (int tries = 0; !any && tries < 32; ++tries) {
		any = RaiseList(dataFlag, tables, FallbackLevels[d2r::RandomBelow(seed, static_cast<uint32_t>(std::size(FallbackLevels)))], normal, list);
	}
	if (!any) {
		if (static std::atomic<bool> logged {false}; !logged.exchange(true)) {  // bad table data repeats every cast
			plog::Warn("desecrate: no monster to raise for level %d", levelId);
		}
		return 0;
	}
	const int32_t count = ClampCount(d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level));
	const int32_t mode = At<uint8_t>(skill, SkSumMode);
	const uint32_t casterId = At<uint32_t>(unit, UnitIdField);
	const auto spawn = d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int16_t) noexcept>(
		SpawnMonster);
	auto* regions = At<uint8_t**>(game, GameMonRegions);
	if (levelId < 0 || static_cast<uint32_t>(levelId) >= At<uint32_t>(tables, TablesLevelCount)) {
		regions = nullptr;
	}
	int32_t created = 0, sx = x, sy = y;
	for (int32_t i = 0; i < count; ++i) {
		int16_t mon = -1;
		while (mon < 0) {
			mon = list[d2r::RandomBelow(seed, ListSize)];
		}
		if (i == 1) {
			sx += 2, sy += 2;
		} else if (i == 2) {
			sx -= 2, sy += 2;
		} else if (i > 2) {
			sx += static_cast<int32_t>(d2r::RandomBelow(seed, 2)) - 2;
			sy += static_cast<int32_t>(d2r::RandomBelow(seed, 2)) - 2;
		}
		Unit* corpse = spawn(game, room, sx, sy, mon, mode, -1, 0x42);
		if (corpse == nullptr) {
			break;  // PD2 stops the cast here, before the cloud
		}
		++created;
		*reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(corpse) + UnitOwnerType) = 0;
		*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(corpse) + UnitOwnerId) = casterId;
		// PD2 takes the corpse back out of its level's spawn count, so it doesn't stop the level spawning
		if (regions != nullptr && regions[levelId] != nullptr) {
			*reinterpret_cast<int32_t*>(regions[levelId] + MonRegionCount) -= 1;
		}
		d2r::StateListRequest request {corpse, corpse, skillId, level, CorpseFrames, -1, 0, CorpseNoDraw,
		                               reinterpret_cast<void (*)()>(HideCorpse)};
		d2r::Engine<void* (*)(d2r::StateListRequest*) noexcept>(NewStateList)(&request);
	}
	bool cloud = false;
	if (created == count) {
		d2r::MissileParams params;
		std::memset(&params, 0, sizeof(params));
		params.flags = 1;  // placed at x/y
		params.owner = unit;
		params.missile = At<int16_t>(skill, SkSrvMissileA);
		params.x = params.targetX = x;
		params.y = params.targetY = y;
		params.skill = skillId;
		params.skillLevel = level;
		cloud = d2r::ValidMissile(dataFlag, params.missile) && d2r::api.createMissile(game, &params) != nullptr;
		d2r::api.setUnitSkillFlag(unit, 1);
	}
	++g_desecrates;
	return created == count ? 1 : 0;
}

// Client half (PD2 cltdofunc 101): cltmissile a, b and c at the target point.

// Client half (PD2 cltdofunc 101): cltmissile a, b and c at the target point.
auto CltDoDesecrate(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	int32_t x = 0, y = 0;
	if (skill == nullptr || d2r::api.clientSkillTargetXY(unit, &x, &y) == 0) {
		return 1;
	}
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 1;
	params.owner = unit;
	params.x = params.targetX = x;
	params.y = params.targetY = y;
	params.skill = skillId;
	params.skillLevel = level;
	int created = 0;
	for (int column = 0; column < 3; ++column) {
		params.missile = At<int16_t>(skill, d2r::rva::SkCltMissileA + column * 2);
		created += d2r::ValidMissile(dataFlag, params.missile) && d2r::api.clientCreateMissile(&params, 0) ? 1 : 0;
	}
	++g_desecrateClients;
	return 1;
}

// --- Melee splash (PD2 srvdofunc 163 -> router 190) ---------------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 163 at 0x103b2340, its per-unit callback 0x10303190; Revive's aura event 32
// (0x10347370) calls the do function itself (docs/research/skills/w33-melee-splash.md). Every unit within calc3 of the struck
// unit, the struck unit aside, takes a hit with no to-hit roll: a player's current skill's damage (W35) and the
// attacker's weapon damage at that skill's SrcDam, with half the life leech, through D2R's area search with PD2's unit
// mask. The radius is calc3 + the inc_splash_radius held by the weapon's item types / 20, 1 less on a normal-tier base.
// shortcut: Smite's splash takes Smite's skill damage, not its shield damage and state 224 list (0x103060c0), and the
// radius counts only the weapon's own item types, not PD2's off-hand rules for 1han / 2han (0x10381720); upgrade with
// the deviation row. PD2's room check (0x1037c820) refuses only PD2's PvP maps, cut.

}  // namespace

PD2R_SLOT(17, CltDo, 121, static_cast<CltDoFn>(CltDoDesecrate), "desecrate")
PD2R_ROUTE(9, Desecrate, SrvDoDesecrate)
