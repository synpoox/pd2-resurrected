// Curses: curse resistance, max curses, Dark Pact and the boss curse (docs/research/items/ir4-curses.md).
// Moved verbatim from item_rules.cpp (#2198).
#include "shared.h"

namespace itemrules {

// --- Curses: curse resistance and max curses (docs/research/items/ir4-curses.md) ---------------------------------------
constexpr int32_t StatCurseSlots = 374;          // curse_slots, merged id (PD2 190): the slot count a curse was cast with
constexpr int32_t StatMaxCurses = 421;           // max_curses (PD2 368)
constexpr int32_t StatCurseResistance = 557;     // curse_effectiveness, shown as Curse Resistance (PD2 504)
constexpr int32_t StatCurseDuration = 109;      // curse_resistance: cuts a curse's duration (same id merged and PD2)
constexpr int32_t StateAttract = 57;             // attract (merged id; D2R's NewStateList tests 0x39)
constexpr int32_t CurseResistanceCap = 75;
constexpr int MaxCurseLists = 64;  // a unit holds a handful; more only stops a corrupt chain from spinning

std::atomic<uint32_t> g_curses {0};
std::atomic<uint32_t> g_curseResisted {0};
std::atomic<uint32_t> g_curseGate {0};
std::atomic<uint32_t> g_gateFallbacks {0};  // ApplyUngated's "did not clear" lines, per cursed target

// PD2's 0x1037c4c0 as its curse code calls it: value * (100 - resistance) / 100, truncated
auto Resisted(int32_t value, int32_t resistance) noexcept -> int32_t {
	const double v = static_cast<double>(value) * (100.0 - resistance) / 100.0;
	return v > 2147483647.0 ? 2147483647 : static_cast<int32_t>(v);
}

auto StatOf(Unit* unit, int32_t stat, int32_t layer) noexcept -> int32_t {
	return unit != nullptr ? d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(unit, stat, layer)
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
		const int32_t used = resistance > CurseResistanceCap && (i > 0 || player) ? CurseResistanceCap : resistance;
		const int32_t resisted = Resisted(base, used);
		setStat(dataFlag, list, stat, resisted, 0);
		const uint32_t n = ++g_curseResisted;
	}
}

// PD2's curse gate (0x1036d7d0) ahead of D2R's: a curse whose aurastat1 resists to 0 is cancelled, and stat 109 cuts
// the duration by PD2's formula (at most 75 on a player), a duration of 0 cancelling it. Returns false to cancel;
// `frames` is the duration to hand D2R, `duration` the target's stat 109.
auto CurseGate(const d2r::CurseParams* params, Unit* target, int32_t& frames, int32_t& duration) noexcept -> bool {
	const bool player = At<int32_t>(target, d2r::rva::UnitType) == 0;
	const auto capped = [player](int32_t r) { return player && r > CurseResistanceCap ? CurseResistanceCap : r; };
	const int32_t resistance = StatOf(target, StatCurseResistance);
	if (resistance != 0 && params->stats[0] >= 0) {
		const int32_t base = d2r::Engine<int32_t (*)(Game*, Unit*, int32_t, int32_t) noexcept>(d2r::rva::CurseStatValue)(
			params->game, target, params->stats[0], params->values[0]);
		if (base != 0 && Resisted(base, capped(resistance)) == 0) {
			++g_curseGate;
			return false;
		}
	}
	frames = params->frames;
	duration = StatOf(target, StatCurseDuration);
	if (duration == 0 || frames <= 0) {
		return true;
	}
	frames = Resisted(params->frames, capped(duration));
	++g_curseGate;
	return frames > 0;
}

ApplyCurseFn g_applyCurseOriginal = nullptr;

