// The final pair's shared life (docs/research/pd2-uber-bosses-dll.md, section 5, read again for #475).
//
// PD2's damage-type step (ProjectDiablo.dll 0x10302ca0, in place of 1.13c's 0x6fcfbd00): after the resists, when a
// clone (rathmaBoneClone or rathmaPoisonClone) takes a flagged type and both boss slots (game +0x2600 / +0x2604) are
// set, and the other clone is not in mode 0 or 12, the hit's damage of that type is halved (0x1037c4c0 with 50 / 100;
// the double at 0x1044fc08 is 50.0) and, unless the other has state 8, the other loses that half from its life
// (stat 6, not below 0). Poison (0x103055b0) puts the same poison (rate and length) on the other clone through
// 0x10305460, under the same slot and mode tests and without the state 8 one.
//
// No SDK service reports damage. The step is D2RCore's, called through a slot (rva::DamageTypeStepSlot): the port
// wraps the slot as item_rules.cpp wraps the event dispatcher, and hooks D2R's poison apply inline past its length test.
// The slots are the AI's (rathma_ai.cpp): the unit ids its think keeps per game.
#include "rathma_life.h"

#include "log.h"
#include "pd2_engine.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t BoneClone = 952, PoisonClone = 953;  // merged monstats ids (PD2 935, 936)
constexpr uint32_t BossKey = 0x55425230;  // 'UBR0' / 'UBR1', as rathma_ai.cpp: PD2's game +0x2600 / +0x2604
constexpr int32_t UnitMonster = 1;
constexpr int32_t ModeDeath = 0, ModeDead = 12;
constexpr int32_t StateResistAll = 8;
constexpr int32_t StatLife = 6;

using StepFn = uint64_t (*)(uint8_t* ctx, uint8_t* type, uint32_t flag) noexcept;
using PoisonFn = void (*)(Unit* attacker, Unit* victim, int32_t rate, int32_t length, int32_t a5, int32_t a6,
                          int32_t a7) noexcept;
StepFn g_stepOriginal = nullptr;
PoisonFn g_poisonOriginal = nullptr;

std::atomic<uint32_t> g_hits {0};
std::atomic<uint32_t> g_shared {0};
std::atomic<uint32_t> g_poisons {0};

// The first 20, then every 100th: enough to read both lives in a game check
constexpr auto Logged(uint32_t n) noexcept -> bool {
	return n <= 20 || n % 100 == 0;
}

auto Counter(Game* game, uint32_t key) noexcept -> int32_t {
	return *d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

auto Life(Unit* unit) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(unit, StatLife, 0);
}

// The live monster with that id: the game's monster lists are keyed by id & 0x7f
auto MonsterById(Game* game, uint32_t id) noexcept -> Unit* {
	using namespace d2r::rva;
	for (auto* u = At<Unit*>(game, GameMonsters + static_cast<int>(id & 0x7f) * 8); u != nullptr;
	     u = At<Unit*>(u, UnitListNext)) {
		if (At<uint32_t>(u, UnitIdField) == id) {
			return u;
		}
	}
	return nullptr;
}

// PD2's test: the victim is a clone, both slots are set, and the other slot's clone is not dying or dead; that
// clone, else null. PD2 takes the slot whatever it holds; the port also wants the other clone's class there, so an
// id left by Rathma or Mendeln never stands in for it.
auto Partner(Game* game, Unit* victim) noexcept -> Unit* {
	using namespace d2r::rva;
	if (game == nullptr || victim == nullptr || At<int32_t>(victim, 0) != UnitMonster) {
		return nullptr;
	}
	const int32_t classId = At<int32_t>(victim, UnitClassId);
	if (classId != BoneClone && classId != PoisonClone) {
		return nullptr;
	}
	const int32_t bone = Counter(game, BossKey), poison = Counter(game, BossKey + 1);
	if (bone == 0 || poison == 0) {
		return nullptr;
	}
	const bool isBone = classId == BoneClone;
	Unit* other = MonsterById(game, static_cast<uint32_t>(isBone ? poison : bone));
	if (other == nullptr || At<int32_t>(other, UnitClassId) != (isBone ? PoisonClone : BoneClone)) {
		return nullptr;
	}
	const int32_t mode = At<int32_t>(other, UnitMode);
	return mode != ModeDeath && mode != ModeDead ? other : nullptr;
}

