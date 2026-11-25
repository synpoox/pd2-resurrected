// blade_sentinel
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

namespace {

// --- Blade Sentinel's think (PD2 monai row 102, BladeCreeper: 0x1035fd10; #1869) ----------------------------------
// D2R's think (0x5c7900, found by its signature) keeps what PD2's keeps: it lives out calc4, makes the blade creeper
// missile once with the owner's attack rating and to-hit percent on the sentinel, and runs it between its two path
// points (stuck time 15, run 20). PD2's think adds, around D2R's:
// - on the missile's think, item_tohit_percent on the sentinel also gets the skill's ToHit (PD2's 0x1030b960, D2R's
//   SkillToHit): the blade creeper missile (ToHit 1) rolls with it; a monster owner (a Shadow Warrior's or Master's
//   sentinel) gives the sentinel tohit = its tohit + 5 x dexterity;
// - 6 frames after the missile, then at most every 12 frames, on a think where the sentinel reached a path point: an
//   area hit within aurarangecalc of the sentinel through PD2's callback 0x10303190 (SplashOne). The owner hits with
//   Blade Sentinel's damage and its weapon at SrcDam, after a to-hit roll with the skill's ToHit, full leech, 75% of
//   each damage type.
// PD2 keeps the next hit's frame in the think's missile flag (1.13c +0x1c); D2R's flag is the same field (+0x4c), which
// its think only tests for 0, so the frame goes there too. No PluginSDK service runs code on a monster's AI tick: the
// think registers on row 102 for bladecreeper only, through ai_registry (one table write).
// The owner: D2R's summon (0x56e6a0), as 1.13c's, walks a monster caster up to its player and makes the sentinel the
// player's pet. PD2's srvdo 44 (0x103aee30) then writes a monster caster into the sentinel's AI owner, so a shadow's
// sentinel hits as the shadow. Ported the same way: srvdo 44 is wrapped to remember a monster caster, and row 102's
// init (run inside the summon, after D2R set the owner) writes it into the AI owner that 0x4a53c0 reads. The pet list
// stays the player's, as in PD2.
constexpr int32_t BladeCreeperRow = 102;  // monai.txt BladeCreeper (PD2 raw 102)

constexpr int32_t BladeCreeper = 413;     // monstats bladecreeper (PD2 raw 413)

constexpr int SentinelLeg = d2r::rva::AiControlParams + 4, SentinelNext = d2r::rva::AiControlParams + 8;

constexpr int32_t SentinelPercent = 75;

constexpr int32_t StatToHit = 19, StatDexterity = 2, StatToHitPercent = 119;  // the same rows in PD2's and our itemstatcost

constexpr int32_t SentinelSrvDo = 44;
// The monster data's AI block (unit data +0x30) and its owner id / type, as 0x4a5800 writes and 0x4a53c0 reads them

thread_local Unit* t_sentinelCaster = nullptr;  // a monster casting srvdo 44, while D2R's srvdo runs

SrvDoFn g_sentinelSrvDo = nullptr;

ai_registry::ThinkFn g_sentinelInit = nullptr;

}  // namespace

namespace pd2r::skills {

std::atomic<uint32_t> g_sentinelHits {0};

std::atomic<uint32_t> g_sentinelUnits {0};

// 93847: 0x5c7900, monai row 102's think, read from the AI table; it calls the pet's owner (0x4a53c0) at +0x10f and +0x312

}  // namespace pd2r::skills

namespace {

// 93847: 0x5c7900, monai row 102's think, read from the AI table; it calls the pet's owner (0x4a53c0) at +0x10f and +0x312
sigscan::Entry AiBladeCreeper {"AiBladeCreeper", "ai", sigscan::Kind::Code, 0, 0,
                               "40 53 55 57 41 55 41 56 41 57 48 83 ec 48 49 8b 40 28 48 8b da 49 8b 28 48 8b f9 0f b6 89 "
                               "06 01 00 00 44 0f bf a8 c4 01 00 00 41 8b d5 e8 ?? ?? ?? ?? 4c 8b f8 48 85 c0 0f 84 ?? ?? "
                               "?? ?? 45 33 c0 41 8b d5 48 8b cb e8"};
// 93847: 0x56f370, D2R's srvdo 44 (1.13c's code); 0x5c7c70, row 102's init (params -1, 1, 0)

// 93847: 0x56f370, D2R's srvdo 44 (1.13c's code); 0x5c7c70, row 102's init (params -1, 1, 0)
sigscan::Entry SrvDoBladeSentinelD2R {"SrvDoBladeSentinel", "skills", sigscan::Kind::Code, 0, 0,
                                      "48 89 5c 24 10 48 89 6c 24 18 48 89 74 24 20 57 41 56 41 57 48 81 ec 80 00 00 00 48 8b "
                                      "da 48 8b e9 0f b6 89 06 01 00 00 41 8b d0 41 8b f1 41 8b f8 e8 ?? ?? ?? ?? 4c 8b f0 48 "
                                      "85 c0 74"};

sigscan::Entry AiInitBladeCreeper {"AiInitBladeCreeper", "ai", sigscan::Kind::Code, 0, 0,
                                   "49 8b 00 c7 40 44 ff ff ff ff 48 c7 40 48 01 00 00 00 c3"};

sigscan::Entry* const SentinelOwn[] = {&AiBladeCreeper, &SrvDoBladeSentinelD2R, &AiInitBladeCreeper};

constexpr int SentinelOwnerCall = 0x10f, SentinelOwnerCall2 = 0x312;

using PetOwnerFn = Unit* (*)(Unit* pet) noexcept;

}  // namespace

