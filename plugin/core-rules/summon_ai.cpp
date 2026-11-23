// PD2's changes to the ShadowMaster summon AI (#1790): monai row 106, used by the Valkyrie, the Decoy (dopplezonnew) and
// the Shadow Master. The tables equal PD2's; the difference is engine code. ProjectDiablo.dll keeps 1.13c's think
// (D2Game +0xab580) and patches it (its patch table, docs/pd2/summon-ai.md):
// - both calls of 1.13c's leash (+0xab66e, +0xab939, callee +0x1e140) go to PD2's own (0x10360d40 in the installed
//   DLL). 1.13c's leash, called whenever the pet is more than aip2(H) = 13 subtiles from its owner, walks it back
//   once it is more than about 6 away, target or not: the Valkyrie ran at a far monster, then turned back. PD2's leash
//   lets the pet fight up to sqrt(3.2 * 361) = 34 subtiles out and idle up to 19 (owner, then pet, squared distances).
// - five branches become `jmp`: the buff branch always casts (+0xab865), the pet never takes its owner's target
//   (+0xab8e2), no "basic attack" shortcut (+0xab99d), no step back or aside in a crowd (+0xabb48, the Decoy's
//   "reposition" run), no fallback attack when no skill scored (+0xac337).
// D2R's think (0x5cd740) is the same code, so the same five branches are patched here, each found by a unique
// signature, through the MutationService (no SDK service changes an AI). The leash (0x5bd1e0) is hooked at its entry:
// no SDK service runs inside an AI think. Only the two calls from the ShadowMaster think (their return addresses) get
// PD2's rule; every other caller (other pets) keeps D2R's leash.
// PD2 also patches the druid vine thinks (#1802): Vines (row 110, Poison Creeper; 1.13c +0xab494, 30 bytes NOPed) no
// longer skips a target that has poison (state 2) or 100 poison resist; CycleOfLife (row 111, Carrion Vine, Solar
// Creeper; +0xab26e, 0 -> 1) keeps its "owner is hurt" flag at 1, so the vine eats corpses at full life or mana too.
#include "summon_ai.h"

#include "hot_log.h"
#include "pd2_engine.h"
#include "log.h"
#include "sig.h"

