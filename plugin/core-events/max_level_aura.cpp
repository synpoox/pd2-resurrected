// PD2's max-level aura (#1838): a level 99 player wears state `maxlevelset` (overlay maxlevel_aura). PD2 sets it in the
// level-up function (ProjectDiablo.dll 0x1036f8c0) and re-applies it through the marker function 0x10380a10, which also runs on
// load. Here the state is derived from the level, with no save field: it is put back on the events that can lose it.
//
// The SDK's gameplay events run on the UI thread and carry a player id only; the server unit is reached by hopping to the
// game thread (ThreadService) and walking the game's clients, as test_kill.cpp. No SDK service sets a state on a unit, so
// it is D2R's SetStateBit then SetStateGfxFlag (a bare state, as D2R's 0x56bfe0).
// shortcut: runs on the host's own events only; a remote player's join or respawn waits for the host's next event; move to a
// server-side join/respawn trigger when multiplayer (#153) lands.
#include "max_level_aura.h"

#include "log.h"
#include "test_kill.h"

#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/threads.h>

#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// states.txt row `maxlevelset` (PD2 state 210). The merged table is one for every bank (#1853), so the row is the same in each.
constexpr int32_t MaxLevelState = 256;
constexpr int32_t StatLevel = 12;
constexpr int32_t MaxLevel = 99;
constexpr int PlayerNameOffset = 0;   // the player data starts with the name (D2 PlayerData szName)
constexpr size_t PlayerNameSize = 16;

const D2RL::ThreadService* g_threads = nullptr;

void __cdecl Apply(const D2RL::PluginContext*, void*) noexcept {
	using namespace d2r::rva;
	Game* game = FirstGame();
	if (game == nullptr) {
		return;
	}
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(d2r::DataFlag(game));
	if (tables == nullptr || At<uint64_t>(tables, TablesStateCount) <= MaxLevelState) {
		return;  // not the merged data: the state row is not there
	}
	for (auto* client = At<uint8_t*>(game, GameClients); client != nullptr; client = At<uint8_t*>(client, ClientNext)) {
		Unit* player = At<int32_t>(client, ClientState) == ClientInGame
			? d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(ClientUnit)(client, 0) : nullptr;
		if (player == nullptr || d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(player, StatLevel, 0) < MaxLevel ||
		    d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(player, MaxLevelState) != 0) {
			continue;
		}
		d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateBit)(player, MaxLevelState, 1);
		d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateGfxFlag)(player, MaxLevelState, 1);
		const char* data = reinterpret_cast<const char*>(d2r::Engine<uint8_t* (*)(Unit*) noexcept>(UnitPlayerData)(player));
		char name[PlayerNameSize + 1] = "?";
		for (size_t i = 0; data != nullptr && i < PlayerNameSize; ++i) {
			name[i] = data[PlayerNameOffset + i];
			name[i + 1] = '\0';
			if (name[i] == '\0') {
				break;
			}
		}
		plog::Info("max level: state %d set on %s", MaxLevelState, name);
	}
}

// Level 99 reached, a game joined, a respawn, or a new act or level: the check is cheap and only sets a missing state
void __cdecl OnEvent(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	if (g_threads != nullptr) {
		g_threads->runOnGameThread(context, Apply, nullptr);
	}
}

}  // namespace

auto InstallMaxLevelAura(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize) ||
	    context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize)) {
		g_threads = nullptr;
		std::snprintf(report, reportSize, "off: no lifecycle or thread service");
		return 0;
	}
	using Kind = D2RL::Lifecycle::GameplayEventKind;
	int listening = 0;
	for (const Kind kind : {Kind::PlayerLevelChanged, Kind::LocalPlayerReady, Kind::PlayerResurrected, Kind::ActChanged, Kind::LevelChanged}) {
		const D2RL::Lifecycle::GameplayEventListener listener {
			.structSize = D2RL::Lifecycle::GameplayEventListenerSize, .kind = kind, .callback = OnEvent};
		D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
		listening += lifecycle->registerGameplayEventListener(context, &listener, &handle) == D2RL::Lifecycle::Result::Success ? 1 : 0;
	}
	std::snprintf(report, reportSize, "%d/5 listeners: state %d on a level %d player", listening, MaxLevelState, MaxLevel);
	return listening > 0 ? 1 : 0;
}
