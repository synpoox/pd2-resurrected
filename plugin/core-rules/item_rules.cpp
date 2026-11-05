// PD2's item rules for Ormus' Robes (docs/research/p12-ormus-robes.md):
//
// - properties func 25 (`sorc-skill-rand-ctc`, ProjectDiablo.dll 0x103a12a0) in D2R's free property slot 26: one
//   random skill of PD2's sorceress list gets +param, and the item casts that skill on cast (item_skilloncast) at
//   chance min and level max, both adjusted by the skill as PD2 does.
// - skill on cast (item_skilloncast, PD2's itemeventfunc 33, 0x103473a0): after the server runs a skill a player
//   cast (mode SC; an inline hook on D2R's server skill dispatcher), each of the player's item_skilloncast listeners
//   rolls the stat's chance, then casts the layer's skill at the target, or at the caster's aim point. Every
//   cast-skill item works this way, not only Ormus' Robes. PD2 raises its dospellcast event for it; where was not
//   found, so this trigger is approximated (docs/unported.md).
// - curses (docs/research/ir4-curses.md): max curses (a target holds up to the caster's max_curses + 1 curses) and
//   curse resistance (curse_effectiveness on the target scales the curse's stats), around D2R's ApplyCurse.
#include "item_rules.h"

#include "d2r.h"
#include "log.h"

#include <atomic>
#include <cstdio>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// PD2's list 8 (built at 0x10206ba0): the sorceress skills func 25 draws from, in PD2's order, as merged ids (Ice
// Barrage 369 -> 441, Lesser Hydra 383 -> 455, Combustion 376 -> 448; merge/ids-skills.tsv)
constexpr int32_t SorceressSkills[] = {39, 45, 44, 55, 59, 441, 64, 38, 49, 48, 53, 43, 36, 47, 46, 51, 56, 455, 62, 448};
constexpr int PropertySlot = 26;           // PD2's func 25; D2R's 25 is affix-rand (tools/pd2_ids.py FUNCS)
constexpr int32_t StatSingleSkill = 107;   // item_singleskill
constexpr int32_t StatSkillOnCast = 378;   // item_skilloncast, merged id (PD2 200, merge/ids-itemstatcost.tsv)
constexpr int32_t ItemTargetSelf = 5;      // ItemTarget of Nova and Frost Nova: PD2 casts them at the aim point

std::atomic<uint32_t> g_rolls {0};
std::atomic<uint32_t> g_raises {0};
std::atomic<uint32_t> g_procRolls {0};
std::atomic<uint32_t> g_procs {0};

using SetStatFn = int32_t (*)(uint8_t dataFlag, uint32_t a1, uint64_t a2, Unit* item, const void* property, uint8_t set,
                              uint16_t stat, uint16_t layer, uint32_t value, uint32_t a8, uint32_t a9,
                              uint64_t a10) noexcept;

auto DataTables(uint32_t dataFlag) noexcept -> const uint8_t* {
	return d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(d2r::rva::DataTables)(dataFlag);
}

// The skill / level split of a CTC layer, from the data D2R's own item events read it from
auto LayerShift(uint32_t dataFlag) noexcept -> uint32_t {
	return At<uint32_t>(DataTables(dataFlag), d2r::rva::TablesSkillLayerShift) & 0x1f;
}
auto LayerMask(uint32_t dataFlag) noexcept -> uint32_t {
	return At<uint32_t>(DataTables(dataFlag), d2r::rva::TablesSkillLayerMask);
}