namespace pd2r::skills {

PetOwnerFn g_petOwner = nullptr;

// One think's cadence: the next hit frame (0 until the missile's think) and whether this think hits

}  // namespace pd2r::skills

namespace {

// One think's cadence: the next hit frame (0 until the missile's think) and whether this think hits
struct SentinelStep {
	int32_t next;
	bool hit;
};

constexpr auto SentinelCadence(int32_t before, int32_t after, bool arrived, int32_t frame) noexcept -> SentinelStep {
	if (before == 0) {
		return {after != 0 ? frame + 6 : 0, false};  // D2R's think set the flag: the missile's think
	}
	return arrived && frame >= before ? SentinelStep {frame + 12, true} : SentinelStep {before, false};
}

static_assert(SentinelCadence(0, 1, false, 100).next == 106 && !SentinelCadence(0, 1, false, 100).hit);

static_assert(SentinelCadence(0, 0, true, 100).next == 0 && !SentinelCadence(0, 0, true, 100).hit);

static_assert(!SentinelCadence(106, 106, true, 105).hit && SentinelCadence(106, 106, true, 105).next == 106);

static_assert(SentinelCadence(106, 106, true, 106).hit && SentinelCadence(106, 106, true, 106).next == 118);

static_assert(!SentinelCadence(106, 106, false, 200).hit);

// The sentinel's Skill1 (as both thinks read it) and its level; level 0 when the sentinel lacks the skill

// The sentinel's Skill1 (as both thinks read it) and its level; level 0 when the sentinel lacks the skill
auto SentinelSkill(const uint8_t* monstats, Unit* unit, int32_t* level) noexcept -> int32_t {
	const int32_t skillId = At<int16_t>(monstats, d2r::rva::MonSkills);
	void* uskill = d2r::Engine<void* (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::GetUnitSkill)(unit, skillId, 0);
	*level = uskill != nullptr
	             ? d2r::Engine<int32_t (*)(Unit*, void*, int32_t) noexcept>(d2r::sig::UnitSkillLevel)(unit, uskill, 1)
	             : 0;
	return skillId;
}


auto SkillToHitOf(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::SkillToHit)(unit, skillId, level);
}


void SentinelHit(Game* game, Unit* unit, Unit* owner, const uint8_t* monstats) noexcept {
	using namespace d2r::rva;
	int32_t level = 0;
	const int32_t skillId = SentinelSkill(monstats, unit, &level);
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	int32_t x = 0, y = 0;
	if (skill == nullptr || level == 0 || !d2r::UnitXY(unit, &x, &y)) {
		return;
	}
	const int32_t radius = d2r::EvalCalc(game, owner, skill, SkAuraRangeCalc, skillId, level);
	SplashHit hit {.struck = nullptr, .srcDam = At<uint8_t>(skill, SkSrcDam), .skillId = skillId, .level = level,
	               .percent = 0, .hitFlags = At<uint32_t>(skill, SkHitFlags), .resultFlags = At<uint16_t>(skill, SkResultFlags),
	               .hitClass = At<int32_t>(skill, SkHitClass), .victims = 0, .attacker = owner, .roll = true,
	               .toHit = SkillToHitOf(owner, skillId, level),
	               .leechDivisor = 1, .damagePercent = SentinelPercent};
	d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::UnitRoom)(unit), x, y};
	if (radius > 0 && center.room != nullptr) {
		d2r::Engine<AreaFn>(d2r::sig::AreaUnits)(game, unit, &center, radius, AreaUnitMask, SplashOne, &hit, 0);
	}
	g_sentinelUnits.fetch_add(static_cast<uint32_t>(hit.victims));
	++g_sentinelHits;
}