// D2R's NewStateList (0x433d20, its entry D2RCore's, so not hookable here) refuses a curse at stat 109 >= 100, cuts
// the duration by it uncapped, and refuses a curse on a unit with attract. PD2 does neither, so the call runs with
// the target's stat 109 zeroed on its base layer (the gate already cut `frames`) and its attract bit off. Both come
// back before this returns; nothing else runs in between (one game thread).
auto ApplyUngated(Unit* target, d2r::CurseParams* params, uint32_t dataFlag, int32_t frames, int32_t duration) noexcept
	-> int64_t {
	using namespace d2r::rva;
	const auto setStat = d2r::Engine<void (*)(Unit*, uint32_t, int32_t, uint32_t) noexcept>(SetUnitStat);
	void* owner = At<void*>(target, UnitListOwner);
	int32_t base = 0;
	bool zeroed = false;
	if (duration != 0 && owner != nullptr) {
		base = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(ListStatValue)(dataFlag, owner,
		                                                                                          StatCurseDuration, 0);
		setStat(target, StatCurseDuration, base - duration, 0);
		zeroed = StatOf(target, StatCurseDuration) == 0;
		if (!zeroed) {  // stat 109 not where it was looked for: D2R's own rule applies, on the uncut duration
			setStat(target, StatCurseDuration, base, 0);
			++g_gateFallbacks;
		}
	}
	// The bit UnitHasState reads (SetStateGfxFlag writes another array)
	const auto hasState = d2r::Engine<uint32_t (*)(Unit*, int32_t) noexcept>(UnitHasState);
	auto* word = owner != nullptr && At<int32_t>(owner, ListOwnerMark) < 0
	                 ? reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(owner) + ListOwnerStates + (StateAttract >> 5) * 4)
	                 : nullptr;
	const uint32_t bit = 1u << (StateAttract & 31);
	const bool attracted = word != nullptr && hasState(target, StateAttract) != 0;
	if (attracted) {
		*word &= ~bit;
		if (hasState(target, StateAttract) != 0) {
			++g_gateFallbacks;
		}
	}
	const int32_t asked = params->frames;
	params->frames = zeroed ? frames : asked;  // the srvdo's area walk reuses params for its next target
	const int64_t applied = g_applyCurseOriginal(target, params);
	params->frames = asked;
	if (zeroed) {
		setStat(target, StatCurseDuration, base, 0);
		if (StatOf(target, StatCurseDuration) != duration) {
			++g_gateFallbacks;
		}
	}
	// A full target may have lost the attract curse itself to this one: then its bit stays off
	if (attracted && d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(target, StateAttract) != nullptr) {
		*word |= bit;
	}
	if (attracted) {
		++g_curseGate;
	}
	return applied;
}

// --- Dark Pact (#265, docs/research/items/ir4-curses.md): PD2's state list creator 0x1036d7d0, state 196 ----------------
// PD2 refuses a monster D2R's NewStateList also refuses (below), then consumes the first curse in the target's chain
// that is not the oldest timed one (so a lone timed curse stays), then
// hits every unit within calc1 + curses - 1 of the target with the skill's magic damage times 1 + 1.1 * (curses - 1)
// (D2Game 0x9f210's roll, area search 0xa0c70 with mask 0x8583 and callback 0xa1210), then puts dark_pact on the
// target past the max-curses rule. No curse on the target: nothing. PD2 skips its curse gate for this state.
constexpr int32_t StateDarkPact = 242;  // dark_pact, merged id (PD2 196)
constexpr int ListState = 0x20;         // int32 the list's state (rva::SetListState writes it)
constexpr uint32_t DarkPactHitFlags = 1;
constexpr uint32_t DarkPactResultFlags = 5;

std::atomic<uint32_t> g_darkPacts {0};

// PD2's early-out in 0x1036d7d0, also D2R's NewStateList's first checks (0x433d82-0x433f16): a monster target is
// refused when its monstats row is missing or flagged npc (flags bit 8: byte +0x3d & the bit-mask table's entry 0),
// or its monstats2 row (monstats +0x4c, table +0xf98, 0x128 a row) lacks revive (flags bit 9: byte +5 & entry 1).
// Dark Pact must ask first: its explosion comes before NewStateList.
constexpr int MonStats2Index = 0x4c;                      // int16 monstats -> monstats2 row
constexpr int TablesMonStats2 = 0xf98, TablesMonStats2Count = 0xfa0;
constexpr int MonStats2Size = 0x128, MonStats2Flags1 = 0x5;  // byte: flags bits 8-15
constexpr uint64_t BitMasks = 0x1d996d0;                   // uint32[32]: 1 << i (MeleeRollFlagMask is entry 0)

