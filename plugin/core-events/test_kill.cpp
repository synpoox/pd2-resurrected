// A game-check aid: kill or wound a monster the way the game does, so clicking through a boss's hiding and adds is not needed.
// The kill is the normal death path (KillQuietly's mode block, without UnitNoDrop): death listeners run and the treasure
// class rolls. The console thread never writes a unit: the work runs on the game thread
// through the SDK's ThreadService.
// shortcut: single player only (the first game found, the local player), like the other test aids.
#include "test_kill.h"

#include "ai_common.h"
#include "log.h"

#include <algorithm>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

const D2RL::ThreadService* g_threads = nullptr;

auto FirstGame() noexcept -> Game* {
	using namespace d2r::rva;
	for (uint32_t id = 1; id <= 0xffff; ++id) {
		const uint64_t key = d2r::Engine<uint64_t (*)(uint16_t) noexcept>(GameKey)(static_cast<uint16_t>(id));
		Game* game = key != 0 ? d2r::Engine<Game* (*)(uint64_t) noexcept>(GameById)(key) : nullptr;
		if (game != nullptr && At<uint16_t>(game, GameIdField) == id) {
			return game;
		}
	}
	return nullptr;
}

// The living monster in the player's level that matches (class or unit id), nearest to the player
auto Find(Game* game, Unit* player, const pd2::test_cmd::Cmd& cmd, bool* ownedByPlayer) noexcept -> Unit* {
	using namespace d2r::rva;
	const int32_t level = ai_common::LevelOf(player);
	int32_t px = 0, py = 0;
	d2r::UnitXY(player, &px, &py);
	Unit* best = nullptr;
	int32_t bestDist = 0x7fffffff;
	for (int bucket = 0; bucket < 128; ++bucket) {
		for (auto* unit = At<Unit*>(game, GameMonsters + bucket * 8); unit != nullptr; unit = At<Unit*>(unit, UnitListNext)) {
			const bool match = cmd.unitId != 0 ? At<uint32_t>(unit, UnitIdField) == cmd.unitId
			                                   : At<int32_t>(unit, UnitClassId) == cmd.classId;
			if (!match || ai_common::Dead(unit) || ai_common::LevelOf(unit) != level) {
				continue;
			}
			// A player's pet (merc, golem, summon): its owner is a player, the input that crashed the engine's death path
			Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, unit);
			if (owner != nullptr && At<int32_t>(owner, UnitType) == 0) {
				*ownedByPlayer = true;
				continue;
			}
			const int32_t dist = ai_common::Dist2(unit, px, py);
			if (dist < bestDist) {
				bestDist = dist;
				best = unit;
			}
		}
	}
	return best;
}

// The whole Cmd in the ThreadService's userData: kind (bits 0-7), percent (8-15), is-unit-id (16), the class or unit id (32-63)
auto Pack(const pd2::test_cmd::Cmd& c) noexcept -> uintptr_t {
	const uint64_t id = c.unitId != 0 ? c.unitId : static_cast<uint32_t>(c.classId);
	return static_cast<uintptr_t>(static_cast<uint64_t>(c.kind) | (static_cast<uint64_t>(c.percent) << 8) |
	                              (static_cast<uint64_t>(c.unitId != 0) << 16) | (id << 32));
}
auto Unpack(uintptr_t v) noexcept -> pd2::test_cmd::Cmd {
	pd2::test_cmd::Cmd c;
	c.kind = static_cast<pd2::test_cmd::Cmd::Kind>(v & 0xff);
	c.percent = static_cast<int32_t>((v >> 8) & 0xff);
	const auto id = static_cast<uint32_t>(v >> 32);
	if ((v >> 16) & 1) {
		c.unitId = id;
	} else {
		c.classId = static_cast<int32_t>(id);
	}
	return c;
}

// Game thread
void Run(const D2RL::PluginContext*, void* user) noexcept {
	using namespace d2r::rva;
	const pd2::test_cmd::Cmd cmd = Unpack(reinterpret_cast<uintptr_t>(user));
	Game* game = FirstGame();
	Unit* player = d2r::Engine<Unit* (*)(uint32_t) noexcept>(ClientPlayer)(d2r::Engine<uint32_t (*)() noexcept>(LocalPlayerSlot)());
	bool owned = false;
	Unit* monster = game != nullptr && player != nullptr ? Find(game, player, cmd, &owned) : nullptr;
	const bool kill = cmd.kind == pd2::test_cmd::Cmd::Kind::Kill;
	if (monster == nullptr && owned) {
		plog::Info("test aid: %s refused: every match in the player's level is a player's pet (merc, golem or summon)",
		           kill ? "kill" : "life");
		return;
	}
	if (monster == nullptr) {
		plog::Info("test aid: %s: no living monster %s %u in the player's level (game %s, player %s)", kill ? "kill" : "life",
		           cmd.unitId != 0 ? "unit" : "class", cmd.unitId != 0 ? cmd.unitId : static_cast<uint32_t>(cmd.classId),
		           game != nullptr ? "found" : "NOT found", player != nullptr ? "found" : "NOT found");
		return;
	}
	const int32_t classId = At<int32_t>(monster, UnitClassId);
	const uint32_t id = At<uint32_t>(monster, UnitIdField);
	if (!kill) {
		const int64_t max = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitMaxLife)(monster);
		const auto life = static_cast<int32_t>(std::max<int64_t>(256, max * cmd.percent / 100));
		d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(monster, StatLife, life, 0);
		plog::Info("test aid: life: class %d unit %u set to %d%% (%d of %lld)", classId, id, cmd.percent, life, static_cast<long long>(max));
		return;
	}
	// KillQuietly's death mode, with no UnitNoDrop (so the drop rolls) and the killer the owner (null for wild monsters).
	// A player there crashed the game (a Game* read of 2 in the engine), so pets of players are skipped in Find: no credit to the player
	alignas(8) uint8_t block[0x30] = {};
	d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(SetUnitFlags)(monster, UnitNoDrop, 0);
	d2r::Engine<void (*)(Unit*, int32_t, void*) noexcept>(ModeCmdInit)(monster, ModeDeath, block);
	*reinterpret_cast<void**>(block + 0x10) = d2r::Engine<void* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, monster);
	d2r::Engine<uint64_t (*)(Game*, void*, int32_t) noexcept>(ModeCmdRun)(game, block, 1);
	plog::Info("test aid: kill: class %d unit %u killed, level %d (death path, drops roll)", classId, id,
	           ai_common::LevelOf(monster));
}

}  // namespace

auto QueueTestCommand(const D2RL::PluginContext* context, const pd2::test_cmd::Cmd& cmd, char* why, size_t whySize) noexcept -> bool {
	if (g_threads == nullptr && (context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success ||
	                             !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize))) {
		g_threads = nullptr;
		std::snprintf(why, whySize, "no SDK thread service");
		return false;
	}
	if (g_threads->runOnGameThread(context, Run, reinterpret_cast<void*>(Pack(cmd))) != D2RL::Threads::Result::Success) {
		std::snprintf(why, whySize, "could not queue it on the game thread");
		return false;
	}
	return true;
}