void SentinelThink(Game* game, Unit* unit, uint8_t* tick) noexcept {
	using namespace d2r::rva;
	uint8_t* control = tick != nullptr ? At<uint8_t*>(tick, AiTickControl) : nullptr;
	const int32_t leg = control != nullptr ? At<int32_t>(control, SentinelLeg) : 0;
	const int32_t before = control != nullptr ? At<int32_t>(control, SentinelNext) : 0;
	ai_registry::Vanilla(BladeCreeperRow)(game, unit, tick);
	if (control == nullptr || d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::sig::UnitDead)(unit) != 0) {
		return;
	}
	auto& next = *reinterpret_cast<int32_t*>(control + SentinelNext);
	const SentinelStep step = SentinelCadence(before, next, At<int32_t>(control, SentinelLeg) != leg, At<int32_t>(game, GameFrame));
	next = step.next;
	Unit* owner = g_petOwner(unit);
	owner = owner != nullptr ? owner : unit;  // as both thinks fall back
	if (before == 0 && step.next != 0) {  // the missile's think: D2R's think just set tohit and item_tohit_percent
		// shortcut: D2R and PD2 set these stats only when the missile was made; this runs when D2R's think tried
		// (srvmissilea valid). Upgrade if a sentinel without its missile shows up.
		const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat);
		const auto set = d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::sig::SetUnitStat);
		int32_t level = 0;
		const int32_t skillId = SentinelSkill(At<const uint8_t*>(tick, AiTickMonStats), unit, &level);
		set(unit, StatToHitPercent, stat(unit, StatToHitPercent, 0) + SkillToHitOf(owner, skillId, level), 0);
		if (At<int32_t>(owner, UnitType) == UnitMonster) {
			set(unit, StatToHit, stat(owner, StatToHit, 0) + 5 * stat(owner, StatDexterity, 0), 0);
		}
	}
	if (step.hit) {
		SentinelHit(game, unit, owner, At<const uint8_t*>(tick, AiTickMonStats));
	}
}

// srvdo 44: D2R's, with a monster caster remembered for the init below

// srvdo 44: D2R's, with a monster caster remembered for the init below
auto SrvDoBladeSentinel(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	t_sentinelCaster = unit != nullptr && At<int32_t>(unit, d2r::rva::UnitType) == d2r::rva::UnitMonster ? unit : nullptr;
	const int64_t ran = g_sentinelSrvDo(game, unit, skillId, level);
	t_sentinelCaster = nullptr;
	return ran;
}

// Row 102's init: D2R's, then PD2's owner write for a sentinel a monster cast (0x103aee30)

// Row 102's init: D2R's, then PD2's owner write for a sentinel a monster cast (0x103aee30)
void SentinelInit(Game* game, Unit* unit, uint8_t* tick) noexcept {
	using namespace d2r::rva;
	g_sentinelInit(game, unit, tick);
	Unit* caster = t_sentinelCaster;
	if (caster == nullptr || unit == nullptr || At<int32_t>(unit, UnitClassId) != BladeCreeper) {
		return;
	}
	uint8_t* data = At<uint8_t*>(unit, UnitData);
	uint8_t* ai = data != nullptr ? At<uint8_t*>(data, MonDataAi) : nullptr;
	if (ai != nullptr) {
		*reinterpret_cast<uint32_t*>(ai + AiOwnerId) = At<uint32_t>(caster, UnitIdField);
		*reinterpret_cast<int32_t*>(ai + AiOwnerType) = UnitMonster;
	}
}

// Slot `rva` must hold `expected` (D2R's function, found by its signature): writes `function` there

}  // namespace

namespace pd2r::skills {

// Slot `rva` must hold `expected` (D2R's function, found by its signature): writes `function` there
auto SwapSlot(const D2RL::PluginContext* context, uint64_t rva, uint64_t expected, const void* function) noexcept -> bool {
	uint8_t bytes[8];
	std::memcpy(bytes, &expected, sizeof(bytes));
	return context->PatchWriteU64(rva, bytes, sizeof(bytes), reinterpret_cast<uint64_t>(function));
}

// The monster-caster owner: row 102's init, then srvdo 44, each slot checked against D2R's function found by
// signature. The init first: alone it is inert (no caster is ever remembered). False when either is refused.

}  // namespace pd2r::skills

