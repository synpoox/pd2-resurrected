// PD2's corruption in one press (ProjectDiablo.dll 0x366f40; docs/research/ir6-cube-rules.md). A corruption takes two
// rows: a roll row gives the item a hidden `corruptor` value and hands the Worldstone Shard back, then an outcome row
// (op 16 on that value) applies the corruption and takes the shard. PD2 runs the cube again right after a roll row,
// so both fire in one press. D2R runs one row per press; this hook on the executor runs the search and the executor
// once more after a roll row, under PD2's gate (0x364960): the cube holds a Worldstone Shard (`wss ` or `cwss`) and
// none of them is corrupted.
//
// PD2 tags the roll rows with its `ladder` column (2), which the merge cuts. They are the rows whose output carries a
// `corruptnum` mod with a range (1-1000 or 1-3000): exactly PD2's 24 tagged rows, checked against PD2's cubemain.
#include "cube_rules.h"

#include "d2r.h"
#include "log.h"

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t PropCorruptNum = 289;  // merged properties.txt corruptnum (D2R skips the "Expansion" row)
constexpr int32_t StatCorrupted = 413;   // merged itemstatcost corrupted (PD2's 360)
constexpr uint32_t CodeShard = 0x20737377, CodeCubeShard = 0x73737763;  // 'wss ', 'cwss'
constexpr int OutputMods = 5;

struct SearchResult {
	int32_t row;
	uint8_t found;
};

// The search's out: one {item, int32, int32} entry per output, the executor's last argument
struct MatchOut {
	uint64_t words[6];
};

using ExecutorFn = uint64_t (*)(Game* game, Unit* player, const uint8_t* record, MatchOut* out) noexcept;
using SearchFn = void (*)(SearchResult* result, Game* game, Unit* player, MatchOut* out) noexcept;

ExecutorFn g_executorOriginal = nullptr;
std::atomic<uint32_t> g_rollRows {0};
std::atomic<uint32_t> g_onePress {0};

auto IsRollRow(const uint8_t* record) noexcept -> bool {
	using namespace d2r::rva;
	for (int m = 0; m < OutputMods; ++m) {
		const uint8_t* mod = record + CubeOutMods + m * CubeModSize;
		const int32_t prop = At<int32_t>(mod, 0);
		if (prop == -1) {
			return false;
		}
		if (prop == PropCorruptNum && At<int16_t>(mod, 6) != At<int16_t>(mod, 8)) {
			return true;
		}
	}
	return false;
}

// PD2's 0x364960: a Worldstone Shard is in the cube and none in it is corrupted.
auto ShardReady(Unit* player) noexcept -> bool {
	using namespace d2r::rva;
	void* inventory = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(UnitInventory)(
		player, __FILE__, __LINE__);
	if (inventory == nullptr) {
		return false;
	}
	const auto next = d2r::Engine<Unit* (*)(Unit*) noexcept>(InventoryNextItem);
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	bool shard = false;
	for (Unit* item = d2r::Engine<Unit* (*)(void*) noexcept>(InventoryFirstItem)(inventory); item != nullptr;
	     item = next(item)) {
		if (d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemPage)(item) != CubePageCube) {
			continue;
		}
		const uint32_t code = d2r::Engine<uint32_t (*)(Unit*) noexcept>(ItemCode)(item);
		if (code != CodeShard && code != CodeCubeShard) {
			continue;
		}
		if (stat(item, StatCorrupted, 0) != 0) {
			return false;
		}
		shard = true;
	}
	return shard;
}

auto CubeExecute(Game* game, Unit* player, const uint8_t* record, MatchOut* out) noexcept -> uint64_t {
	const uint64_t made = g_executorOriginal(game, player, record, out);
	if (game == nullptr || player == nullptr || record == nullptr || !IsRollRow(record)) {
		return made;
	}
	++g_rollRows;
	if (!ShardReady(player)) {
		plog::Info("corruption: roll row ran, no clean shard in the cube, no second row");
		return made;
	}
	// Through core-maps' hook, so op 30 blocks still apply to the second search
	SearchResult result {};
	MatchOut again {};
	d2r::Engine<SearchFn>(d2r::rva::CubeRecipeSearch)(&result, game, player, &again);
	if (result.found == 0) {
		plog::Info("corruption: roll row ran, no outcome row matches");
		return made;
	}
	const uint8_t* outcome =
		d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::rva::CubeRecipe)(d2r::DataFlag(game), result.row);
	if (outcome == nullptr) {
		return made;
	}
	plog::Info("corruption: one press runs outcome row %d, %u so far", result.row, ++g_onePress);
	g_executorOriginal(game, player, outcome, &again);
	return made;
}

}  // namespace

auto InstallCubeRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	const bool hook = context->InstallInlineHook(CubeExecutor, CubeExecutorEntry, sizeof(CubeExecutorEntry),
	                                             CubeExecute, &g_executorOriginal);
	std::snprintf(report, reportSize, "corruption in one press%s", hook ? "" : " FAILED (bytes differ)");
	return hook ? 1 : 0;
}

auto CubeRuleCount() noexcept -> int {
	return 1;
}

auto CubeRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "corruption roll rows %u, one-press outcomes %u", g_rollRows.load(), g_onePress.load());
}
