// PD2's corruption in one press (ProjectDiablo.dll 0x366f40; docs/research/ir6-cube-rules.md). A corruption takes two
// rows: a roll row gives the item a hidden `corruptor` value and hands the Worldstone Shard back, then an outcome row
// (op 16 on that value) applies the corruption and takes the shard. PD2 runs the cube again right after a roll row,
// so both fire in one press. D2R runs one row per press; this hook on the executor runs the search and the executor
// once more after a roll row, under PD2's gate (0x364960): the cube holds a Worldstone Shard (`wss ` or `cwss`) and
// none of them is corrupted.
//
// PD2 tags the roll rows with its `ladder` column (2), which the merge cuts. They are the rows whose output carries a
// `corruptnum` mod with a range (1-1000 or 1-3000): exactly PD2's 24 tagged rows, checked against PD2's cubemain.
//
// PD2's Demonic Cube reroll (op 31, 0x3669b0) remakes a unique or set item from its own row and takes one Demonic Cube
// off the stack. The merge gives those rows `usetype,reg`, D2R's own remake of the input's row at its item level, and
// a Demonic Cube stack as output b. The executor takes whole stacks, so this hook sets output b to the stack less one
// (or drops it), and has output a keep the old item's ethereal flag and socket count, as PD2 does.
#include "cube_rules.h"

#include "d2r.h"
#include "log.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t PropCorruptNum = 289;  // merged properties.txt corruptnum (D2R skips the "Expansion" row)
constexpr int32_t StatCorrupted = 413;   // merged itemstatcost corrupted (PD2's 360)
constexpr uint32_t CodeShard = 0x20737377, CodeCubeShard = 0x73737763;  // 'wss ', 'cwss'
constexpr uint32_t CodeDemonicCube = 0x6e726d69;                         // 'imrn'
constexpr int32_t StatQuantity = 70, StatSockets = 194;
constexpr uint8_t OpReroll = 31;
constexpr int32_t QualitySet = 5, QualityUnique = 7;
constexpr int32_t UniqueBits = 0x1000;  // 0x443910 tracks rows up to this one
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
std::atomic<uint32_t> g_rerolls {0};

auto Inventory(Unit* unit) noexcept -> void* {
	return d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(d2r::rva::UnitInventory)(unit, __FILE__, __LINE__);
}

auto Stat(Unit* unit, int32_t stat) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(unit, stat, 0);
}

auto Code(Unit* item) noexcept -> uint32_t {
	return d2r::Engine<uint32_t (*)(Unit*) noexcept>(d2r::rva::ItemCode)(item);
}

// Calls `visit(item)` for each item in the player's cube until it returns false.
template <class Visit>
auto ForCubeItems(Unit* player, Visit visit) noexcept -> void {
	using namespace d2r::rva;
	void* inventory = Inventory(player);
	if (inventory == nullptr) {
		return;
	}
	const auto next = d2r::Engine<Unit* (*)(Unit*) noexcept>(InventoryNextItem);
	for (Unit* item = d2r::Engine<Unit* (*)(void*) noexcept>(InventoryFirstItem)(inventory); item != nullptr;
	     item = next(item)) {
		if (d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemPage)(item) == CubePageCube && !visit(item)) {
			return;
		}
	}
}

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
	bool shard = false, corrupted = false;
	ForCubeItems(player, [&](Unit* item) noexcept {
		const uint32_t code = Code(item);
		if (code == CodeShard || code == CodeCubeShard) {
			shard = true;
			corrupted = Stat(item, StatCorrupted) != 0;
		}
		return !corrupted;
	});
	return shard && !corrupted;
}

auto Quality(Unit* item) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::ItemQuality)(item);
}

auto Row(Unit* item) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::ItemRow)(item);
}

