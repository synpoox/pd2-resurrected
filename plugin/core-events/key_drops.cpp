// PD2's key drop rule (ProjectDiablo.dll 0x103799d0; docs/pd2/uber-bosses-dll.md, section 7). On every
// monster death PD2 rolls odds-only TCs: a TC's Unique x max(Set, 1) is a 1-in-N chance, Unique scaled down by the
// player count, and a hit drops the TC's item by the monster. PD2's corrupted-zone rolls (the Talisman and the
// Worldstone Shard) roll in an active Terror Zone instead (docs/deviations.md).
//
// The trigger is the SDK's monster-death listener, which runs on the server thread after D2R's own drop. It hands over
// ids only, so the game, the monster, the Terror Zone test, the TC rows and the drop are engine calls. No SDK service
// covers them from this thread: ItemService creates items for a player handle, not at a dead monster, and
// DataTableService answers Busy off the captured game thread and has no TC lookup by name.
#include "key_drops.h"

#include "log.h"
#include "death_steps.h"
#include "tc_roll.h"

#include <D2RLPlugin/item.h>
#include <D2RLPlugin/lifecycle_events.h>

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

enum class Where : uint8_t { Undead, Demon, RedPortal, TerrorZone, Any };

struct KeyRoll {
	uint32_t code;      // items.txt code
	Where where;
	const char* tc[3];  // the TC of each difficulty
};

constexpr KeyRoll Rolls[] = {
	{D2RL::Items::MakeItemCode("rtmo"), Where::Undead, {"Rathma Jawbone N", "Rathma Jawbone NM", "Rathma Jawbone H"}},
	{D2RL::Items::MakeItemCode("lucd"), Where::Demon, {"lucdN", "lucdNM", "lucdH"}},
	{D2RL::Items::MakeItemCode("lucb"), Where::RedPortal, {"lucbN", "lucbNM", "lucbH"}},
	{D2RL::Items::MakeItemCode("lucc"), Where::TerrorZone, {"lucc", "lucc", "lucc"}},
	{D2RL::Items::MakeItemCode("wss"), Where::TerrorZone, {"Corrupted Zone", "Corrupted Zone", "Corrupted Zone"}},
	{D2RL::Items::MakeItemCode("imrn"), Where::Any, {"Demonic Cube N", "Demonic Cube NM", "Demonic Cube H"}},
};
constexpr int RollCount = static_cast<int>(sizeof(Rolls) / sizeof(Rolls[0]));

constexpr int32_t FirstRedPortal = 125, LastRedPortal = 127;  // Abaddon, the Pit of Acheron, the Infernal Pit


std::atomic<uint32_t> g_deaths {0};
std::atomic<uint32_t> g_hits[RollCount] {};

auto Applies(Where where, Game* game, const uint8_t* monstats, int32_t level) noexcept -> bool {
	using namespace d2r::rva;
	switch (where) {
	case Where::Undead: return (At<uint32_t>(monstats, MonFlags) & MonUndead) != 0;
	case Where::Demon: return (At<uint32_t>(monstats, MonFlags) & MonDemon) != 0;
	case Where::RedPortal: return level >= FirstRedPortal && level <= LastRedPortal;
	case Where::TerrorZone:
		return level >= 0 && d2r::Engine<bool (*)(Game*, int32_t) noexcept>(InTerrorZone)(game, level);
	case Where::Any: return true;
	}
	return false;
}

void OnMonsterDeath(const D2RL::PluginContext* context, const D2RL::Lifecycle::MonsterDeathEvent* event,
                    void* userData) noexcept {
	using namespace d2r::rva;
	(void)context;
	(void)userData;
	if (event == nullptr || event->monster.type != D2RL::Lifecycle::UnitType::Monster || event->gameId > 0xffff) {
		return;
	}
	const auto id = static_cast<uint16_t>(event->gameId);
	const uint64_t key = d2r::Engine<uint64_t (*)(uint16_t) noexcept>(GameKey)(id);
	Game* game = key != 0 ? d2r::Engine<Game* (*)(uint64_t) noexcept>(GameById)(key) : nullptr;
	if (game == nullptr || At<uint16_t>(game, GameIdField) != id) {
		return;
	}
	Unit* monster = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(FindUnit)(game, 1, event->monster.id);
	const uint8_t dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const uint8_t* monstats =
		monster != nullptr ? d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
								 dataFlag, At<int32_t>(monster, UnitClassId))
						   : nullptr;
	// The listener also sees pets, mercenaries and revives (alignment 2), which drop nothing
	if (monstats == nullptr || d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitAlignment)(monster) == 2) {
		return;
	}
	g_deaths.fetch_add(1, std::memory_order_relaxed);
	UberDeathEarly(game, monster);
	const uint32_t difficulty = event->difficulty < 3 ? event->difficulty : 2;
	const int32_t players = d2r::Engine<int32_t (*)(Game*) noexcept>(PlayerCount)(game);
	auto* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(monster);
	const DeathContext death {game, monster, event, players, seed};
	UberDeathLate(death);
	for (int i = 0; i < RollCount; ++i) {
		const KeyRoll& r = Rolls[i];
		if (!Applies(r.where, game, monstats, event->levelId)) {
			continue;
		}
		const int64_t odds = TcOdds(dataFlag, r.tc[difficulty], players);
		if (odds <= 0 || odds > 0xffffffffLL || d2r::RandomBelow(seed, static_cast<uint32_t>(odds)) != 0) {
			continue;
		}
		const int32_t row = d2r::Engine<int32_t (*)(uint8_t, uint32_t) noexcept>(ItemRowByCode)(dataFlag, r.code);
		if (row < 0) {
			if (plog::Sample(g_tcMissing)) {
				plog::Warn("key drops: no item \"%.4s\" (%u so far)", reinterpret_cast<const char*>(&r.code), g_tcMissing.load());
			}
			continue;
		}
		d2r::Engine<void* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t) noexcept>(DropItemAt)(
			game, monster, row, QualityNormal, 0, 0);
		const uint32_t hits = g_hits[i].fetch_add(1, std::memory_order_relaxed) + 1;
		plog::Info("key drops: %.4s from monster %d in level %d, TC \"%s\" 1 in %lld, %d players (%u so far)",
		           reinterpret_cast<const char*>(&r.code), At<int32_t>(monster, UnitClassId), event->levelId, r.tc[difficulty], odds, players,
		           hits);
	}
}

}  // namespace

auto InstallKeyDrops(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		std::snprintf(report, reportSize, "off: no lifecycle service");
		return 0;
	}
	const D2RL::Lifecycle::MonsterDeathListener listener {
		.structSize = D2RL::Lifecycle::MonsterDeathListenerSize,
		.flags = 0,
		.callback = OnMonsterDeath,
		.userData = nullptr,
	};
	D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
	if (lifecycle->registerMonsterDeathListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
		std::snprintf(report, reportSize, "off: monster-death listener refused");
		return 0;
	}
	std::snprintf(report, reportSize, "%d rolls and the uber boss drops on every monster death", RollCount);
	return 1;
}

auto KeyDropsCounters(char* out, size_t size) noexcept -> void {
	int n = std::snprintf(out, size, "key drops: %u deaths", g_deaths.load());
	for (int i = 0; i < RollCount && n > 0 && static_cast<size_t>(n) < size; ++i) {
		n += std::snprintf(out + n, size - n, ", %.4s %u", reinterpret_cast<const char*>(&Rolls[i].code),
		                   g_hits[i].load());
	}
}