auto StateRefused(uint32_t dataFlag, Unit* target) noexcept -> bool {
	using namespace d2r::rva;
	if (At<int32_t>(target, UnitType) != UnitMonster) {
		return false;
	}
	const auto* masks = reinterpret_cast<const uint8_t*>(d2r::imageBase + BitMasks);
	const uint8_t* mon = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
		dataFlag, At<int32_t>(target, UnitClassId));
	if (mon == nullptr || (At<uint8_t>(mon, MonFlags + 1) & masks[0]) != 0) {
		return true;
	}
	const int64_t row = At<int16_t>(mon, MonStats2Index);
	const uint8_t* tables = DataTables(dataFlag);
	if (tables == nullptr || row < 0 || static_cast<uint64_t>(row) >= At<uint64_t>(tables, TablesMonStats2Count)) {
		return true;
	}
	const uint8_t* mon2 = At<const uint8_t*>(tables, TablesMonStats2) + row * MonStats2Size;
	return (At<uint8_t>(mon2, MonStats2Flags1) & masks[4]) == 0;
}

using AreaFn = void (*)(Game*, Unit*, d2r::AreaCenter*, int32_t, uint32_t, int32_t (*)(d2r::AreaContext*, Unit*), void*,
                        int32_t) noexcept;  // rva::AreaUnits

struct DarkPactHit {
	d2r::LeapKnockback* rolled;  // one roll for every unit in range, as PD2's callback copies one block
	int64_t victims;
};

// One unit in range (1.13c D2Game 0xa1210): a copy of the roll through the area apply
auto DarkPactOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	auto* hit = static_cast<DarkPactHit*>(area->arg);
	d2r::LeapKnockback damage;
	d2r::CopyRolled(*hit->rolled, damage);
	damage.hitFlags = DarkPactHitFlags;
	damage.resultFlags = DarkPactResultFlags;
	d2r::Engine<void (*)(Game*, Unit*, Unit*, void*) noexcept>(d2r::rva::AreaApply)(static_cast<Game*>(area->game),
	                                                                               area->unit, victim, &damage);
	++hit->victims;
	d2r::ReleaseDamage(damage);
	return 1;
}

// PD2's multiplier: the magic damage, as an unsigned value, times (curses - 1.0) * 1.1 + 1.0, truncated
auto DarkPactDamage(int32_t magic, int curses) noexcept -> int32_t {
	const double v = static_cast<double>(static_cast<uint32_t>(magic)) * ((curses - 1.0) * 1.1 + 1.0);
	return v >= 4294967295.0 ? -1 : static_cast<int32_t>(static_cast<uint32_t>(v));
}