// PD2's func 25 (0x103a12a0). Property: int16 code, int16 kind, int32 param (+4), min (+8), max (+0xc).
auto PropSorcSkillRandCtc(uint8_t dataFlag, uint32_t a1, uint64_t a2, Unit* item, const void* property, uint8_t set,
                          uint16_t stat, uint16_t val, int32_t first, uint32_t a8, uint32_t a9, uint64_t a10) noexcept
	-> int32_t {
	(void)stat;
	(void)val;
	(void)first;
	if (property == nullptr || item == nullptr) {
		return 0;
	}
	using namespace d2r::rva;
	constexpr int32_t count = static_cast<int32_t>(std::size(SorceressSkills));
	const int32_t pick = static_cast<int32_t>(
		d2r::Engine<uint64_t (*)(Unit*, int32_t, int32_t) noexcept>(ItemRandom)(item, 0, count - 1));
	const int32_t skill = SorceressSkills[pick >= 0 && pick < count ? pick : 0];
	const int32_t bonus = At<int32_t>(property, 4);
	int32_t chance = At<int32_t>(property, 8);
	int32_t level = At<int32_t>(property, 0xc);
	if (const uint8_t* record = d2r::api.getSkillRecord(dataFlag, skill)) {
		if (At<int32_t>(record, SkLocalDelay) > 0) {  // a skill with a cooldown procs at 67% of the chance
			chance = static_cast<int32_t>(static_cast<double>(chance) * 67.0 / 100.0);
		}
		level -= (At<uint16_t>(record, SkReqLevel) - 6) / 6;  // a later skill procs at a lower level
		level = level < 1 ? 1 : level;
	}
	const auto setStat = d2r::Engine<SetStatFn>(PropertySetStat);
	const int32_t result = setStat(dataFlag, a1, a2, item, property, set, StatSingleSkill, static_cast<uint16_t>(skill),
	                               static_cast<uint32_t>(bonus), a8, a9, a10);
	const uint32_t layer = (static_cast<uint32_t>(skill) << LayerShift(dataFlag)) |
	                       (static_cast<uint32_t>(level) & LayerMask(dataFlag));
	setStat(dataFlag, a1, a2, item, property, set, StatSkillOnCast, static_cast<uint16_t>(layer),
	        static_cast<uint32_t>(chance), a8, a9, a10);
	const uint32_t n = ++g_rolls;
	plog::Info("ormus: rolled skill %d +%d, cast on cast %d%% level %d (%u so far)", skill, bonus, chance, level, n);
	return result;
}

// PD2's itemeventfunc 33 (0x103473a0), called with the listener's stat << 16 | layer
auto ItemEventSkillOnCast(Game* game, int32_t event, Unit* unit, Unit* target, void* damage, uint32_t statLayer,
                          int32_t unused) noexcept -> int32_t {
	(void)event;
	(void)damage;
	(void)unused;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	using namespace d2r::rva;
	const uint32_t layer = statLayer & 0xffff;
	const int32_t chance = d2r::Engine<int32_t (*)(Unit*, uint32_t, uint32_t) noexcept>(UnitLayerStat)(
		unit, statLayer >> 16, layer);
	if (chance < 1) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const int32_t skill = static_cast<int32_t>(layer >> LayerShift(dataFlag));
	const int32_t level = static_cast<int32_t>(layer & LayerMask(dataFlag));
	auto* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	const int32_t roll = static_cast<int32_t>(d2r::RandomBelow(seed, 100));
	++g_procRolls;
	const uint8_t* record = roll < chance ? d2r::api.getSkillRecord(dataFlag, skill) : nullptr;
	if (record == nullptr) {
		return 0;
	}
	const auto setFlag = d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(SetUnitFlag);
	setFlag(unit, UnitFlagEventCast, 1);  // no dospellcast from the proc itself (SkillDispatch)
	int64_t cast = 0;
	const bool atTarget = target != nullptr && At<uint8_t>(record, SkItemTarget) != ItemTargetSelf;
	if (atTarget) {
		cast = d2r::Engine<int64_t (*)(Unit*, int32_t, int32_t, Unit*, int32_t) noexcept>(CastEventSkill)(
			unit, skill, level, target, 1);
	} else {
		int32_t x = 0, y = 0;
		if (d2r::api.skillTargetXY(game, unit, &x, &y) != 0 || d2r::UnitXY(unit, &x, &y)) {
			cast = d2r::Engine<int64_t (*)(Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(
				CastEventSkillAt)(unit, skill, level, x, y, 0);
		}
	}
	setFlag(unit, UnitFlagEventCast, 0);
	const uint32_t n = ++g_procs;
	if (n <= 20 || n % 100 == 0) {
		plog::Info("skill on cast: %d%% (roll %d) skill %d level %d at %s: %lld, %u so far", chance, roll, skill, level,
		           atTarget ? "target" : "point", static_cast<long long>(cast), n);
	}
	return 1;
}

// item_skilloncast's itemevent1 (dospellcast, 18 in the merged events.txt), read from the stat's record so a
// renumbered events.txt keeps working; -1 when the row has none
auto SpellCastEvent(uint32_t dataFlag) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint8_t* tables = DataTables(dataFlag);
	if (tables == nullptr || static_cast<uint64_t>(StatSkillOnCast) >= At<uint64_t>(tables, TablesItemStatCostCount)) {
		return -1;
	}
	const uint8_t* records = At<const uint8_t*>(tables, TablesItemStatCost);
	return records != nullptr ? At<int16_t>(records + StatSkillOnCast * IscRecordSize, IscItemEvent1) : -1;
}