#include <D2RLPlugin/mutations.h>

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace {

using d2r::Game;
using d2r::Unit;

// D2R's mover kinds (0x5bc940's fourth argument, as D2R's leash picks them)
constexpr int32_t MoveNone = 0, MoveBack = 1, MoveWarp = 3, MoveWander = 4, MoveAside = 5;

// PD2's leash rule (0x10360d40): d2 = owner-to-pet squared distance; `fighting` = the pet has a target
constexpr auto LeashMove(int64_t d2, bool fighting, bool wanderRoll) noexcept -> int32_t {
	const double limit = (fighting ? 3.2 : 1.0) * 361.0;  // the DLL's doubles 1.0 / 3.2 times 361.0
	if (static_cast<double>(d2) > limit) {
		return d2 >= 0x900 ? MoveWarp : MoveBack;
	}
	if (fighting) {
		return MoveNone;
	}
	if (d2 <= 5) {
		return MoveAside;
	}
	return wanderRoll ? MoveWander : MoveNone;  // PD2: one think in 12
}
static_assert(LeashMove(361, false, false) == MoveNone && LeashMove(362, false, false) == MoveBack);
static_assert(LeashMove(1155, true, false) == MoveNone && LeashMove(1156, true, false) == MoveBack);
static_assert(LeashMove(0x900, true, false) == MoveWarp && LeashMove(5, false, false) == MoveAside);
static_assert(LeashMove(100, false, true) == MoveWander && LeashMove(100, true, true) == MoveNone);

using LeashFn = uint64_t (*)(Game*, Unit* pet, Unit* target, Unit* owner, int32_t flag, void* tick, int32_t, int32_t radius) noexcept;
using MoverFn = uint64_t (*)(Game*, Unit* owner, Unit* pet, int32_t kind, int32_t, int32_t, uint8_t radius) noexcept;
using PathTargetFn = Unit* (*)(Game*, Unit*) noexcept;
using IsDeadFn = int32_t (*)(Unit*) noexcept;

LeashFn g_leashOriginal = nullptr;
MoverFn g_mover = nullptr;
PathTargetFn g_pathTarget = nullptr;
IsDeadFn g_isDead = nullptr;
uint64_t g_leashReturn[2] = {};  // absolute return addresses of the think's two leash calls

std::atomic<uint32_t> g_leashCalls {0};
std::atomic<uint32_t> g_moves[6] {};

auto Dist2(Unit* a, Unit* b) noexcept -> int64_t {
	int32_t ax = 0, ay = 0, bx = 0, by = 0;
	if (!d2r::UnitXY(a, &ax, &ay) || !d2r::UnitXY(b, &bx, &by)) {
		return -1;
	}
	return int64_t {ax - bx} * (ax - bx) + int64_t {ay - by} * (ay - by);
}

// PD2's leash (0x10360d40), for the ShadowMaster think's two calls and the Shadow Warrior think's one: non-zero once moved
auto Pd2Leash(Game* game, Unit* pet, Unit* target, Unit* owner, int32_t radius) noexcept -> uint64_t {
	++g_leashCalls;
	// PD2 leashes only a player's pet
	if (game == nullptr || pet == nullptr || owner == nullptr || d2r::At<int32_t>(owner, d2r::rva::UnitType) != 0) {
		return 0;
	}
	const int64_t d2 = Dist2(owner, pet);
	if (d2 < 0) {
		return 0;
	}
	bool fighting = target != nullptr;
	if (!fighting) {
		// PD2: the unit the pet's path is after, a live player or monster within 34 subtiles
		Unit* chase = g_pathTarget(game, pet);
		fighting = chase != nullptr && chase != owner && d2r::At<int32_t>(chase, d2r::rva::UnitType) <= 1 &&
		           g_isDead(chase) == 0 && Dist2(pet, chase) >= 0 && Dist2(pet, chase) <= 1155;
	}
	uint32_t* seed = d2r::api.unitSeed != nullptr ? d2r::api.unitSeed(pet) : nullptr;
	const bool wander = seed != nullptr && d2r::RandomBelow(seed, 12) == 0;
	const int32_t move = LeashMove(d2, fighting, wander);
	++g_moves[move];
	if (move == MoveNone) {
		return 0;
	}
	// shortcut: PD2 walks back with its own mover (0x10356a40) and measures to the owner's path end while the owner
	// moves; D2R's mover kinds and the owner's position stand in. Upgrade if pets lag a running owner.
	return g_mover(game, owner, pet, move, 0, 0, static_cast<uint8_t>(move == MoveWarp ? 0 : radius));
}

auto SummonLeash(Game* game, Unit* pet, Unit* target, Unit* owner, int32_t flag, void* tick, int32_t a7,
                 int32_t radius) noexcept -> uint64_t {
	const auto from = reinterpret_cast<uint64_t>(_ReturnAddress());
	if (from != g_leashReturn[0] && from != g_leashReturn[1]) {
		return g_leashOriginal(game, pet, target, owner, flag, tick, a7, radius);
	}
	return Pd2Leash(game, pet, target, owner, radius);
}

// --- Shadow Warrior (monai row 105, #1800) ---------------------------------------------------------------------------
// PD2 replaces 1.13c's ShadowWarrior init (D2Game +0xa4450) and think (+0xac410) with its own (ProjectDiablo.dll
// 0x10360a10, 0x10360240 in PD2.gpr's build). D2R's row 105 (0x5cf3b0, 0x5ceea0, live in the AI table at 0x2397bb8)
// is 1.13c's code. What PD2 changes (docs/features/core-rules.md has the addresses):
// - init: the pet also gets every class skill its owner has, once, at the level D2R's think gives a copied skill
//   (owner level / 2 + Shadow Warrior level / 3). D2R's think instead copies the owner's left and right skill on every
//   think, at the live level, any skill; PD2's think only looks those two ids up in the pet's own skills.
// - think: first it casts a buff (aitype 1) whose aurastate the owner has and the pet lacks (Burst of Speed, Fade,
//   Blade Shield, Venom); it never drops its target for distance (D2R: beyond aip1 or owner beyond aip2); it leashes
//   with PD2's rule (Pd2Leash); no chance (aip3 - 2 x level) to swing a plain attack; a skill passes its aitype test
//   (rewritten, ShadowWarriorUsable) and then casts 3 in 4 once the cast delay ran out (D2R: a mana roll and the aip8
//   counter); it walks to its target only for a melee skill.
// The think is replaced whole (most of it differs); the init is wrapped (D2R's, then PD2's skill grant). No SDK service
// runs an AI, so both are inline hooks; every engine function is the callee of a call in D2R's own row-105 code.
// PD2's patch of a call at 1.13c D2Game +0xac550 (the leash in 1.13c's think) is dead code there: row 105 is the only
// row with that think and PD2 replaced it.

// The AI tick D2R passes an AI function (game, unit, tick), and its AI control block (tick +0)
constexpr int TickControl = 0x00, TickTarget = 0x10, TickFlag = 0x24, TickMonStats = 0x28, TickDifficulty = 0x38;
constexpr int AiNextFrame = 0x44, AiCounter = 0x48, AiLevel = 0x4c;
constexpr int UnitSkills = 0x100, SkillsFirst = 0x00, SkillNext = 0x08;  // the unit's skill list (0x34b6e0, 0x33dcd0)
constexpr int MonAip4 = 0xa2, MonAip8 = 0xba, MonAip8Hell = 0xbe;    // monstats aip4[difficulty], aip8 by difficulty
constexpr int SkillAiType = 0x2d8, SkillAnimMode = 0x234;              // skills.txt record (0x5cf650, 0x5cf33c)
constexpr const char* SwFile = "summon_ai.cpp";

using Tick = uint8_t;
using SwEventsFn = void (*)(Game*, Unit*, int32_t, int32_t) noexcept;
using SwOwnerFn = Unit* (*)(Unit*) noexcept;
using SwSkillOfFn = void* (*)(Unit*) noexcept;                                // owner's left / right skill
using SwSkillIdFn = int32_t (*)(void* skill, const char* file, int32_t line) noexcept;
using SwAddSkillFn = void (*)(Unit*, int32_t id, int32_t level, int32_t, const char* file, int32_t line) noexcept;
using SwFindSkillFn = void* (*)(Unit*, int32_t id, int32_t flags) noexcept;    // (unit, id, -1)
using SwOwnerSkillFn = void* (*)(Unit*, int32_t id, int32_t) noexcept;         // (owner, id, 0), D2R's init
using SwRangeFn = int32_t (*)(Unit*, void* skill) noexcept;                   // 1 = melee
using SwWalkFn = void (*)(Game*, Unit*, Unit* target) noexcept;
using SwModeFn = uint32_t (*)(void* skill) noexcept;
using SwUseSkillFn = uint64_t (*)(Game*, Unit*, uint8_t mode, int32_t id, Unit* target, int32_t x, int32_t y, uint8_t) noexcept;
using SwRecordFn = uint8_t* (*)(uint8_t dataFlag, int32_t id) noexcept;
using SwLevelFn = int32_t (*)(Unit*, void* skill, int32_t, int32_t) noexcept;  // (unit, skill, 1, 0)
using SwFramesFn = int32_t (*)(uint8_t dataFlag, Unit*, int32_t mode, int32_t id, int32_t level) noexcept;
using SwIdleFn = void (*)(Game*, Unit*, int32_t frames) noexcept;
using SwUnitClassFn = int32_t (*)(Unit*, const char* file, int32_t line) noexcept;
using SwSkillClassFn = int32_t (*)(uint8_t dataFlag, int32_t id) noexcept;
using SwCanUseFn = int32_t (*)(Game*, Unit* owner, Unit* pet, int32_t id) noexcept;
using SwHasStateFn = int32_t (*)(Unit*, int32_t state) noexcept;
using SwAiFn = void (*)(Game*, Unit*, Tick*) noexcept;

struct SwEngine {
	SwEventsFn events;
	SwOwnerFn owner;
	SwSkillOfFn leftSkill, rightSkill;
	SwSkillIdFn skillId;
	SwAddSkillFn addSkill;
	SwFindSkillFn findSkill;
	SwOwnerSkillFn ownerSkill;
	SwRangeFn range;
	SwWalkFn walk;
	SwModeFn mode;
	SwUseSkillFn useSkill;
	SwRecordFn record;
	SwLevelFn level;
	SwFramesFn frames;
	SwIdleFn idle;
	SwUnitClassFn unitClass;
	SwSkillClassFn skillClass;
	SwCanUseFn canUse;
	SwHasStateFn hasState;
} g_sw {};
SwAiFn g_swInitOriginal = nullptr;

std::atomic<uint32_t> g_swThinks {0}, g_swBuffs {0}, g_swCasts {0}, g_swWalks {0}, g_swIdles {0}, g_swInits {0};

template <typename T>
auto Field(void* p, int offset) noexcept -> T& {
	return *reinterpret_cast<T*>(static_cast<uint8_t*>(p) + offset);
}

auto Ai(Tick* tick) noexcept -> uint8_t* {
	return d2r::At<uint8_t*>(tick, TickControl);
}

auto Seed(Unit* pet) noexcept -> uint32_t* {
	return d2r::api.unitSeed != nullptr ? d2r::api.unitSeed(pet) : nullptr;
}

auto FirstSkill(Unit* unit) noexcept -> void* {
	auto* skills = d2r::At<uint8_t*>(unit, UnitSkills);
	return skills != nullptr ? d2r::At<void*>(skills, SkillsFirst) : nullptr;
}

auto NextSkill(void* skill) noexcept -> void* {
	return d2r::At<void*>(skill, SkillNext);
}

// PD2's 10380e30: skills that never walk to their target (rows of PD2's skills.txt, equal to ours)
constexpr auto NeverWalks(int32_t id) noexcept -> bool {
	return id == 107 || id == 131 || id == 142 || id == 150 || id == 242;
}
static_assert(NeverWalks(107) && !NeverWalks(0) && !NeverWalks(254));

// PD2's aitype test (0x10360090), replacing D2R's 0x5cf650 (which also lets aitype 0, 3 and 6-10 through, and with the
// tick flag set only 4 and 13)
auto ShadowWarriorUsable(Game* game, Unit* owner, Unit* pet, int32_t id, Tick* tick) noexcept -> bool {
	const uint8_t* rec = g_sw.record(static_cast<uint8_t>(d2r::DataFlag(game)), id);
	if (rec == nullptr || g_sw.canUse(game, owner, pet, id) == 0) {
		return false;
	}
	Unit* target = d2r::At<Unit*>(tick, TickTarget);
	const int32_t state = d2r::At<uint16_t>(rec, d2r::rva::SkAuraState);
	const int32_t targetState = d2r::At<uint16_t>(rec, d2r::rva::SkAuraTargetState);
	switch (rec[SkillAiType]) {
	case 1:  // buff: the pet lacks its aurastate
		return state != 0 && g_sw.hasState(pet, state) == 0;
	case 2:  // curse-like: a target, neither the pet's nor the target's state on yet
		return target != nullptr && (state == 0 || g_sw.hasState(pet, state) == 0) &&
		       (targetState == 0 || g_sw.hasState(target, targetState) == 0);
	case 3:  // traps and shadows
		// shortcut: PD2 also caps this by a count in its own player data (+0x44 -> +0xcc, not decoded); D2R's pet caps
		// stand in. Upgrade if Shadow Warriors lay traps past the owner's.
		return target != nullptr;
	case 4:
	case 13:  // charge-up and Dragon Flight: only with the tick flag
		return target != nullptr && d2r::At<int32_t>(tick, TickFlag) != 0;
	case 5:
	case 11:
	case 12:
		return target != nullptr;
	default:  // 0 and 6-10 (passives, masteries): never
		return false;
	}
}

// PD2's skill try (0x103601c0, replacing D2R's 0x5cf420): the owner's class, the aitype test, then 3 in 4 once the
// cast delay ran out (D2R: a mana-cost roll and the aip8 counter)
auto ShadowWarriorTry(Game* game, Unit* owner, Unit* pet, int32_t id, Tick* tick) noexcept -> bool {
	if (g_sw.unitClass(owner, SwFile, __LINE__) != g_sw.skillClass(static_cast<uint8_t>(d2r::DataFlag(game)), id) ||
	    !ShadowWarriorUsable(game, owner, pet, id, tick)) {
		return false;
	}
	if (id == 0) {
		return true;
	}
	uint32_t* seed = Seed(pet);
	return d2r::At<int32_t>(Ai(tick), AiNextFrame) <= d2r::At<int32_t>(game, d2r::rva::GameFrame) && seed != nullptr &&
	       d2r::RandomBelow(seed, 100) >= 25;
}

// Casts `skill` at `target` (null: on itself) and sets the next think after its animation, as D2R's think does
auto ShadowWarriorCast(Game* game, Unit* pet, void* skill, int32_t id, Unit* target, Tick* tick) noexcept -> void {
	const uint8_t dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	g_sw.useSkill(game, pet, static_cast<uint8_t>(g_sw.mode(skill)), id, target, 0, 0, 0);
	const uint8_t* rec = g_sw.record(dataFlag, id);
	const int32_t level = g_sw.level(pet, skill, 1, 0);
	const int32_t frames = rec != nullptr ? g_sw.frames(dataFlag, pet, d2r::At<int32_t>(rec, SkillAnimMode), id,
	                                                    level > 1 ? level : 1)
	                                      : 0;
	Field<int32_t>(Ai(tick), AiNextFrame) = d2r::At<int32_t>(game, d2r::rva::GameFrame) + 0x12 + frames / 3;
}

auto ShadowWarriorIdle(Game* game, Unit* pet, int32_t frames) noexcept -> void {
	++g_swIdles;
	g_sw.idle(game, pet, frames);
}

// PD2's think (0x10360240), in its order
auto ShadowWarriorThink(Game* game, Unit* pet, Tick* tick) noexcept -> void {
	const uint32_t n = ++g_swThinks;
	if (game == nullptr || pet == nullptr || tick == nullptr) {
		return;
	}
	Unit* owner = g_sw.owner(pet);
	if (owner == nullptr) {
		ShadowWarriorIdle(game, pet, 100);
		return;
	}
	const uint8_t dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	// a buff the owner has and the pet lacks comes first, target or not
	for (void* skill = FirstSkill(pet); skill != nullptr; skill = NextSkill(skill)) {
		const int32_t id = g_sw.skillId(skill, SwFile, __LINE__);
		const uint8_t* rec = g_sw.record(dataFlag, id);
		const int32_t state = rec != nullptr ? d2r::At<uint16_t>(rec, d2r::rva::SkAuraState) : 0;
		if (rec != nullptr && rec[SkillAiType] == 1 && state != 0 && g_sw.hasState(owner, state) != 0 &&
		    g_sw.hasState(pet, state) == 0) {
			++g_swBuffs;
			if (pd2::HotSample(n)) {
				plog::Info("summon ai: shadow warrior think %u: buff %d (the owner has state %d)", n, id, state);
			}
			ShadowWarriorCast(game, pet, skill, id, nullptr, tick);
			return;
		}
	}
	g_sw.events(game, pet, 2, 0);
	const uint8_t* mon = d2r::At<uint8_t*>(tick, TickMonStats);
	uint32_t span = d2r::At<uint16_t>(mon, MonAip8Hell);
	if (span < 2) {
		ShadowWarriorIdle(game, pet, 100);
		return;
	}
	span = span > 0xff ? 0x100 : span;
	uint8_t* ai = Ai(tick);
	const uint8_t difficulty = d2r::At<uint8_t>(tick, TickDifficulty);
	int32_t& counter = Field<int32_t>(ai, AiCounter);
	counter += -1 - d2r::At<int16_t>(mon, MonAip4 + difficulty * 2);
	if (counter < 0 || counter > static_cast<int32_t>(span * 0x40)) {
		counter = 0;
	}
	// no target drop for distance: PD2 keeps the target and lets its leash decide
	Unit* target = d2r::At<Unit*>(tick, TickTarget);
	if (Pd2Leash(game, pet, target, owner, 6) != 0) {
		return;
	}
	void* ownerLeft = g_sw.leftSkill(owner);
	void* ownerRight = g_sw.rightSkill(owner);
	if (ownerLeft == nullptr || ownerRight == nullptr) {
		ShadowWarriorIdle(game, pet, 25);
		return;
	}
	// the pet's own copies (from the init), not a fresh copy of the owner's
	void* left = g_sw.findSkill(pet, g_sw.skillId(ownerLeft, SwFile, __LINE__), -1);
	void* right = g_sw.findSkill(pet, g_sw.skillId(ownerRight, SwFile, __LINE__), -1);
	if (left == nullptr && right == nullptr) {
		ShadowWarriorIdle(game, pet, 25);
		return;
	}
	uint32_t* seed = Seed(pet);
	void* skill = left == nullptr ? right : right != nullptr && seed != nullptr && d2r::RandomBelow(seed, 2) == 0 ? right : left;
	int32_t id = g_sw.skillId(skill, SwFile, __LINE__);
	if (!ShadowWarriorTry(game, owner, pet, id, tick)) {
		skill = skill == right && left != nullptr ? left : right != nullptr ? right : skill;
		id = g_sw.skillId(skill, SwFile, __LINE__);
		if (!ShadowWarriorTry(game, owner, pet, id, tick)) {
			id = 0;
			skill = g_sw.findSkill(pet, 0, -1);
			if (skill == nullptr) {
				g_sw.addSkill(pet, 0, 1, 0, SwFile, __LINE__);
				skill = g_sw.findSkill(pet, 0, -1);
			}
		}
	}
	if (skill == nullptr) {
		ShadowWarriorIdle(game, pet, 25);
		return;
	}
	const int32_t range = g_sw.range(pet, skill);
	if (range == 1 && d2r::At<int32_t>(tick, TickFlag) == 0 && !NeverWalks(id)) {
		if (target == nullptr) {
			// shortcut: PD2 walks to no target (the walk fails and it thinks again next frame); idle as D2R does
			ShadowWarriorIdle(game, pet, 25);
			return;
		}
		++g_swWalks;
		if (pd2::HotSample(n)) {
			plog::Info("summon ai: shadow warrior think %u: walks to use skill %d", n, id);
		}
		g_sw.walk(game, pet, target);
		return;
	}
	if (range != 0 && target == nullptr) {
		return;  // PD2: nothing to cast at
	}
	++g_swCasts;
	if (pd2::HotSample(n)) {
		plog::Info("summon ai: shadow warrior think %u: casts skill %d%s", n, id, target != nullptr ? " at its target" : "");
	}
	ShadowWarriorCast(game, pet, skill, id, target, tick);
}

// D2R's init, then PD2's grant (0x10360a10): every class skill of a player owner, once
auto ShadowWarriorInit(Game* game, Unit* pet, Tick* tick) noexcept -> void {
	g_swInitOriginal(game, pet, tick);
	if (game == nullptr || pet == nullptr || tick == nullptr) {
		return;
	}
	Unit* owner = g_sw.owner(pet);
	const uint8_t* mon = d2r::At<uint8_t*>(tick, TickMonStats);
	if (owner == nullptr || d2r::At<int32_t>(owner, d2r::rva::UnitType) != 0 || mon == nullptr ||
	    g_sw.ownerSkill(owner, d2r::At<int16_t>(mon, MonAip8), 0) == nullptr) {
		return;
	}
	const uint8_t dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const int32_t ownerClass = g_sw.unitClass(owner, SwFile, __LINE__);
	const int32_t swLevel = d2r::At<int32_t>(Ai(tick), AiLevel);
	int32_t granted = 0;
	for (void* skill = FirstSkill(owner); skill != nullptr; skill = NextSkill(skill)) {
		const int32_t id = g_sw.skillId(skill, SwFile, __LINE__);
		// shortcut: PD2 checks its own per-class skill list (0x1037fd70); the skill's charclass stands in
		if (g_sw.skillClass(dataFlag, id) != ownerClass) {
			continue;
		}
		const int32_t level = swLevel / 3 + g_sw.level(owner, skill, 1, 0) / 2;  // D2R's 0x5c5ef0
		g_sw.addSkill(pet, id, level > 1 ? level : 1, 0, SwFile, __LINE__);
		++granted;
	}
	if (pd2::HotSample(g_swInits)) {
		plog::Info("summon ai: shadow warrior init: %d class skills of its owner (Shadow Warrior level %d)", granted, swLevel);
	}
}

// The leash's entry: mov [rsp+10],rbx / mov [rsp+18],rbp / mov [rsp+20],rsi
constexpr uint8_t LeashEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20};
constexpr const char* LeashPattern = "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 48 89 4C 24 08 57 41 54 41 55 41 56 41 57 "
                                     "48 83 EC 60 4C 8B F9 49 8B F1 49 8B C9 49 8B D8 4C 8B F2 E8";
