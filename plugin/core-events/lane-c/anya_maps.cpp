// Anya's Hell reward (#1323; PD2: ProjectDiablo.dll 0x10372050, docs/research/maps/anya-maps.md). D2R queues Anya's class
// item as a deferred event; its execute function (0x546cc0 in build 93847) makes the item. PD2 also gives, in Hell only,
// 3 random T1 maps (item level 99, normal) in the player's inventory. This runs the original first, then does that.
//
// No SDK service gives an item to a player at a quest step with PD2's drop-to-ground fallback, so the quest item
// function is called as the engine does. All three engine addresses are found by signature at load, never by RVA.
#include "anya_maps.h"

#include "log.h"
#include "pd2_engine.h"
#include "sig_hook.h"

#include <D2RLPlugin/data_tables.h>

#include <atomic>
#include <cstdio>
#include <random>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// The deferred event's closure: +8 player guid, +0x10 game (D2R's A5Q3AnyaGiftExecute)
constexpr int ClosureGuid = 0x08;
constexpr int ClosureGame = 0x10;
constexpr int MapCount = 3;
constexpr int MapItemLevel = 99;
constexpr int QualityNormal = 2;
constexpr int HellDifficulty = 2;
constexpr int MaxCandidates = 64;

constexpr char GrantPattern[] = "48 89 5c 24 08 48 89 54 24 10 57 48 83 ec 30 44 8b 41 08 48 8b d9 48 8b 49 10 33 d2";
constexpr char QuestItemPattern[] =
    "48 89 6c 24 18 48 89 74 24 20 57 48 81 ec a0 00 00 00 41 8b c0 48 8b f2 48 8b e9 4c 8d 44 24 60 0f b6 89 06 01 00 00";
constexpr uint8_t GrantEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x54, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x30};

using GrantFn = uint64_t (*)(void* closure, void* arg) noexcept;
using FindUnitFn = Unit* (*)(Game* game, int32_t type, uint32_t id) noexcept;
using QuestItemFn = Unit* (*)(Game* game, Unit* player, uint32_t code, int32_t ilvl, int32_t quality, int32_t drop) noexcept;
using ItemRecordFn = const uint8_t* (*)(uint32_t dataFlag, int32_t classId) noexcept;

const D2RL::PluginContext* g_context = nullptr;
GrantFn g_original = nullptr;
FindUnitFn g_findUnit = nullptr;
QuestItemFn g_questItem = nullptr;
std::atomic<uint32_t> g_grants {0};
std::atomic<uint32_t> g_maps {0};

// The codes of every misc row whose type is t1m, read from the loaded items table (never a fixed list)
auto T1MapCodes(Game* game, uint32_t* codes) noexcept -> int {
	const D2RL::DataTableService* tables = nullptr;
	if (g_context->QueryService(&tables) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasDataTableServiceField(tables, D2RL::DataTableServiceRequiredSize)) {
		return 0;
	}
	const auto bank = static_cast<D2RL::DataTables::Bank>(d2r::DataFlag(game));
	// shortcut: the itemtypes code is tried zero-padded then space-padded; upgrade if the table pads differently
	D2RL::DataTables::RowView type {.structSize = D2RL::DataTables::RowViewSize};
	if (tables->findRowByCode(g_context, bank, D2RL::DataTables::TableId::ItemTypes, D2RL::DataTables::MakeFourCC('t', '1', 'm'),
	                          &type) != D2RL::DataTables::Result::Success &&
	    tables->findRowByCode(g_context, bank, D2RL::DataTables::TableId::ItemTypes,
	                          D2RL::DataTables::MakeFourCC('t', '1', 'm', ' '), &type) != D2RL::DataTables::Result::Success) {
		return 0;
	}
	D2RL::DataTables::TableView items {.structSize = D2RL::DataTables::TableViewSize};
	if (tables->getTable(g_context, bank, D2RL::DataTables::TableId::Items, &items) != D2RL::DataTables::Result::Success) {
		return 0;
	}
	const auto itemRecord = d2r::Engine<ItemRecordFn>(d2r::sig::ItemRecord);
	int count = 0;
	for (uint32_t i = 0; i < items.rowCount && count < MaxCandidates; ++i) {
		const uint8_t* record = itemRecord(d2r::DataFlag(game), static_cast<int32_t>(i));
		if (record != nullptr && At<int16_t>(record, d2r::rva::ItType) == static_cast<int16_t>(type.rowIndex)) {
			codes[count++] = At<uint32_t>(record, d2r::rva::ItCode);
		}
	}
	return count;
}

auto AnyaGift(void* closure, void* arg) noexcept -> uint64_t {
	const uint64_t result = g_original(closure, arg);
	auto* game = closure != nullptr ? At<Game*>(closure, ClosureGame) : nullptr;
	if (game == nullptr || At<uint8_t>(game, d2r::rva::GameDifficulty) != HellDifficulty) {
		return result;
	}
	Unit* player = g_findUnit(game, 0, At<uint32_t>(closure, ClosureGuid));
	uint32_t codes[MaxCandidates] = {};
	const int n = player != nullptr ? T1MapCodes(game, codes) : 0;
	if (n == 0) {
		plog::Warn("anya maps: %s", player == nullptr ? "player gone, no maps" : "no t1m rows, no maps");
		return result;
	}
	static thread_local std::mt19937 rng {std::random_device {}()};
	char given[MapCount * 5] = "";
	for (int i = 0; i < MapCount; ++i) {
		const uint32_t code = codes[std::uniform_int_distribution<int>(0, n - 1)(rng)];
		g_questItem(game, player, code, MapItemLevel, QualityNormal, 1);
		std::snprintf(given + std::char_traits<char>::length(given), sizeof(given) - std::char_traits<char>::length(given),
		              "%s%.3s", i != 0 ? "," : "", reinterpret_cast<const char*>(&code));
	}
	g_grants.fetch_add(1, std::memory_order_relaxed);
	g_maps.fetch_add(MapCount, std::memory_order_relaxed);
	plog::Info("anya maps: gave %d T1 maps (%s)", MapCount, given);
	return result;
}

}  // namespace

auto InstallAnyaMaps(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	g_context = context;
	uint64_t questItem = 0;
	const bool found = d2r::sig::FindUnit.state == sigscan::State::Ok && d2r::sig::ItemRecord.state == sigscan::State::Ok &&
	                   sigscan::ScanUnique(context->exeBase, QuestItemPattern, 0, &questItem) == 1;
	bool on = false;
	if (found) {
		g_findUnit = d2r::Engine<FindUnitFn>(d2r::sig::FindUnit);
		g_questItem = reinterpret_cast<QuestItemFn>(context->exeBase + questItem);
		on = sigscan::HookBySignature(context, "AnyaGiftExecute", GrantPattern, 0, GrantEntry, sizeof(GrantEntry), AnyaGift,
		                              &g_original);
	} else {
		plog::Warn("anya maps: signature not found (FindUnit or QuestCreateItem), off");
	}
	std::snprintf(report, reportSize, "anya gift hook%s", on ? "" : " FAILED (signature)");
	return on ? 1 : 0;
}

auto AnyaMapsCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "anya maps: %u grants, %u maps", g_grants.load(), g_maps.load());
}