// The caster's registered event listeners (unit +0xe0, D2R's own list: equipping an item adds its stat listeners
// there). PD2 raises dospellcast and D2R would call each listener's
// itemeventfunc, but D2RCore routes item listeners through its own copy of the function table, which a plugin cannot
// reach; so this calls PD2's event for each item_skilloncast listener itself.
auto CastOnCastListeners(Game* game, Unit* unit, Unit* target, int32_t event) noexcept -> void {
	using namespace d2r::rva;
	int guard = 0;  // a unit holds a few dozen listeners; 256 only stops a corrupt list from spinning
	for (const uint8_t* node = At<const uint8_t*>(unit, UnitEventList); node != nullptr && guard < 256;
	     node = At<const uint8_t*>(node, EventNodeNext), ++guard) {
		if (At<int32_t>(node, EventNodeEvent) == event && At<int32_t>(node, EventNodeKind) == EventNodeKindItem &&
		    At<uint32_t>(node, EventNodeStat) == static_cast<uint32_t>(StatSkillOnCast)) {
			ItemEventSkillOnCast(game, event, unit, target, nullptr,
			                     (static_cast<uint32_t>(StatSkillOnCast) << 16) | (At<uint32_t>(node, EventNodeLayer) & 0xffff), 0);
		}
	}
}

using DispatchFn = uint64_t (*)(Game* game, Unit* unit, int32_t skillId, int32_t level, int32_t a5, int32_t a6,
                                int32_t a7) noexcept;
DispatchFn g_dispatchOriginal = nullptr;

// D2R's server skill dispatcher, then dospellcast on a player who just cast (mode SC) a skill by hand.
// shortcut: PD2's own raise site was not found; upgrade if it turns up (docs/research/p12-ormus-robes.md)
auto SkillDispatch(Game* game, Unit* unit, int32_t skillId, int32_t level, int32_t a5, int32_t a6, int32_t a7) noexcept
	-> uint64_t {
	const uint64_t ran = g_dispatchOriginal(game, unit, skillId, level, a5, a6, a7);
	using namespace d2r::rva;
	if (ran == 0 || game == nullptr || unit == nullptr || At<int32_t>(unit, UnitType) != 0 ||
	    At<int32_t>(unit, UnitMode) != PlayerModeCast || (At<uint32_t>(unit, UnitFlags) & UnitFlagEventCast) != 0) {
		return ran;
	}
	const int32_t event = SpellCastEvent(d2r::DataFlag(game));
	if (event >= 0) {
		CastOnCastListeners(game, unit, d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit), event);
		if (plog::Sample(g_raises)) {
			plog::Info("dospellcast: event %d after skill %d level %d, %u so far", event, skillId, level, g_raises.load());
		}
	}
	return ran;
}

// --- Curses: curse resistance and max curses (docs/research/ir4-curses.md) ---------------------------------------
constexpr int32_t StatCurseSlots = 374;          // curse_slots, merged id (PD2 190): the slot count a curse was cast with
constexpr int32_t StatMaxCurses = 421;           // max_curses (PD2 368)
constexpr int32_t StatCurseResistance = 557;     // curse_effectiveness, shown as Curse Resistance (PD2 504)
constexpr int32_t CurseResistanceCap = 75;
constexpr int MaxCurseLists = 64;  // a unit holds a handful; more only stops a corrupt chain from spinning

std::atomic<uint32_t> g_curses {0};
std::atomic<uint32_t> g_curseResisted {0};

// PD2's 0x1037c4c0 as its curse code calls it: value * (100 - resistance) / 100, truncated
auto Resisted(int32_t value, int32_t resistance) noexcept -> int32_t {
	const double v = static_cast<double>(value) * (100.0 - resistance) / 100.0;
	return v > 2147483647.0 ? 2147483647 : static_cast<int32_t>(v);
}

auto StatOf(Unit* unit, int32_t stat) noexcept -> int32_t {
	return unit != nullptr ? d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(unit, stat, 0)
	                       : 0;
}