// In the leash, its one mover call (rcx game, rdx owner, r8 pet, r9d kind, two zeros, the radius byte); E8 at +19
constexpr const char* MoverCallPattern = "40 88 6C 24 30 4D 8B C6 89 7C 24 28 48 8B D6 89 7C 24 20 E8";
// The think's two leash calls, to their return addresses: the aip2(H) gate (+0x41) and the near-owner call (+0x35)
constexpr const char* LeashCall1Pattern =
	"0F BF 8A 9A 00 00 00 0F AF C9 3B C1 7E 3B 48 8B 45 88 45 33 C0 4C 8B 4D 98 48 8B D7 C7 44 24 38 06 00 00 00 49 8B "
	"CD C7 44 24 30 00 00 00 00 48 89 44 24 28 8B 44 24 68 89 44 24 20 E8 ?? ?? ?? ?? 85 C0";
constexpr const char* LeashCall2Pattern =
	"3D 90 00 00 00 7F 3C 8B 44 24 68 4D 8B CF C7 44 24 38 06 00 00 00 4D 8B C5 C7 44 24 30 00 00 00 00 48 8B D7 4C 89 "
	"74 24 28 49 8B CC 89 44 24 20 E8 ?? ?? ?? ?? 85 C0";
// The think's calls of the path-target getter (E8 at +6) and of the dead test (E8 at +3)
constexpr const char* PathTargetCallPattern = "48 8B D7 49 8B CD E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 75 12";
constexpr const char* IsDeadCallPattern = "48 8B C8 E8 ?? ?? ?? ?? 85 C0 75 17 4C 8B C3 48 8B D7 49 8B CC E8";

