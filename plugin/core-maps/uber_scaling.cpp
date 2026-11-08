// PD2's uber tier scaling, state `uberscaling` (ProjectDiablo.dll 0x10381160, from 0x10371a70; docs/research/
// pd2-uber-bosses-dll.md, section 4). PD2's monster spawn hook runs it after the map stats (0x1038af90) on every
// monster spawning in a PD2 level, so it rides map_stats' spawn hook: no SDK service changes a spawning monster's
// stats. Only the 30 monsters of the uber fights (PD2's set 0x105def0c) are scaled, from the open fight's tier (key
// `UBRT`, PD2 game +0x2629) and entries (`UBRE`, +0x2628), and state `uberscaling` marks them so none is scaled twice.
#include "uber_scaling.h"

#include "log.h"

#include <algorithm>
#include <cstdint>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// PD2's 0x105def0c, in the port's monstats ids (PD2's + 17): Dclone, his skeletons and trapped souls, Rathma and
// Mendeln with their clones, golems, totem, spire and void knights, Lucion with his controller, adds and spawner
constexpr int32_t FightMonsters[] = {806, 807,  808,  809,  810,  811,  903,  904,  905,  906,
                                     907, 908,  950,  951,  952,  953,  954,  955,  956,  983,
                                     985, 986,  1123, 1124, 1129, 1130, 1131, 1132, 1157, 1158};
// PD2's 0x105deec4: Dclone, Rathma, Mendeln, their clones, Lucion
constexpr int32_t Bosses[] = {806, 950, 951, 952, 953, 1129};
constexpr int32_t ScalingState = 287;          // uberscaling (PD2 241)
constexpr uint32_t TierKey = 0x55425254;       // 'UBRT', as map_open.cpp
constexpr uint32_t EntryKey = 0x55425245;      // 'UBRE', as map_open.cpp
constexpr int32_t AllSkills = 127, FireAbsorb = 142, LightAbsorb = 144, MagicAbsorb = 146, ColdAbsorb = 148;
constexpr int32_t DamageResist = 36, PoisonLengthResist = 110;

auto CounterValue(Game* game, uint32_t key) noexcept -> int32_t {
	const int32_t* v = d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
	return v != nullptr ? *v : 0;
}

auto In(const int32_t* first, const int32_t* last, int32_t id) noexcept -> bool {
	return std::find(first, last, id) != last;
}

// PD2's maxhp: x1.5 at tier 0; at tiers 1-2 x4 (bosses) or x3, then + tier * trunc(that * 20 / 100); capped at
// 0x7fffffff. The arithmetic is PD2's (64-bit product, a double for the 20%).
constexpr auto ScaledMaxLife(uint32_t maxhp, int32_t tier, bool boss) noexcept -> int32_t {
	const uint64_t p = tier < 1 ? static_cast<uint64_t>(maxhp) * 3 / 2 : static_cast<uint64_t>(maxhp) * (boss ? 4 : 3);
	const double share = static_cast<double>(static_cast<int32_t>(p)) * 20.0 / 100.0;
	const int32_t q = share <= 2147483647.0 ? static_cast<int32_t>(share) : 0x7fffffff;
	const int64_t scaled = static_cast<int64_t>(p) + static_cast<int64_t>(q) * tier;
	return scaled < 0 || scaled > 0x7fffffff ? 0x7fffffff : static_cast<int32_t>(scaled);
}
static_assert(ScaledMaxLife(1000, 0, true) == 1500 && ScaledMaxLife(1001, 0, false) == 1501);
static_assert(ScaledMaxLife(1000, 1, true) == 4800 && ScaledMaxLife(1000, 2, true) == 5600);
static_assert(ScaledMaxLife(1000, 1, false) == 3600 && ScaledMaxLife(1000, 2, false) == 4200);
static_assert(ScaledMaxLife(0x7fffffff, 2, true) == 0x7fffffff);

}  // namespace

auto ScaleUberMonster(Game* game, Unit* monster) noexcept -> void {
	using namespace d2r::rva;
	const int32_t classId = At<int32_t>(monster, UnitClassId);
	if (!In(std::begin(FightMonsters), std::end(FightMonsters), classId) ||
	    d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(monster, ScalingState) != 0) {
		return;
	}
	const int32_t id = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitId)(monster, __FILE__, __LINE__);
	void* list = d2r::Engine<void* (*)(int32_t, int32_t, int32_t, int32_t) noexcept>(NewStatList)(
		0, 0, At<int32_t>(monster, UnitType), id);
	if (list == nullptr) {
		return;
	}
	const bool boss = In(std::begin(Bosses), std::end(Bosses), classId);
	const int32_t tier = CounterValue(game, TierKey);
	const int32_t entries = CounterValue(game, EntryKey) & 0xff;  // PD2 reads the byte
	const int32_t e = entries >= 1 ? entries - 1 : 0;
	d2r::Engine<void (*)(void*, int32_t) noexcept>(SetListState)(list, ScalingState);
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const auto setUnit = d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat);
	const auto set = [list, flag = d2r::DataFlag(game)](int32_t s, int32_t v) noexcept {
		d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat)(flag, list, s, v, 0);
	};
	const int32_t maxhp = stat(monster, StatMaxLife, 0);
	const int32_t life = stat(monster, StatLife, 0);
	const int32_t scaled = ScaledMaxLife(static_cast<uint32_t>(maxhp), tier, boss);
	set(AllSkills, tier * 4);
	if (boss) {
		for (const int32_t absorb : {FireAbsorb, ColdAbsorb, LightAbsorb}) {
			set(absorb, 7 * tier + 2 * e);
		}
		set(MagicAbsorb, 3 * tier + 2 * e);
		set(DamageResist, 3 * e);
		set(PoisonLengthResist, 3 * tier + e);  // the DLL's e, not the 3e section 4 had
	}
	setUnit(monster, StatMaxLife, scaled, 0);
	// shortcut: a full-life monster stays full; PD2's next call (0x1030bd80, a D2Common thunk) is not identified
	if (life >= maxhp) {
		setUnit(monster, StatLife, scaled, 0);
	}
	d2r::Engine<void (*)(Unit*, void*, int32_t) noexcept>(AttachStatList)(monster, list, 1);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateBit)(monster, ScalingState, 1);  // AttachStatList does not (#630)
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateGfxFlag)(monster, ScalingState, 1);
	plog::Info("uber scaling: monster %d (unit %d) in level %d, tier %d, entries %d%s: maxhp %d -> %d, life %d -> %d, "
	           "skills +%d, absorbs %d/%d, dr %d, plr %d",
	           classId, id, d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(
	               d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(monster)),
	           tier, entries, boss ? " (boss)" : "", maxhp >> 8, stat(monster, StatMaxLife, 0) >> 8, life >> 8,
	           stat(monster, StatLife, 0) >> 8, stat(monster, AllSkills, 0), stat(monster, FireAbsorb, 0),
	           stat(monster, MagicAbsorb, 0), stat(monster, DamageResist, 0), stat(monster, PoisonLengthResist, 0));
}