auto ApplyDarkPact(Unit* target, d2r::CurseParams* params, uint32_t dataFlag) noexcept -> int64_t {
	using namespace d2r::rva;
	Game* game = params->game;
	Unit* caster = params->caster;
	uint8_t* owner = At<uint8_t*>(target, UnitListOwner);
	if (owner == nullptr || At<int32_t>(owner, ListOwnerMark) >= 0) {
		return 0;
	}
	if (StateRefused(dataFlag, target)) {
		++g_darkPacts;
		return 0;
	}
	const auto listStat = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(ListStatValue);
	const auto setListStat = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	const auto stateList = d2r::Engine<uint8_t* (*)(Unit*, int32_t) noexcept>(UnitStateList);
	// PD2's walk: count the curses, keep the most slots any was cast with, find the oldest timed one
	const int32_t own = StatOf(caster, StatMaxCurses) + 1;
	int32_t slots = own;
	int count = 0;
	uint8_t* oldest = nullptr;
	int guard = 0;
	for (uint8_t* list = At<uint8_t*>(owner, ListOwnerFirst); list != nullptr && guard < MaxCurseLists;
	     list = At<uint8_t*>(list, ListNext), ++guard) {
		if ((At<uint32_t>(list, ListFlags) & ListFlagCurse) == 0) {
			continue;
		}
		++count;
		const int32_t kept = listStat(dataFlag, list, StatCurseSlots, 0);
		if (kept != 0 && kept < slots) {
			setListStat(dataFlag, list, StatCurseSlots, 0, 0);
		} else if (kept > slots) {
			slots = kept;
		}
		if ((At<uint32_t>(list, ListFlags) & ListFlagTimed) != 0) {
			oldest = list;
		}
	}
	// The first curse that is not the oldest timed one goes (PD2's 0x1030c530), state bit off as RemoveSkillState does
	int32_t consumed = -1;
	guard = 0;
	for (uint8_t* list = At<uint8_t*>(owner, ListOwnerFirst); list != nullptr && guard < MaxCurseLists;
	     list = At<uint8_t*>(list, ListNext), ++guard) {
		if ((At<uint32_t>(list, ListFlags) & ListFlagCurse) != 0 && list != oldest) {
			consumed = At<int32_t>(list, ListState);
			d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateBit)(target, consumed, 0);
			d2r::Engine<void (*)(Unit*, void*) noexcept>(DetachStatList)(target, list);
			d2r::Engine<void (*)(uint32_t, void*) noexcept>(FreeStatList)(dataFlag, list);
			break;
		}
	}
	++g_darkPacts;
	if (count < 1) {
		return 0;
	}
	// The follow-up: the skill's magic roll, scaled, around the target
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, params->skill);
	int32_t x = 0, y = 0;
	if (skill != nullptr && caster != nullptr && d2r::UnitXY(target, &x, &y)) {
		const int32_t radius = d2r::EvalCalc(game, caster, skill, SkCalc1, params->skill, params->level) + count - 1;
		d2r::LeapKnockback rolled;
		d2r::Engine<void (*)(Unit*, void*, int32_t, int32_t) noexcept>(ElementalDamage)(caster, &rolled, params->skill,
		                                                                                params->level);
		auto& magic = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&rolled) + DmgMagic);
		magic = DarkPactDamage(magic, count);
		DarkPactHit hit {&rolled, 0};
		d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(target), x, y};
		d2r::Engine<AreaFn>(AreaUnits)(game, caster, &center, radius, AreaUnitMask, DarkPactOne, &hit, 0);
		d2r::ReleaseDamage(rolled);
	}
	// dark_pact itself, past the max-curses rule: NewStateList sees only an older dark_pact list, if any
	uint8_t* same = stateList(target, StateDarkPact);
	uint8_t* hidden[MaxCurseLists];
	int hiddenCount = 0;
	owner = At<uint8_t*>(target, UnitListOwner);
	guard = 0;
	for (uint8_t* list = owner != nullptr ? At<uint8_t*>(owner, ListOwnerFirst) : nullptr;
	     list != nullptr && guard < MaxCurseLists; list = At<uint8_t*>(list, ListNext), ++guard) {
		if ((At<uint32_t>(list, ListFlags) & ListFlagCurse) != 0 && list != same) {
			*reinterpret_cast<uint32_t*>(list + ListFlags) &= ~ListFlagCurse;
			hidden[hiddenCount++] = list;
		}
	}
	const int64_t applied = ApplyUngated(target, params, dataFlag, params->frames, StatOf(target, StatCurseDuration));
	for (int i = 0; i < hiddenCount; ++i) {
		*reinterpret_cast<uint32_t*>(hidden[i] + ListFlags) |= ListFlagCurse;
	}
	uint8_t* list = applied != 0 ? stateList(target, StateDarkPact) : nullptr;
	if (list != nullptr && list != same && own > StatOf(target, StatCurseSlots)) {
		setListStat(dataFlag, list, StatCurseSlots, own, 0);
	}
	return applied;
}

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
	if (state == StateDarkPact) {
		return ApplyDarkPact(target, params, dataFlag);
	}
	int32_t frames = 0, duration = 0;
	if (!CurseGate(params, target, frames, duration)) {
		return 0;
	}
	const auto stateList = d2r::Engine<uint8_t* (*)(Unit*, int32_t) noexcept>(UnitStateList);
	uint8_t* before = stateList(target, state);
	uint8_t* hidden[MaxCurseLists];
	int hiddenCount = 0;
	const int32_t slots = MakeRoom(dataFlag, params->caster, target, state, hidden, hiddenCount);
	const int64_t applied = ApplyUngated(target, params, dataFlag, frames, duration);
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
	return applied;
}

