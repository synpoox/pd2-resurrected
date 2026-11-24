// Decoy (Dopplezon, srvdo 49) on a controller and after a rejoin (#2068).
//
// Rejoin: Bind and Summon (a companion plugin) saves a player's pets and, when the character joins, casts their skills
// again through the srvdo (seen: srvdo 49 runs for skill 28 at the join, the caster standing in town). Its options are
// per class, so Amazon persistence brings the Decoy back with the Valkyrie. PD2 has no persistence; a Decoy is a timed
// pet. A real cast runs in the cast mode (SC); the restore does not. So srvdo 49's slot is wrapped to mark a real cast,
// and monai row 106's init slot (the ShadowMaster init, summon_ai.cpp; every Decoy runs it inside srvdo 49's creator) is
// wrapped: a Decoy (monstats aip8 = Dopplezon) made outside a real cast gets the lifetime event srvdo 49 queues
// (AddUnitEvent type 7, key 0x15), due in 2 frames, so D2R removes it as an expired Decoy. The Valkyrie and other pets
// are untouched.
// Controller: see AimAtNearestEnemy.
// Slot swaps, not hooks: summon_ai's inline hook on the init's body stays under ours. No SDK service creates, removes or
// aims pets.
#include "common.h"
#include "registry.h"

#include "hot_log.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

using d2r::Game;
using d2r::Unit;

constexpr int SrvDoSummonSlot = 49;  // Shadow Warrior, Shadow Master, Dopplezon (skills.txt srvdofunc)
constexpr int ShadowMasterRow = 106;  // monai row of the Valkyrie, the Decoy and the Shadow Master
constexpr int16_t Dopplezon = 28;
constexpr int32_t PlayerModeCast = 10;  // plrmode SC, Decoy's anim
constexpr int32_t UnitPlayer = 0;
constexpr int32_t BesideCaster = 2;       // subtiles: a point this close is a controller's native aim, not a choice
constexpr int32_t AutoTargetRange = 20;   // subtiles searched for the enemy, about half the screen's width
constexpr int TickMonStats = 0x28, MonAip8 = 0xba;  // the AI tick's monstats record; its aip8 (summon_ai.cpp)
constexpr int EventPetEnd = 7, PetEndKey = 0x15, PetEndFrames = 2;  // srvdo 49's lifetime event (0x5710c0)

using SrvDoFn = int64_t (*)(Game*, Unit*, int32_t, int32_t) noexcept;
using InitFn = void (*)(Game*, Unit*, void*) noexcept;
using AddEventFn = void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept;

SrvDoFn g_srvDo = nullptr;
InitFn g_init = nullptr;
// srvdo 49 runs on the game thread, and the init it reaches runs inside it
thread_local int g_casting = 0;  // inside a real cast
thread_local int g_inside = 0;   // inside srvdo 49 at all
thread_local Unit* g_restored = nullptr;  // a Decoy made by the srvdo 49 call running now, outside a real cast
std::atomic<uint32_t> g_removed {0};

// A controller's native aim sends a Decoy cast with no target unit and a point beside the caster (seen: 1 subtile off);
// the client's auto-target never reaches the server. Such a cast goes to the nearest enemy instead, through the
// caster's path target, which srvdo 49 reads as the skill's point (SkillTargetXY 0x432b70 with no target unit). A
// mouse click or Controller QoL's reticle sends the point aimed at, further out, and is left alone.
auto AimAtNearestEnemy(Game* game, Unit* caster) noexcept -> void {
	using namespace d2r::rva;
	int32_t x = 0, y = 0, tx = 0, ty = 0;
	uint8_t* path = d2r::At<uint8_t*>(caster, UnitPath);
	if (path == nullptr || d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, caster) != nullptr ||
	    d2r::Engine<int32_t (*)(Game*, Unit*, int32_t*, int32_t*) noexcept>(SkillTargetXY)(game, caster, &tx, &ty) == 0 ||
	    !d2r::UnitXY(caster, &x, &y) || std::abs(tx - x) > BesideCaster || std::abs(ty - y) > BesideCaster) {
		return;
	}
	using NearestFn = Unit* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t, uint32_t, int32_t*) noexcept;
	Unit* enemy = d2r::Engine<NearestFn>(NearestTarget)(game, caster, x, y, AutoTargetRange, NearestTargetFlags,
	                                                    d2r::At<uint32_t>(caster, UnitIdField), nullptr);
	int32_t ex = 0, ey = 0;
	if (enemy == nullptr || !d2r::UnitXY(enemy, &ex, &ey)) {
		return;
	}
	d2r::Engine<void (*)(void*, int32_t, int32_t) noexcept>(PathSetTargetXY)(path, ex, ey);
}