// A 2-byte patch: pattern, offset of the patched bytes, D2R's bytes, PD2's
struct Branch {
	const char* name;
	const char* pattern;
	int offset;
	uint8_t from[2];
	uint8_t to[2];
};
// PD2's five `jmp`s, D2R's jcc in the ShadowMaster think (same displacement)
constexpr Branch Branches[] = {
	{"buff casts", "E8 ?? ?? ?? ?? 83 F8 01 75 41 83 7C 24 68 00 74 24", 8, {0x75, 0x41}, {0xeb, 0x41}},
	{"no owner target", "E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 74 25 48 8B C8 E8 ?? ?? ?? ?? 85 C0 75 17 4C 8B C3", 23, {0x75, 0x17}, {0xeb, 0x17}},
	{"no basic attack", "44 0F 4C F0 83 7C 24 68 00 74 73 48 8B CF", 9, {0x74, 0x73}, {0xeb, 0x73}},
	{"no crowd step", "83 FA 03 7E 6E 8B 06 48 69 C8 C5 90 C6 6A", 3, {0x7e, 0x6e}, {0xeb, 0x6e}},
	{"no fallback attack", "4D 85 FF 0F 85 ?? ?? ?? ?? 4D 85 ED 74 45 45 33 C9 45 33 C0 49 8B D5 48 8B CF E8", 12, {0x74, 0x45}, {0xeb, 0x45}},
};
// The vine thinks (D2R 0x5d0630, 0x5c83c0)
constexpr Branch Vines[] = {
	// Vines 0x5d07ad: `lea edx,[rax+2]` starts the state-2 / stat-45 == 100 test; jmp +0x28 past it to its attack
	{"Poison Creeper", "48 85 ED 0F 84 ?? ?? ?? ?? 8D 50 02 48 8B CD E8 ?? ?? ?? ?? 85 C0 0F 85 ?? ?? ?? ?? 45 33 C0 8D 50 2D 48 8B CD E8 ?? ?? ?? ?? 83 F8 64 0F 84",
	 9, {0x8d, 0x50}, {0xeb, 0x28}},
	// CycleOfLife 0x5c86a0: `test r15d,r15d / je` on the "owner below max life or mana" flag; the je becomes a nop
	{"Carrion Vine", "83 BC 24 C8 00 00 00 00 74 57 45 85 FF 74 52 4C 8B 7C 24 60", 13, {0x74, 0x52}, {0x66, 0x90}},
};

