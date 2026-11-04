// PD2's cube op 30 (ProjectDiablo.dll 0x365fe0 and 0x366f40; docs/research/pd2-map-system-dll.md, section 3): when
// an op-30 row's inputs all match the cube's contents, the transmute stops and nothing is made. The row shadows the
// rows after it: the map recipes use it to refuse normal maps (heroic, corrupt, fortify, Catalyst, ears) and unique
// maps (ears), and to stop the generic gem and skull rerolls from duplicating stacked maps.
//
// PD2 checks op-30 rows before its other filters, so difficulty, class, the input count and the stackable check do not
// apply to them; the blocks with a qty input (3 perfect gems, 6 perfect skulls) depend on that, as their numinputs is 2.
// D2R's search applies those filters to every row and has no op 30 (unknown ops pass), so this hook runs D2R's
// search, then walks the op-30 rows up to the row it found (or all of them) through D2R's own input matcher. The first
// op-30 row whose inputs all match turns the result into "no recipe".
#include "cube_ops.h"

#include "d2r.h"
#include "log.h"

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr uint8_t OpBlock = 30;
constexpr int Inputs = 7;

struct SearchResult {
	int32_t row;
	uint8_t found;
};

// The search's out: one {item, int32, int32} entry per output, filled by the input matcher
struct MatchOut {
	uint64_t words[6];
};

using SearchFn = void (*)(SearchResult* result, Game* game, Unit* player, MatchOut* out) noexcept;
using MatchFn = uint64_t (*)(Game* game, void* inventory, const uint8_t* row, int32_t input, MatchOut* out,
                             int32_t* used) noexcept;

SearchFn g_searchOriginal = nullptr;
std::atomic<uint32_t> g_searches {0};
std::atomic<uint32_t> g_blocks {0};

auto InputsMatch(Game* game, void* inventory, const uint8_t* row) noexcept -> bool {
	// Own buffers, as the search zeroes its own per row: the row the search found keeps its out untouched
	MatchOut out {};
	int32_t used[d2r::rva::CubeMatchUsedSlots] = {};
	const auto match = d2r::Engine<MatchFn>(d2r::rva::CubeInputMatch);
	for (int input = 0; input < Inputs; ++input) {
		if (match(game, inventory, row, input, &out, used) == 0) {
			return false;
		}
	}
	return true;
}

// The first enabled op-30 row at or before `last` whose inputs all match, or -1.
// The op-30 rows are version 2 with no ladder season, so D2R's version and ladder filters would pass them too.
auto BlockingRow(Game* game, Unit* player, int32_t last) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	void* inventory = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(UnitInventory)(
		player, __FILE__, __LINE__);
	if (inventory == nullptr) {
		return -1;
	}
	const auto record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(CubeRecipe);
	for (int32_t i = 0; i <= last; ++i) {
		const uint8_t* row = record(dataFlag, i);
		if (row == nullptr) {
			break;
		}
		if (At<uint8_t>(row, CubeOp) == OpBlock && At<uint8_t>(row, CubeEnabled) != 0 &&
		    InputsMatch(game, inventory, row)) {
			return i;
		}
	}
	return -1;
}

auto RecipeSearch(SearchResult* result, Game* game, Unit* player, MatchOut* out) noexcept -> void {
	g_searchOriginal(result, game, player, out);
	if (result == nullptr || game == nullptr || player == nullptr) {
		return;
	}
	++g_searches;
	const int32_t count = d2r::Engine<int32_t (*)(uint32_t) noexcept>(d2r::rva::CubeRecipeCount)(d2r::DataFlag(game));
	const int32_t last = result->found != 0 ? result->row : count - 1;
	const int32_t block = BlockingRow(game, player, last);
	if (block < 0) {
		return;
	}
	plog::Info("op 30: cubemain row %d blocks the transmute (D2R found %s %d), %u so far", block,
	           result->found != 0 ? "row" : "no row", result->found != 0 ? result->row : -1, ++g_blocks);
	result->found = 0;
}

}  // namespace

auto InstallCubeOps(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	const bool hook = context->InstallInlineHook(CubeRecipeSearch, CubeRecipeSearchEntry, sizeof(CubeRecipeSearchEntry),
	                                             RecipeSearch, &g_searchOriginal);
	std::snprintf(report, reportSize, "op 30 block%s", hook ? "" : " FAILED (bytes differ)");
	return hook ? 1 : 0;
}

auto CubeOpCount() noexcept -> int {
	return 1;
}

auto CubeOpCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "cube searches %u, op 30 blocks %u", g_searches.load(), g_blocks.load());
}