// Op 31: PD2's 0x3669b0 on D2R's `usetype,reg` row (top of file).
// shortcut: patches the shared cubemain record for the one call, as the cube runs on the game thread; one game per
// process (single player). Upgrade if games ever run in parallel.
auto Reroll(Game* game, Unit* player, const uint8_t* record, MatchOut* out) noexcept -> uint64_t {
	using namespace d2r::rva;
	Unit* target = nullptr;
	int32_t cubes = 0;
	ForCubeItems(player, [&](Unit* item) noexcept {
		if (Code(item) == CodeDemonicCube) {
			cubes = Stat(item, StatQuantity);
		} else if (const int32_t q = Quality(item); q == QualitySet || q == QualityUnique) {
			target = item;
		}
		return true;
	});
	auto* row = const_cast<uint8_t*>(record);
	uint8_t* a = row + CubeOutput;
	uint8_t* b = a + CubeOutputSize;
	const int qty = CubeOutQty - CubeOutput, flags = CubeOutFlags - CubeOutput;
	if (target == nullptr || cubes < 1 || b[qty] != 1) {
		plog::Info("reroll: row not as merged (item %d, demonic cubes %d, output b qty %u), D2R runs it as is",
		           target != nullptr, cubes, b[qty]);
		return g_executorOriginal(game, player, record, out);
	}
	// PD2 moves socketed items to the new item; D2R's executor would delete them with the old one
	if (void* filled = Inventory(target);
	    filled != nullptr && d2r::Engine<Unit* (*)(void*) noexcept>(InventoryFirstItem)(filled) != nullptr) {
		plog::Info("reroll: refused, the item has socketed items (empty its sockets first)");
		return 0;
	}
	const int32_t quality = Quality(target), unique = Row(target), sockets = Stat(target, StatSockets);
	// `reg` makes the row only on its own base; an upgraded item would come back as another unique or set item
	const uint8_t* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(d2r::DataFlag(game));
	const bool isSet = quality == QualitySet;
	const uint8_t* records = At<const uint8_t*>(tables, isSet ? SetRecords : UniqueRecords);
	const uint64_t count = At<uint64_t>(tables, (isSet ? SetRecords : UniqueRecords) + 8);
	if (unique < 0 || static_cast<uint64_t>(unique) >= count ||
	    At<uint32_t>(records + unique * (isSet ? SetRecordSize : UniqueRecordSize), RecordBaseCode) != Code(target)) {
		plog::Info("reroll: refused, quality %d row %d is not on its own base (upgraded?)", quality, unique);
		return 0;
	}
	const bool ethereal = d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemIsEthereal)(target) != 0;
	// 0x443910 will not make a unique row this game already made. PD2's 0x103841d0 clears the row's bit for the remake
	// and puts back what it was, so a reroll neither trips nor changes the one-per-game rule.
	uint32_t* madeBits = nullptr;
	uint32_t bit = 0, wasMade = 0;
	if (quality == QualityUnique && unique >= 0 && unique <= UniqueBits) {
		madeBits = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(game) + GameUniquesMade) + (unique >> 5);
		bit = 1u << (unique & 31);
		wasMade = *madeBits & bit;
		*madeBits &= ~bit;
	}
	uint8_t savedA[CubeOutputSize], savedB[CubeOutputSize];
	std::memcpy(savedA, a, sizeof(savedA));
	std::memcpy(savedB, b, sizeof(savedB));
	if (ethereal) {
		*reinterpret_cast<uint16_t*>(a + flags) |= CubeOutEthereal;
	}
	if (sockets > 0) {
		*reinterpret_cast<uint16_t*>(a + flags) |= CubeOutSocket;
		a[qty] = static_cast<uint8_t>(sockets);
	}
	if (cubes > 1) {
		b[qty] = static_cast<uint8_t>(cubes - 1);
	} else {
		std::memcpy(b, b + CubeOutputSize, CubeOutputSize);  // output c: empty on these rows
	}
	const uint64_t made = g_executorOriginal(game, player, record, out);
	std::memcpy(a, savedA, sizeof(savedA));
	std::memcpy(b, savedB, sizeof(savedB));
	if (madeBits != nullptr) {
		*madeBits = (*madeBits & ~bit) | wasMade;
	}
	bool remade = false;
	ForCubeItems(player, [&](Unit* item) noexcept {
		remade = Quality(item) == quality && Row(item) == unique;
		return !remade;
	});
	plog::Info("reroll: quality %d row %d ethereal %d sockets %d, demonic cubes %d -> %d, remade %d (%u so far)",
	           quality, unique, ethereal, sockets, cubes, cubes - 1, remade, ++g_rerolls);
	return made;
}

auto CubeExecute(Game* game, Unit* player, const uint8_t* record, MatchOut* out) noexcept -> uint64_t {
	if (game != nullptr && player != nullptr && record != nullptr && record[d2r::rva::CubeOp] == OpReroll) {
		return Reroll(game, player, record, out);
	}
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
	std::snprintf(report, reportSize, "corruption in one press, demonic cube reroll%s", hook ? "" : " FAILED (bytes differ)");
	return hook ? 1 : 0;
}

auto CubeRuleCount() noexcept -> int {
	return 1;
}

auto CubeRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "corruption roll rows %u, one-press outcomes %u, rerolls %u", g_rollRows.load(),
	              g_onePress.load(), g_rerolls.load());
}