// The rva a `call rel32` at `rva` goes to
auto CallTarget(uint64_t exeBase, uint64_t rva) noexcept -> uint64_t {
	int32_t rel = 0;
	std::memcpy(&rel, reinterpret_cast<const void*>(exeBase + rva + 1), sizeof(rel));
	return rva + 5 + static_cast<int64_t>(rel);
}

auto Scan(const D2RL::PluginContext* context, const char* name, const char* pattern, int offset, uint64_t* rva) noexcept
	-> bool {
	const int n = sigscan::ScanUnique(context->exeBase, pattern, offset, rva);
	if (n != 1) {
		plog::Warn("summon ai: skipped %s: %d matches", name, n);
		return false;
	}
	return true;
}

auto PatchBranches(const D2RL::PluginContext* context, const Branch* branches, const uint64_t* rvas, size_t count) noexcept
	-> bool {
	const D2RL::MutationService* mutations = nullptr;
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize) ||
	    mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		return false;
	}
	for (size_t i = 0; i < count; ++i) {
		const D2RL::Mutations::BytePatchRequest patch{.structSize = D2RL::Mutations::BytePatchRequestSize,
		                                              .rva = rvas[i],
		                                              .expected = branches[i].from,
		                                              .expectedSize = 2,
		                                              .bytes = branches[i].to,
		                                              .size = 2};
		D2RL::Mutations::OperationHandle operation = 0;
		if (mutations->stageBytePatch(context, transaction, &patch, &operation) != D2RL::Mutations::Result::Success) {
			mutations->cancelTransaction(context, transaction);
			return false;
		}
	}
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	if (mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		return false;
	}
	return true;
}