// PD2's slot rule (ProjectDiablo.dll 0x1036d7d0) before D2R's ApplyCurse runs. D2R's NewStateList removes the first
// curse list it finds, so every curse list that must stay loses its curse flag until ApplyCurse returns (`hidden`).
// Returns the caster's slot count, which the new curse keeps (stat curse_slots).
auto MakeRoom(uint32_t dataFlag, Unit* caster, Unit* target, int32_t state, uint8_t* (&hidden)[MaxCurseLists],
              int& hiddenCount) noexcept -> int32_t {
	using namespace d2r::rva;
	const auto listStat = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(ListStatValue);
	const auto setStat = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	const int32_t own = StatOf(caster, StatMaxCurses) + 1;
	const uint8_t* owner = At<const uint8_t*>(target, UnitListOwner);
	if (owner == nullptr || At<int32_t>(owner, ListOwnerMark) >= 0) {
		return own;
	}
	// The most slots any curse on the target was cast with; a curse cast with fewer drops its count (PD2 does so)
	int32_t slots = own;
	int count = 0;
	uint8_t* oldest = nullptr;  // the last timed curse in the chain, the one a full target loses
	int guard = 0;
	for (uint8_t* list = At<uint8_t*>(owner, ListOwnerFirst); list != nullptr && guard < MaxCurseLists;
	     list = At<uint8_t*>(list, ListNext), ++guard) {
		if ((At<uint32_t>(list, ListFlags) & ListFlagCurse) == 0) {
			continue;
		}
		++count;
		const int32_t kept = listStat(dataFlag, list, StatCurseSlots, 0);
		if (kept != 0 && kept < slots) {
			setStat(dataFlag, list, StatCurseSlots, 0, 0);
		} else if (kept > slots) {
			slots = kept;
		}
		if ((At<uint32_t>(list, ListFlags) & ListFlagTimed) != 0) {
			oldest = list;
		}
	}
	// The one curse list NewStateList may see: this state's own (it refreshes or replaces it), else none while a slot
	// is free, else the oldest timed curse
	uint8_t* same = d2r::Engine<uint8_t* (*)(Unit*, int32_t) noexcept>(UnitStateList)(target, state);
	uint8_t* seen = same != nullptr ? same : count < slots ? nullptr : oldest;
	guard = 0;
	for (uint8_t* list = At<uint8_t*>(owner, ListOwnerFirst); list != nullptr && guard < MaxCurseLists;
	     list = At<uint8_t*>(list, ListNext), ++guard) {
		if ((At<uint32_t>(list, ListFlags) & ListFlagCurse) != 0 && list != seen) {
			*reinterpret_cast<uint32_t*>(list + ListFlags) &= ~ListFlagCurse;
			hidden[hiddenCount++] = list;
		}
	}
	const uint32_t n = ++g_curses;
	if (n <= 20 || n % 100 == 0) {
		plog::Info("curse: state %d, %d curse(s) on the target, %d slot(s) (caster %d), %s, %u so far", state, count,
		           slots, own, same != nullptr ? "same state" : seen != nullptr ? "replaces the oldest" : "added", n);
	}
	return own;
}

// PD2's curse resistance (stat curse_effectiveness on the cursed unit), on the list ApplyCurse just set: aurastat1,
// which D2R sets only on a new list, at most 75 on a player (0x1036d7d0); aurastat2-6 at most 75 (0x102fad50, PD2's
// stat setter for curse lists)
auto ResistCurse(uint32_t dataFlag, Unit* target, const d2r::CurseParams* params, void* list, bool fresh) noexcept
	-> void {
	using namespace d2r::rva;
	const int32_t resistance = StatOf(target, StatCurseResistance);
	if (resistance == 0) {
		return;
	}
	const auto value = d2r::Engine<int32_t (*)(Game*, Unit*, int32_t, int32_t) noexcept>(CurseStatValue);
	const auto setStat = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	const bool player = At<int32_t>(target, UnitType) == 0;
	for (int i = fresh ? 0 : 1; i < 6; ++i) {
		const int32_t stat = params->stats[i];
		if (stat < 0) {
			continue;
		}
		const int32_t base = value(params->game, target, stat, params->values[i]);
		if (base == 0) {
			continue;  // D2R set nothing for it
		}
		// shortcut: an aurastat1 resisted to 0 leaves the curse on with the stat at 0 (PD2 cancels it); upgrade by
		// resisting params->values[0] before the call if a unit ever shows it
		const int32_t used = resistance > CurseResistanceCap && (i > 0 || player) ? CurseResistanceCap : resistance;
		const int32_t resisted = Resisted(base, used);
		setStat(dataFlag, list, stat, resisted, 0);
		const uint32_t n = ++g_curseResisted;
		if (n <= 20 || n % 100 == 0) {
			plog::Info("curse resistance %d (used %d) on %s: aurastat%d stat %d %d -> %d, %u so far", resistance, used,
			           player ? "player" : "monster", i + 1, stat, base, resisted, n);
		}
	}
}