// --- Boss curse (#1902): the curse umod's area callback, 93847 0x4a06b0 (1.13c D2Game 0x24030) ----------------------
// D2R's callback puts Amplify Damage with NewStateList itself, past ApplyCurse and so past the curse rules above. PD2
// swaps it for 0x1036e880, which hands the curse to its curse helper 0x1036d7d0; here the callback is replaced by an
// ApplyCurse call through CurseApply, its params filled as the curse srvdo (0x55c840) fills them.
// "44 89 4c 24 20 ... 48 83 b8 b8 11 00 00 42": the entry, its hostility call, the skills.txt row 66 bounds check
// mov [rsp+0x20],r9d / push rbp / push rsi / push r12 / push r14 / push r15 / mov rbp,rsp
// PD2's callback casts MonAmplifyDamage (PD2 424, merged 494; test_boss_curse.py checks the row); the level stays the
// handler's, from row 66
constexpr int32_t SkillMonAmplifyDamage = 494;
constexpr int SkAuraTargetState = 0xa2;     // int16 auratargetstate: the state the callback puts

// ApplyCurse reads past CurseParams: 0x60 and 0x68 hold the skill's +0xa4 and +0xac, which its state send reads
struct BossCurseParams {
	d2r::CurseParams curse;
	uint64_t sendA;
	uint64_t sendB;
};
static_assert(offsetof(BossCurseParams, sendA) == 0x60 && offsetof(BossCurseParams, sendB) == 0x68);

BossCurseFn g_bossCurseOriginal = nullptr;  // never called: the replacement does the whole job
int32_t (*g_bossCurseHostile)(Game*, Unit*, Unit*) noexcept = nullptr;

auto BossCurse(Game* game, Unit* caster, Unit* target, int32_t level) noexcept -> void {
	using namespace d2r::rva;
	if (game == nullptr || caster == nullptr || target == nullptr || g_bossCurseHostile(game, caster, target) == 0) {
		return;  // the callback's own first check, ahead of MakeRoom touching the target's curses
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), SkillMonAmplifyDamage);
	if (skill == nullptr || At<int16_t>(skill, SkAuraTargetState) < 0) {
		return;
	}
	BossCurseParams p {};
	d2r::CurseParams& c = p.curse;
	c.game = game;
	c.caster = caster;
	c.skill = SkillMonAmplifyDamage;
	c.level = level;
	c.key = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey)(caster);
	c.frames = d2r::EvalCalc(game, caster, skill, SkAuraLenCalc, SkillMonAmplifyDamage, level);
	for (int i = 0; i < 6; ++i) {
		const int16_t stat = At<int16_t>(skill, SkAuraStat1 + 2 * i);
		c.stats[i] = stat < 0 ? -1 : stat;
		c.values[i] = stat < 0 ? 0 : d2r::EvalCalc(game, caster, skill, SkAuraStatCalc1 + 4 * i, SkillMonAmplifyDamage, level);
	}
	c.state = At<int16_t>(skill, SkAuraTargetState);
	p.sendA = At<uint64_t>(skill, SkSendStateA);
	p.sendB = At<uint64_t>(skill, SkSendStateB);
	const int64_t applied = CurseApply(target, &c);
}

}  // namespace itemrules