// D2R's row-105 code: the think's and the init's entries, and the call sites their engine functions are read from
constexpr uint8_t SwThinkEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
constexpr uint8_t SwInitEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20};
struct SwSite {
	const char* name;
	const char* pattern;
	int calls[4];  // offsets of the E8s whose callees fill g_sw in order; 0 ends
};
// The think (0x5ceea0): its entry, events (0x48b890) and owner (0x4a53c0)
constexpr const char* SwThinkPattern =
	"48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 60 49 8B 68 28 45 33 C9 41 0F B6 70 38 4D 8B F8 49 8B 58 "
	"10 4C 8B F2 41 8B 78 24 4C 8B E9 41 8B 40 20 45 8D 41 02 48 89 6C 24 50 40 88 B4 24 B0 00 00 00 48 89 5C 24 48 89 "
	"7C 24 40 89 84 24 B8 00 00 00 E8 ?? ?? ?? ?? 49 8B CE E8";
// The init (0x5cf3b0): its entry and the owner's Shadow Warrior skill (0x33dd40)
constexpr const char* SwInitPattern =
	"48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 49 8B 30 48 8B CA 49 8B F8 C7 46 4C 01 00 00 00 E8 ?? ?? ?? ?? 48 8B "
	"D8 48 85 C0 74 30 48 8B 4F 28 45 33 C0 0F BF 91 BA 00 00 00 48 8B C8 E8";