auto SharedStep(uint8_t* ctx, uint8_t* type, uint32_t flag) noexcept -> uint64_t {
	using namespace d2r::rva;
	const uint64_t result = g_stepOriginal(ctx, type, flag);
	if (ctx == nullptr || type == nullptr || At<int32_t>(type, TypeMirrored) == 0) {
		return result;
	}
	Unit* victim = At<Unit*>(ctx, StepVictim);
	Unit* other = Partner(At<Game*>(ctx, StepGame), victim);
	auto* field = At<int32_t*>(type, TypeField);
	if (other == nullptr || field == nullptr) {
		return result;
	}
	const auto half = static_cast<int32_t>(static_cast<int64_t>(*field) * 50 / 100);
	*field = half;
	++g_hits;
	if (d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(other, StateResistAll) != 0) {
		return result;
	}
	const int32_t before = Life(other);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(other, StatLife,
	                                                                             std::max(0, before - half), 0);
	if (const uint32_t n = ++g_shared; half > 0 && Logged(n)) {
		plog::Info("rathma life: class %d takes %d (1/256 life, halved) at life %d; class %d too: life %d -> %d "
		           "(%u shared so far)",
		           At<int32_t>(victim, UnitClassId), half, Life(victim), At<int32_t>(other, UnitClassId), before,
		           Life(other), n);
	}
	return result;
}

auto SharedPoison(Unit* attacker, Unit* victim, int32_t rate, int32_t length, int32_t a5, int32_t a6,
                 int32_t a7) noexcept -> void {
	g_poisonOriginal(attacker, victim, rate, length, a5, a6, a7);
	// The hook sits past the length test; PD2's own guard also wants the attacker
	if (attacker == nullptr || victim == nullptr || (rate <= 0 && length <= 0)) {
		return;
	}
	if (Unit* other = Partner(At<Game*>(victim, d2r::rva::UnitGame), victim); other != nullptr) {
		g_poisonOriginal(attacker, other, rate, length, a5, a6, a7);
		if (const uint32_t n = ++g_poisons; Logged(n)) {
			plog::Info("rathma life: poison %d for %d frames on class %d, class %d too (%u so far)", rate, length,
			           At<int32_t>(victim, d2r::rva::UnitClassId), At<int32_t>(other, d2r::rva::UnitClassId), n);
		}
	}
}

}  // namespace

auto InstallRathmaLife(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	// D2RCore's step is in the slot; the wrapper calls it
	g_stepOriginal = *reinterpret_cast<StepFn const*>(d2r::imageBase + DamageTypeStepSlot);
	const uint64_t stepSlot = reinterpret_cast<uint64_t>(g_stepOriginal);
	const bool step = stepSlot != 0 && context->PatchWriteU64(DamageTypeStepSlot, &stepSlot, sizeof(stepSlot),
	                                                          reinterpret_cast<uint64_t>(&SharedStep));
	const bool poison = context->InstallInlineHook(PoisonApplyHook, PoisonApplyHookEntry, sizeof(PoisonApplyHookEntry),
	                                               &SharedPoison, &g_poisonOriginal);
	std::snprintf(report, reportSize, "damage step%s; poison%s", step ? "" : " FAILED (slot write)",
	              poison ? "" : " FAILED (bytes differ)");
	return (step ? 1 : 0) + (poison ? 1 : 0);
}

auto RathmaLifeCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "rathma life: %u halved hits, %u taken from the other clone, %u poisons copied",
	              g_hits.load(), g_shared.load(), g_poisons.load());
}