namespace {

// The monster-caster owner: row 102's init, then srvdo 44, each slot checked against D2R's function found by
// signature. The init first: alone it is inert (no caster is ever remembered). False when either is refused.
auto PatchSentinelOwner(const D2RL::PluginContext* context) noexcept -> bool {
	if (SrvDoBladeSentinelD2R.state != sigscan::State::Ok || AiInitBladeCreeper.state != sigscan::State::Ok ||
	    d2r::sig::SrvDoTable.state != sigscan::State::Ok || d2r::sig::AiTable.state != sigscan::State::Ok) {
		return false;
	}
	g_sentinelInit = reinterpret_cast<ai_registry::ThinkFn>(AiInitBladeCreeper.address);
	g_sentinelSrvDo = reinterpret_cast<SrvDoFn>(SrvDoBladeSentinelD2R.address);
	// The AI table's rows start 8 bytes before d2r::sig::AiTable's think column: {kind, init, think, next}
	const uint64_t initSlot = d2r::sig::AiTable.rva + static_cast<uint64_t>(BladeCreeperRow) * d2r::rva::AiRowSize - 8;
	return SwapSlot(context, initSlot, AiInitBladeCreeper.address, reinterpret_cast<const void*>(&SentinelInit)) &&
	       SwapSlot(context, d2r::sig::SrvDoTable.rva + static_cast<uint64_t>(SentinelSrvDo) * 8,
	                SrvDoBladeSentinelD2R.address, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvDoBladeSentinel)));
}

// Finds D2R's think and the owner call in it, then registers the think on row 102; false (nothing written) on a miss

// Finds D2R's think and the owner call in it, then registers the think on row 102; false (nothing written) on a miss
auto PatchBladeSentinel(const D2RL::PluginContext* context) noexcept -> bool {
	if (AiBladeCreeper.state != sigscan::State::Ok) {
		return false;
	}
	const auto callee = [](uint64_t at) -> uint64_t {
		const auto* code = reinterpret_cast<const uint8_t*>(at);
		int32_t rel = 0;
		std::memcpy(&rel, code + 1, sizeof(rel));
		return code[0] == 0xe8 ? at + 5 + rel : 0;
	};
	const uint64_t owner = callee(AiBladeCreeper.address + SentinelOwnerCall);
	const uint64_t roll =
		d2r::sig::MeleeRoll.state == sigscan::State::Ok ? callee(d2r::sig::MeleeRoll.address + MeleeRollToHitCall) : 0;
	if (owner == 0 || owner != callee(AiBladeCreeper.address + SentinelOwnerCall2) || roll == 0) {
		return false;
	}
	g_toHitRoll = reinterpret_cast<ToHitRollFn>(roll);
	g_petOwner = reinterpret_cast<PetOwnerFn>(owner);
	return ai_registry::RegisterAt(context, BladeCreeperRow, AiBladeCreeper.address, BladeCreeper, &SentinelThink);
}

// --- Holy Light (PD2 srvdofunc 176) ---------------------------------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 176 at 0x103b3cc0, its per-unit callback 0x103043b0
// (docs/research/skills/w17-holy-light.md). Every unit within aurarangecalc of the target, the caster aside: an ally heals
// cltcalc2-cltcalc3 life, an enemy takes the skill's magic damage; whichever side the target is not on gets half.

}  // namespace

auto InstallBladeSentinelRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	size_t used = std::strlen(report);
	sigscan::Resolve(SentinelOwn, std::size(SentinelOwn), context->exeBase, nullptr, 0);
	sigscan::Entry* const leapArc[] = {&LeapArc};
	sigscan::Resolve(leapArc, 1, context->exeBase, nullptr, 0);
	if (LeapArc.state != sigscan::State::Ok) {
		plog::Info("skills: signature %s %s, leap arc keeps D2R's timing", LeapArc.name, sigscan::StateText(LeapArc.state));
	}
	const bool sentinel = PatchBladeSentinel(context);
	const bool sentinelOwner = sentinel && PatchSentinelOwner(context);
	used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used,
		              "; blade sentinel think (monai %d)%s, monster caster owner (srvdofunc %d, monai init)%s",
		              BladeCreeperRow, sentinel ? "" : " FAILED (signature or slot)", SentinelSrvDo,
		              sentinelOwner ? "" : " FAILED (signature or slot)");
	}
	int installed = 0;
	for (const bool ok : {sentinel, sentinelOwner}) {
		installed += ok ? 1 : 0;
	}
	return installed;
}

PD2R_RULE_N(InstallBladeSentinelRules, 360, 2)