constexpr SwSite SwSites[] = {
	{"think left/right", "85 C0 0F 85 ?? ?? ?? ?? 48 85 DB 0F 84 ?? ?? ?? ?? 49 8B CC E8 ?? ?? ?? ?? 49 8B CC 48 8B D8 E8", {0x14, 0x1f}},
	{"think skill id", "41 B8 D7 0B 00 00 48 8B D5 48 8B CB E8", {0xc}},
	{"think add skill", "C7 44 24 28 DD 0B 00 00 48 8D 05 ?? ?? ?? ?? 45 33 C9 44 8B C3 48 89 44 24 20 8B D6 49 8B CE E8", {0x1f}},
	{"think find skill", "41 B8 FF FF FF FF 8B D6 49 8B CE E8 ?? ?? ?? ?? 41 B8 FF FF FF FF 48 89 44 24 58", {0xb}},
	{"think range", "48 8B D7 49 8B CE 44 8B E0 E8", {0x9}},
	{"think walk", "4C 8B 44 24 48 49 8B D6 49 8B CD E8", {0xb}},
	{"think mode", "48 8B D3 48 8B CF E8 ?? ?? ?? ?? 48 8B CF 8B D8 E8", {0x10}},
	{"think cast",
	 "44 8B CB 89 4C 24 28 44 0F B6 C0 48 8B 4C 24 48 49 8B D6 48 89 4C 24 20 49 8B CD E8 ?? ?? ?? ?? 41 0F B6 8D 06 01 00 "
	 "00 41 8B D4 E8 ?? ?? ?? ?? 45 33 C9 44 8B C6 48 8B D7 49 8B CE 48 8B D8 E8 ?? ?? ?? ?? 44 8B 83 34 02 00 00 3B C6 41 "
	 "0F B6 8D 06 01 00 00 45 8B CC 0F 4F F0 49 8B D6 89 74 24 20 E8",
	 {0x1b, 0x2b, 0x3f, 0x62}},
	{"think idle", "41 B8 19 00 00 00 49 8B D6 49 8B CD E8", {0xc}},
	{"try classes",
	 "41 B8 30 0B 00 00 48 8D 15 ?? ?? ?? ?? 48 8B CD E8 ?? ?? ?? ?? 44 8B BC 24 80 00 00 00 8B D8 41 0F B6 8E 06 01 00 00 "
	 "41 8B D7 E8",
	 {0x10, 0x2a}},
	{"aitype can use", "45 8B CE 4C 8B C7 49 8B D7 49 8B CD E8", {0xc}},
	{"aitype state", "0F BF 83 A0 00 00 00 66 85 C0 7E 0E 8B D0 48 8B CF E8", {0x11}},
};

// Finds row 105's code; false (and nothing set) when any site is missing
auto FindShadowWarrior(const D2RL::PluginContext* context, uint64_t* think, uint64_t* init) noexcept -> bool {
	if (!Scan(context, "Shadow Warrior think", SwThinkPattern, 0, think) ||
	    !Scan(context, "Shadow Warrior init", SwInitPattern, 0, init)) {
		return false;
	}
	const uint64_t base = context->exeBase;
	uint64_t callees[20] = {};
	size_t n = 0;
	callees[n++] = CallTarget(base, *think + 0x56);  // events
	callees[n++] = CallTarget(base, *think + 0x5e);  // owner
	for (const SwSite& site : SwSites) {
		uint64_t rva = 0;
		if (!Scan(context, site.name, site.pattern, 0, &rva)) {
			return false;
		}
		for (int call : site.calls) {
			if (call != 0 && n < std::size(callees) - 1) {
				callees[n++] = CallTarget(base, rva + call);
			}
		}
	}
	callees[n++] = CallTarget(base, *init + 0x3d);  // the owner's Shadow Warrior skill
	if (n != std::size(callees)) {
		return false;
	}
	const auto at = [&](size_t i) { return reinterpret_cast<void*>(base + callees[i]); };
	g_sw = {.events = reinterpret_cast<SwEventsFn>(at(0)),
	        .owner = reinterpret_cast<SwOwnerFn>(at(1)),
	        .leftSkill = reinterpret_cast<SwSkillOfFn>(at(2)),
	        .rightSkill = reinterpret_cast<SwSkillOfFn>(at(3)),
	        .skillId = reinterpret_cast<SwSkillIdFn>(at(4)),
	        .addSkill = reinterpret_cast<SwAddSkillFn>(at(5)),
	        .findSkill = reinterpret_cast<SwFindSkillFn>(at(6)),
	        .ownerSkill = reinterpret_cast<SwOwnerSkillFn>(at(19)),
	        .range = reinterpret_cast<SwRangeFn>(at(7)),
	        .walk = reinterpret_cast<SwWalkFn>(at(8)),
	        .mode = reinterpret_cast<SwModeFn>(at(9)),
	        .useSkill = reinterpret_cast<SwUseSkillFn>(at(10)),
	        .record = reinterpret_cast<SwRecordFn>(at(11)),
	        .level = reinterpret_cast<SwLevelFn>(at(12)),
	        .frames = reinterpret_cast<SwFramesFn>(at(13)),
	        .idle = reinterpret_cast<SwIdleFn>(at(14)),
	        .unitClass = reinterpret_cast<SwUnitClassFn>(at(15)),
	        .skillClass = reinterpret_cast<SwSkillClassFn>(at(16)),
	        .canUse = reinterpret_cast<SwCanUseFn>(at(17)),
	        .hasState = reinterpret_cast<SwHasStateFn>(at(18))};
	return true;
}

}  // namespace