using ApplyCurseFn = int64_t (*)(Unit* target, d2r::CurseParams* params) noexcept;
ApplyCurseFn g_applyCurseOriginal = nullptr;

auto CurseApply(Unit* target, d2r::CurseParams* params) noexcept -> int64_t {
	using namespace d2r::rva;
	if (target == nullptr || params == nullptr || params->game == nullptr) {
		return g_applyCurseOriginal(target, params);
	}
	const uint32_t dataFlag = d2r::DataFlag(params->game);
	const int32_t state = params->state;
	if (state < 0 || d2r::Engine<uint32_t (*)(uint32_t, int32_t) noexcept>(StateIsCurse)(dataFlag, state) == 0) {
		return g_applyCurseOriginal(target, params);
	}
	const auto stateList = d2r::Engine<uint8_t* (*)(Unit*, int32_t) noexcept>(UnitStateList);
	uint8_t* before = stateList(target, state);
	uint8_t* hidden[MaxCurseLists];
	int hiddenCount = 0;
	const int32_t slots = MakeRoom(dataFlag, params->caster, target, state, hidden, hiddenCount);
	const int64_t applied = g_applyCurseOriginal(target, params);
	for (int i = 0; i < hiddenCount; ++i) {
		*reinterpret_cast<uint32_t*>(hidden[i] + ListFlags) |= ListFlagCurse;
	}
	uint8_t* list = applied != 0 ? stateList(target, state) : nullptr;
	if (list == nullptr) {
		return applied;
	}
	const bool fresh = list != before;
	if (fresh && slots > StatOf(target, StatCurseSlots)) {
		d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat)(dataFlag, list,
		                                                                                      StatCurseSlots, slots, 0);
	}
	ResistCurse(dataFlag, target, params, list, fresh);
	const uint32_t n = g_curses.load();
	if (n <= 20 || n % 100 == 0) {
		int held = 0;  // read back from the target, so the game check sees what D2R kept
		const uint8_t* owner = At<const uint8_t*>(target, UnitListOwner);
		int guard = 0;
		for (const uint8_t* l = owner != nullptr ? At<const uint8_t*>(owner, ListOwnerFirst) : nullptr;
		     l != nullptr && guard < MaxCurseLists; l = At<const uint8_t*>(l, ListNext), ++guard) {
			held += (At<uint32_t>(l, ListFlags) & ListFlagCurse) != 0 ? 1 : 0;
		}
		plog::Info("curse: state %d landed, the target holds %d curse(s)", state, held);
	}
	return applied;
}

}  // namespace

auto InstallItemRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	static constexpr uint8_t Empty[8] = {};
	using PropertyFn = int32_t (*)(uint8_t, uint32_t, uint64_t, Unit*, const void*, uint8_t, uint16_t, uint16_t,
	                               int32_t, uint32_t, uint32_t, uint64_t) noexcept;
	const bool property = PropertySlot < PropertyFuncSlots &&
	                      context->PatchWriteU64(PropertyFuncTable + static_cast<uint64_t>(PropertySlot) * 8, Empty,
	                                             sizeof(Empty),
	                                             reinterpret_cast<uint64_t>(static_cast<PropertyFn>(PropSorcSkillRandCtc)));
	const bool hook = context->InstallInlineHook(SrvSkillDispatch, SrvSkillDispatchEntry, sizeof(SrvSkillDispatchEntry),
	                                             SkillDispatch, &g_dispatchOriginal);
	const bool curses = context->InstallInlineHook(ApplyCurse, ApplyCurseEntry, sizeof(ApplyCurseEntry), CurseApply,
	                                               &g_applyCurseOriginal);
	std::snprintf(report, reportSize, "property func %d ormus%s; dospellcast skill on cast%s; curses%s", PropertySlot,
	              property ? "" : " FAILED (slot not empty)", hook ? "" : " FAILED (bytes differ)",
	              curses ? "" : " FAILED (bytes differ)");
	return (property ? 1 : 0) + (hook ? 1 : 0) + (curses ? 1 : 0);
}

auto ItemRuleCount() noexcept -> int {
	return 3;
}

auto ItemRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "ormus rolls %u, spell casts %u, skill on cast rolls %u, procs %u, curses %u, resisted %u",
	              g_rolls.load(), g_raises.load(), g_procRolls.load(), g_procs.load(), g_curses.load(),
	              g_curseResisted.load());
}