// D2R's own end of a timed Decoy, 2 frames on
auto Remove(Game* game, Unit* pet) noexcept -> void {
	d2r::Engine<AddEventFn>(d2r::rva::AddUnitEvent)(game, pet, EventPetEnd,
	                                                d2r::At<int32_t>(game, d2r::rva::GameFrame) + PetEndFrames, PetEndKey, 0, 0);
	const uint32_t n = g_removed.fetch_add(1, std::memory_order_relaxed) + 1;
	plog::Info("decoy: Decoy %u made outside a cast (restored): removed, %u so far",
	           d2r::At<uint32_t>(pet, d2r::rva::UnitIdField), n);
}

auto SrvDoSummon(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	// A player's real cast runs in the cast mode; Bind and Summon's restore calls this function with the player standing
	const bool player = unit != nullptr && d2r::At<int32_t>(unit, d2r::rva::UnitType) == UnitPlayer;
	const bool cast = unit != nullptr && (!player || d2r::At<int32_t>(unit, d2r::rva::UnitMode) == PlayerModeCast);
	if (cast && player && skillId == Dopplezon) {
		AimAtNearestEnemy(game, unit);
	}
	g_casting += cast ? 1 : 0;
	++g_inside;
	Unit* const outer = g_restored;
	g_restored = nullptr;
	const int64_t ran = g_srvDo(game, unit, skillId, level);
	// After srvdo 49 has queued its own lifetime event, so ours (sooner) is the last one set
	if (g_restored != nullptr) {
		Remove(game, g_restored);
	}
	g_restored = outer;
	--g_inside;
	g_casting -= cast ? 1 : 0;
	return ran;
}

auto ShadowMasterInit(Game* game, Unit* pet, void* tick) noexcept -> void {
	g_init(game, pet, tick);
	const uint8_t* mon = tick != nullptr ? d2r::At<uint8_t*>(tick, TickMonStats) : nullptr;
	if (g_casting != 0 || game == nullptr || pet == nullptr || mon == nullptr || d2r::At<int16_t>(mon, MonAip8) != Dopplezon) {
		return;
	}
	if (g_inside != 0) {
		g_restored = pet;  // removed when srvdo 49 returns
	} else {
		Remove(game, pet);
	}
}

// Keeps the slot's current function and writes ours over it; false (nothing written) when the slot is empty
template <typename Fn>
auto Wrap(const D2RL::PluginContext* context, uint64_t slotRva, Fn& own, Fn function) noexcept -> bool {
	uint64_t current = 0;
	std::memcpy(&current, reinterpret_cast<const void*>(context->exeBase + slotRva), sizeof(current));
	own = reinterpret_cast<Fn>(current);
	return current != 0 && context->PatchWriteU64(slotRva, &current, sizeof(current), reinterpret_cast<uint64_t>(function));
}

}  // namespace

auto InstallDecoy(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	// The AI table's rows start 8 bytes before d2r::sig::AiTable's think column: {kind, init, think, next}
	const bool on = d2r::sig::SrvDoTable.state == sigscan::State::Ok && d2r::sig::AiTable.state == sigscan::State::Ok &&
	                Wrap(context, d2r::sig::SrvDoTable.rva + uint64_t {SrvDoSummonSlot} * 8, g_srvDo, &SrvDoSummon) &&
	                Wrap(context, d2r::sig::AiTable.rva + uint64_t {ShadowMasterRow} * d2r::rva::AiRowSize - 8, g_init,
	                     &ShadowMasterInit);
	plog::Info("decoy: restored Decoys removed, beside-caster casts aimed %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; decoy %s", on ? "on" : "FAILED");
	}
	return on;
}

PD2R_RULE(InstallDecoy, 240)