auto InstallSummonAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	// Every address first: a patched branch would hide the dead-test call's signature
	uint64_t leash = 0, mover = 0, call1 = 0, call2 = 0, pathTarget = 0, isDead = 0, branches[std::size(Branches)] = {};
	bool found = Scan(context, "leash", LeashPattern, 0, &leash) && Scan(context, "mover call", MoverCallPattern, 19, &mover) &&
	             Scan(context, "leash call 1", LeashCall1Pattern, 0x41, &call1) &&
	             Scan(context, "leash call 2", LeashCall2Pattern, 0x35, &call2) &&
	             Scan(context, "path target call", PathTargetCallPattern, 6, &pathTarget) &&
	             Scan(context, "dead test call", IsDeadCallPattern, 3, &isDead);
	for (size_t i = 0; found && i < std::size(Branches); ++i) {
		found = Scan(context, Branches[i].name, Branches[i].pattern, Branches[i].offset, &branches[i]);
	}
	bool hooked = false, patched = false;
	if (found) {
		const uint64_t base = context->exeBase;
		g_mover = reinterpret_cast<MoverFn>(base + CallTarget(base, mover));
		g_pathTarget = reinterpret_cast<PathTargetFn>(base + CallTarget(base, pathTarget));
		g_isDead = reinterpret_cast<IsDeadFn>(base + CallTarget(base, isDead));
		g_leashReturn[0] = base + call1;
		g_leashReturn[1] = base + call2;
		plog::Info("summon ai: leash 0x%llx, mover 0x%llx, think calls return at 0x%llx and 0x%llx",
		           static_cast<unsigned long long>(leash), static_cast<unsigned long long>(CallTarget(base, mover)),
		           static_cast<unsigned long long>(call1), static_cast<unsigned long long>(call2));
		hooked = context->InstallInlineHook(leash, LeashEntry, sizeof(LeashEntry), &SummonLeash, &g_leashOriginal);
		patched = PatchBranches(context, Branches, branches, std::size(Branches));
	}
	uint64_t vines[std::size(Vines)] = {};
	bool vinesFound = true;
	for (size_t i = 0; vinesFound && i < std::size(Vines); ++i) {
		vinesFound = Scan(context, Vines[i].name, Vines[i].pattern, Vines[i].offset, &vines[i]);
	}
	const bool vinesPatched = vinesFound && PatchBranches(context, Vines, vines, std::size(Vines));
	// The Shadow Warrior uses PD2's leash, so it needs the ShadowMaster addresses too. The init first: alone it only
	// adds skills D2R's think never picks, where PD2's think alone would find none.
	uint64_t swThink = 0, swInit = 0;
	const bool swFound = found && FindShadowWarrior(context, &swThink, &swInit);
	bool swHooked = false;
	if (swFound && context->InstallInlineHook(swInit, SwInitEntry, sizeof(SwInitEntry), &ShadowWarriorInit, &g_swInitOriginal)) {
		swHooked = context->InstallInlineHook(swThink, SwThinkEntry, sizeof(SwThinkEntry), &ShadowWarriorThink);
		plog::Info("summon ai: Shadow Warrior init 0x%llx, think 0x%llx%s", static_cast<unsigned long long>(swInit),
		           static_cast<unsigned long long>(swThink), swHooked ? "" : " (think not hooked)");
	}
	std::snprintf(report, reportSize, "ShadowMaster leash%s; ShadowMaster branches%s; vines%s; Shadow Warrior AI%s",
	              !found ? " FAILED (signature)" : hooked ? "" : " FAILED (bytes differ)",
	              !found ? " FAILED (signature)" : patched ? "" : " FAILED (bytes differ)",
	              !vinesFound ? " FAILED (signature)" : vinesPatched ? "" : " FAILED (bytes differ)",
	              !swFound ? " FAILED (signature)" : swHooked ? "" : " FAILED (bytes differ)");
	return (hooked ? 1 : 0) + (patched ? 1 : 0) + (vinesPatched ? 1 : 0) + (swHooked ? 1 : 0);
}

auto SummonAiCount() noexcept -> int {
	return 4;
}

auto SummonAiCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size,
	              "summon leash calls %u: stay %u, walk back %u, warp %u, wander %u, step aside %u; shadow warrior inits "
	              "%u, thinks %u: buff %u, cast %u, walk %u, idle %u",
	              g_leashCalls.load(), g_moves[MoveNone].load(), g_moves[MoveBack].load(), g_moves[MoveWarp].load(),
	              g_moves[MoveWander].load(), g_moves[MoveAside].load(), g_swInits.load(), g_swThinks.load(),
	              g_swBuffs.load(), g_swCasts.load(), g_swWalks.load(), g_swIdles.load());
}
