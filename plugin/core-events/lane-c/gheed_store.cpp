// The map event's Gheed (#1842; PD2 ProjectDiablo.dll, PD2.gpr addresses; docs/features/map-system.md).
//
// Stock: PD2 0x103846d0 replaces 1.13c's gamble fill (D2Game +0xbe2a0) for NPC 147 in a map level whose event is Gheed.
// Once per game (game +0x2628, the event's state, set to 2) it makes a store of its own (act index -1, so not the act's
// store) holding: 1 in 200 Kadala's Heirloom (0x10388cd0, not ported: docs/deviations.md), then up to 5 random unique
// items (below), item level 99, unidentified, and none of the plain stock. Here the stock goes in his trade store: D2R's
// trade fill (0x53c9f0 in build 93847) is wrapped, and for the map's Gheed its plain stock is not made; each unique is
// made through D2R's own store item maker (0x540ea0: makes, checks and places an item in the NPC's store), whose item
// creation (0x43d530) is wrapped so that, during this call only, the drop request also names the unique row.
//
// Standing still: PD2's Npc think (0x10357340, monai row 32) idles Gheed for 25 frames (0x103a1910) in a map level; D2R's
// Npc think lets him walk. His think is registered on row 32 (ai_registry), every other NPC keeps D2R's.
//
// No PluginSDK service fills an NPC's store (createItem only reaches the player's containers) or runs a monster's AI,
// so these are native hooks; every address is found by signature at load, and nothing changes when one is not found.
#include "gheed_store.h"

#include "ai_registry.h"
#include "log.h"
#include "map_events.h"
#include "pd2_engine.h"
#include "sig_hook.h"

#include <D2RLPlugin/data_tables.h>
#include <D2RLPlugin/mutations.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <intrin.h>
#include <iterator>

#include <windows.h>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t GheedClass = 147;
constexpr int32_t GheedEvent = 18;   // map_events.cpp Gheed (PD2 game +0x1df4 == 0x12)
constexpr int32_t StockBuilt = 2;    // PD2 sets the event's state (game +0x2628) to 2 once the stock is built
constexpr int32_t NpcAiRow = 32;     // monai Npc
constexpr int32_t IdleFrames = 25;   // PD2 0x103a1910(game, unit, 0x19)
constexpr int32_t ItemLevel = 99;
constexpr int32_t QualityUnique = 7;
constexpr int32_t MaxUniques = 5;    // PD2 stops once 5 are made
constexpr int32_t MakeTries = 20;    // PD2's 0x14 item attempts
constexpr int32_t PickTries = 10;    // rows drawn for one pick
constexpr uint32_t ItemFlagIdentified = 0x10;

// D2R's compiled uniqueitems record (build 93847: the loader's column table at 0x31f1d0)
constexpr int UqCode = 0x28, UqFlags = 0x2c, UqRarity = 0x34, UqLvl = 0x38, UqLvlReq = 0x3a;
constexpr uint32_t UqSpawnable = 1u << 0, UqDisabled = 1u << 2;
constexpr uint32_t JewelCode = 0x2077656a;  // 'jew '

// PD2's row test (0x103846d0), one draw: `percent` is the rand() % 100 PD2 draws for a row with lvl req 60+. The 1.13c
// record's flags word (+0x2c) must be non-zero; its enabled column became D2R's spawnable (disabled clear).
struct UniqueRow {
	uint32_t code, flags;
	uint16_t rarity, lvl, lvlReq;
};
constexpr auto HighLevelPasses(uint16_t lvlReq, int32_t percent) noexcept -> bool {
	return lvlReq < 60 || lvlReq - 49 <= percent;
}
constexpr auto Sellable(const UniqueRow& r) noexcept -> bool {
	return r.code != JewelCode && (r.flags & (UqSpawnable | UqDisabled)) == UqSpawnable && r.rarity != 0 && r.lvl != 0 &&
	       r.lvl < 100;
}
static_assert(HighLevelPasses(59, 0) && !HighLevelPasses(60, 10) && HighLevelPasses(60, 11) && !HighLevelPasses(99, 49) &&
              HighLevelPasses(99, 50));
static_assert(Sellable({0x206e6972, 1, 1, 50, 40}) && !Sellable({JewelCode, 1, 1, 50, 40}) &&
              !Sellable({0x206e6972, 5, 1, 50, 40}) && !Sellable({0x206e6972, 0, 1, 50, 40}) &&
              !Sellable({0x206e6972, 1, 0, 50, 40}) && !Sellable({0x206e6972, 1, 1, 100, 40}) &&
              !Sellable({0x206e6972, 1, 1, 0, 40}));

// D2R's trade fill (game, player, npc): runs on an NPC's first trade (and on a restock)
constexpr char FillPattern[] = "48 89 5c 24 10 4c 89 44 24 18 48 89 4c 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8d 6c 24 f0 48";
constexpr uint8_t FillEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x4c, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x4c, 0x24, 0x08};
// D2R's store item maker (npc, item code, game, send, quality, item level, player level) -> the item placed in the
// store, or null; a player level above 25 may swap the base for its exceptional or elite, so 0 is passed
constexpr char MakerPattern[] = "48 8b c4 44 89 48 20 53 55 56 57 41 54 41 55 41";
// its item creation (unit, items row, game, mode, quality, f6, f7, item level, a9, a10, a11) -> item: fills a drop
// request and calls ItemSpawn, then marks the item identified
constexpr char CreatePattern[] = "40 55 53 56 57 48 8d 6c 24 f8 48 81 ec 08 01 00 00 48";
constexpr uint8_t CreateEntry[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xf8, 0x48, 0x81, 0xec, 0x08, 0x01, 0x00, 0x00};
// The calls to D2R's item price thunk (0x36f0b0, a jmp to 0x36f0c0, which core-rules hooks for its repair cap) that
// price a store buy: the server's buy handler 0x53d850 (0x53d9c9, 0x53df48) and the client's store UI 0x10cad0 (0x10cd5f,
// 0x10cde4, 0x10ce69) and 0x10d160 (0x10d523, 0x10d5e7, 0x10d691). Each pattern has one match; the e8 is at +6.
constexpr const char* PriceCalls[] = {
	"8b d7 89 5c 24 20 e8 ?? ?? ?? ?? 8b 8d d8 00 00 00",
	"40 48 8b 4c 24 58 e8 ?? ?? ?? ?? 48 8b 4c 24 78 45",
	"48 8b d5 48 8b c8 e8 ?? ?? ?? ?? 41 89 04 24 b8 02",
	"48 8b d5 48 8b c8 e8 ?? ?? ?? ?? 41 89 04 24 b8 03",
	"48 8b d5 48 8b c8 e8 ?? ?? ?? ?? 41 89 04 24 b8 01",
	"48 8b d6 48 8b c8 e8 ?? ?? ?? ?? 48 8b ce 89 05 a7",
	"48 8b d6 48 8b c8 e8 ?? ?? ?? ?? 48 8b ce 89 05 e3",
	"48 8b d6 48 8b c8 e8 ?? ?? ?? ?? 89 05 3c b1 93 02",
};
constexpr int PriceCallAt = 6;
// D2R's Npc think, monai row 32's slot in the AI table
constexpr char NpcThinkPattern[] = "48 89 5c 24 10 48 89 74 24 18 57 48 83 ec 40 49 8b f0";
// Calls read from those bodies (checked bytes around each call): the fill's prologue gets the NPC's store record
// (0x502b70: game, npc, out) and stamps its +0x38 with the time (0x120d720), which D2R's restock check (0x503290) reads;
// the maker frees an item whose base is not the one asked (0x43ec10: game, item)
struct CallSite {
	int offset;  // of the e8
	uint8_t before[6], after[6];
};
constexpr CallSite FillRecord {0x43, {0x49, 0x8b, 0xd6, 0x49, 0x8b, 0xcc}, {0x48, 0x8b, 0xf8, 0x48, 0x89, 0x44}};
constexpr CallSite FillNow {0x50, {0xf8, 0x48, 0x89, 0x44, 0x24, 0x58}, {0x33, 0xf6, 0x48, 0x89, 0x47, 0x38}};
constexpr CallSite MakerFree {0x2f4, {0x48, 0x8b, 0xd7, 0x49, 0x8b, 0xcf}, {0x41, 0xff, 0xc6, 0x33, 0xc0, 0x41}};
constexpr int StoreStamp = 0x38;

// The drop request 0x43d530 fills for ItemSpawn (d2r::rva Drop* offsets: +0x00 unit, +0x18 item level, +0xc0 flags)
constexpr int ReqGame = 0x10, ReqItemLevel = 0x18, ReqRow = 0x20, ReqMode = 0x24, ReqOne = 0x38, ReqGameWord = 0x3a,
              ReqQuality = 0x40, ReqUnique = 0x50, ReqA10 = 0x58, ReqA11 = 0x5c, ReqFlags = 0xc0, ReqSize = 0xc8;
constexpr int GameWord = 0x128;  // the game's u16 0x43d530 copies into the request

using FillFn = void (*)(Game* game, Unit* player, Unit* npc) noexcept;
using MakerFn = Unit* (*)(Unit* npc, uint32_t code, Game* game, int32_t send, int32_t quality, int32_t ilvl, int32_t plvl) noexcept;
using RecordFn = uint8_t* (*)(Game* game, Unit* npc, uint64_t* out) noexcept;
using NowFn = uint64_t (*)() noexcept;
using FreeFn = void (*)(Game* game, Unit* item) noexcept;
using PriceFn = uint64_t (*)(Unit* player, Unit* item, int32_t difficulty, void* table, int32_t npc, int32_t mode) noexcept;
using CreateFn = Unit* (*)(Unit* unit, int32_t row, Game* game, int32_t mode, int32_t quality, int32_t f6, int32_t f7, int32_t ilvl,
                           int32_t a9, int32_t a10, int32_t a11) noexcept;

const D2RL::PluginContext* g_context = nullptr;
FillFn g_fill = nullptr;
MakerFn g_maker = nullptr;
CreateFn g_create = nullptr;
RecordFn g_record = nullptr;
NowFn g_now = nullptr;
FreeFn g_free = nullptr;
PriceFn g_price = nullptr;
thread_local int32_t g_forceUnique = -1;  // the unique row 0x43d530 makes, only during MakeUnique
std::atomic<uint32_t> g_stocks {0}, g_uniques {0}, g_idles {0};

// PD2 rolls with the CRT's rand() (map_events.cpp seeds each thread the same way)
auto Rand() noexcept -> int32_t {
	static thread_local const bool seeded = (std::srand(static_cast<unsigned>(__rdtsc())), true);
	(void)seeded;
	return std::rand();
}

auto LevelOf(Unit* unit) noexcept -> int32_t {
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::UnitRoom)(unit);
	return room != nullptr ? d2r::Engine<int32_t (*)(void*) noexcept>(d2r::sig::RoomLevelId)(room) : -1;
}

// PD2's test: NPC 147 in a map level (and, for the store, the game's event is Gheed)
auto IsMapGheed(Unit* npc) noexcept -> bool {
	return npc != nullptr && At<int32_t>(npc, d2r::rva::UnitClassId) == GheedClass && IsMapLevel(LevelOf(npc));
}

auto CreateItem(Unit* unit, int32_t row, Game* game, int32_t mode, int32_t quality, int32_t f6, int32_t f7, int32_t ilvl,
                int32_t a9, int32_t a10, int32_t a11) noexcept -> Unit* {
	if (g_forceUnique < 0) {
		return g_create(unit, row, game, mode, quality, f6, f7, ilvl, a9, a10, a11);
	}
	// The maker frees an item whose base is not the one asked and, after its quality-2 retry, gives up: an item that is
	// not the forced unique is handed back on another base, so nothing but a unique reaches his store
	const auto reject = [&]() noexcept { return g_create(unit, row == 0 ? 1 : 0, game, mode, 2, f6, f7, ilvl, a9, a10, a11); };
	if (quality != QualityUnique) {
		return reject();
	}
	// 0x43d530's own request, with the unique row (+1) the TC drop 0x444680 also sets
	alignas(8) uint8_t req[ReqSize] {};
	*reinterpret_cast<Unit**>(req) = unit;
	*reinterpret_cast<Game**>(req + ReqGame) = game;
	*reinterpret_cast<int32_t*>(req + ReqItemLevel) = ilvl > 0 ? ilvl : 1;
	*reinterpret_cast<int32_t*>(req + ReqRow) = row;
	*reinterpret_cast<int32_t*>(req + ReqMode) = mode;
	*reinterpret_cast<uint16_t*>(req + ReqOne) = 1;
	*reinterpret_cast<uint16_t*>(req + ReqGameWord) = At<uint16_t>(game, GameWord);
	*reinterpret_cast<int32_t*>(req + ReqQuality) = quality;
	*reinterpret_cast<int32_t*>(req + ReqUnique) = g_forceUnique + 1;
	*reinterpret_cast<int32_t*>(req + ReqA10) = a10;
	*reinterpret_cast<int32_t*>(req + ReqA11) = a11;
	*reinterpret_cast<uint64_t*>(req + ReqFlags) = (f6 != 0 ? 8u : 0u) | (f7 != 0 ? 2u : 0u);
	Unit* item = d2r::Engine<Unit* (*)(Game*, void*, int32_t) noexcept>(d2r::sig::ItemSpawn)(game, req, a9);
	if (item == nullptr || d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::sig::ItemQuality)(item) != QualityUnique) {
		if (item != nullptr) {
			g_free(game, item);
		}
		return reject();
	}
	d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(d2r::sig::SetItemFlag)(item, ItemFlagIdentified, 1);
	return item;
}

// The unit ids of the store items made this way, for the buy price below. The id, not the unit: the client's copy of
// an item is another unit with the same id.
// shortcut: ids of the last 16 items in this process, so only a local game's client (single player, or the host) sees
// the same price; a remote client prices them as a plain item and its buy is refused. Upgrade with a client-side table
// fed by the server if remote play needs it.
std::atomic<uint32_t> g_stock[16] {};
std::atomic<uint32_t> g_stockNext {0};
constexpr int UnitGuid = 0x08;  // the unit's id (Unit +0x08)

constexpr int ItemDataFlags = 0x18;  // u32 in the item's UnitData (SetItemFlag's word)

// one of his stock: still unidentified and an id made here (only uniques are). Not ItemQuality: the client's copy of an
// unidentified unique read quality 1 in game, so the client would price it as a plain item and the buy is refused.
auto IsStock(Unit* item) noexcept -> bool {
	const auto* data = At<const uint8_t*>(item, d2r::rva::UnitData);
	if (data == nullptr || (At<uint32_t>(data, ItemDataFlags) & ItemFlagIdentified) != 0) {
		return false;
	}
	const uint32_t id = At<uint32_t>(item, UnitGuid);
	for (const auto& s : g_stock) {
		if (id != 0 && s.load(std::memory_order_relaxed) == id) {
			return true;
		}
	}
	return false;
}

// PD2 0x103841d0 (item level 99, mode 4, unique row forced) and 0x10314290 (into the store), unidentified (1.13c #10977
// clears flag 0x10) as PD2 leaves it
auto MakeUnique(Game* game, Unit* npc, int32_t row, uint32_t code) noexcept -> Unit* {
	g_forceUnique = row;
	Unit* item = g_maker(npc, code, game, 0, QualityUnique, ItemLevel, 0);
	g_forceUnique = -1;
	if (item != nullptr) {
		d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(d2r::sig::SetItemFlag)(item, ItemFlagIdentified, 0);
		g_stock[g_stockNext.fetch_add(1, std::memory_order_relaxed) % std::size(g_stock)].store(At<uint32_t>(item, UnitGuid),
		                                                                                         std::memory_order_relaxed);
	}
	return item;
}

// The buy (0x53d850) refuses an item whose server price differs from the price the client sent (result 7, "That item
// has just been traded."), so his stock's buy is repriced at the same calls on both sides. PD2's price (PD2.gpr
// 0x10385020: a map level 0x1037c510, NPC 0x93, its gamble mode 2) is 0x1037c4c0(1000000, 100, 100.0 - stat 0x57):
// 1,000,000 gold less the buyer's reduced-prices percent (0x10385076 mov ecx,0xf4240; 0x10385071 mov edx,0x64).
constexpr int64_t StockPrice = 1000000;
constexpr int32_t StatReducedPrices = 0x57;
constexpr auto StockPriceFor(int32_t reduced) noexcept -> uint64_t {
	const int64_t percent = reduced > 99 ? 99 : reduced;  // D2R's gamble path clamps it to 99; PD2 does not
	return static_cast<uint64_t>(StockPrice * (100 - percent) / 100);
}
static_assert(StockPriceFor(0) == 1000000 && StockPriceFor(10) == 900000 && StockPriceFor(200) == 10000);

auto Price(Unit* player, Unit* item, int32_t difficulty, void* table, int32_t npc, int32_t mode) noexcept -> uint64_t {
	constexpr int32_t ModeBuy = 0;
	if (mode != ModeBuy || player == nullptr || item == nullptr || !IsStock(item)) {
		return g_price(player, item, difficulty, table, npc, mode);
	}
	return StockPriceFor(d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat)(player, StatReducedPrices, 0));
}

// A page within rel32 reach below the image (as core-rules' AllocateRelay), for `jmp [rip+0]; entry`
auto AllocateRelay(uint64_t image) noexcept -> uint8_t* {
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	const uint64_t step = info.dwAllocationGranularity;
	const uint64_t reach = 0x70000000ull;
	const uint64_t low = image > reach ? image - reach : step;
	for (uint64_t at = (low + step - 1) & ~(step - 1); at < image; at += step) {
		if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
			return static_cast<uint8_t*>(p);
		}
	}
	return nullptr;
}

// All the price calls or none: each must be an e8 to the same thunk, and one MutationService transaction checks and
// writes them all (the stock bytes are the expected bytes), so the two sides never price his stock differently.
auto RedirectPrices(const D2RL::PluginContext* context) noexcept -> bool {
	constexpr size_t Count = std::size(PriceCalls);
	uint64_t sites[Count] {};
	uint8_t stock[Count][5] {};
	uint64_t thunk = 0;
	for (size_t i = 0; i < Count; ++i) {
		uint64_t match = 0;
		if (sigscan::ScanUnique(context->exeBase, PriceCalls[i], 0, &match) != 1) {
			return false;
		}
		sites[i] = context->exeBase + match + PriceCallAt;
		std::memcpy(stock[i], reinterpret_cast<const void*>(sites[i]), 5);
		int32_t rel = 0;
		std::memcpy(&rel, stock[i] + 1, 4);
		const uint64_t target = sites[i] + 5 + static_cast<int64_t>(rel);
		if (stock[i][0] != 0xe8 || (thunk != 0 && target != thunk)) {
			return false;
		}
		thunk = target;
	}
	const D2RL::MutationService* mutations = nullptr;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize)) {
		return false;
	}
	uint8_t* relay = AllocateRelay(context->exeBase);
	if (relay == nullptr) {
		return false;
	}
	const uint64_t entry = reinterpret_cast<uint64_t>(&Price);
	const uint8_t jump[6] = {0xff, 0x25, 0, 0, 0, 0};
	std::memcpy(relay, jump, sizeof(jump));
	std::memcpy(relay + sizeof(jump), &entry, 8);
	DWORD old;
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (VirtualProtect(relay, 0x1000, PAGE_EXECUTE_READ, &old) == 0 ||
	    mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		VirtualFree(relay, 0, MEM_RELEASE);
		return false;
	}
	g_price = reinterpret_cast<PriceFn>(thunk);  // before a call can land
	uint8_t calls[Count][5] {};
	bool staged = true;
	for (size_t i = 0; i < Count && staged; ++i) {
		const int64_t reach = static_cast<int64_t>(reinterpret_cast<uint64_t>(relay)) - static_cast<int64_t>(sites[i] + 5);
		const int32_t rel32 = static_cast<int32_t>(reach);
		calls[i][0] = 0xe8;
		std::memcpy(calls[i] + 1, &rel32, 4);
		const D2RL::Mutations::BytePatchRequest patch {.structSize = D2RL::Mutations::BytePatchRequestSize,
		                                               .rva = sites[i] - context->exeBase,
		                                               .expected = stock[i],
		                                               .expectedSize = 5,
		                                               .bytes = calls[i],
		                                               .size = 5};
		D2RL::Mutations::OperationHandle operation = 0;
		staged = reach == rel32 && mutations->stageBytePatch(context, transaction, &patch, &operation) == D2RL::Mutations::Result::Success;
	}
	D2RL::Mutations::CommitResult result {.structSize = D2RL::Mutations::CommitResultSize};
	if (!staged || mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		VirtualFree(relay, 0, MEM_RELEASE);
		return false;
	}
	return true;
}

// PD2's pick: up to 10 draws of rand() % rows, the first that passes wins; -1 when none does
auto Pick(const D2RL::DataTables::TableView& view, const D2RL::DataTableService* tables, D2RL::DataTables::Bank bank) noexcept
	-> int32_t {
	for (int32_t i = 0; i < PickTries && view.rowCount != 0; ++i) {
		const int32_t row = Rand() % static_cast<int32_t>(view.rowCount);
		const auto* rec = static_cast<const uint8_t*>(view.rows) + static_cast<size_t>(row) * view.rowSize;
		const UniqueRow r {At<uint32_t>(rec, UqCode), At<uint32_t>(rec, UqFlags), At<uint16_t>(rec, UqRarity),
		                   At<uint16_t>(rec, UqLvl), At<uint16_t>(rec, UqLvlReq)};
		if (!HighLevelPasses(r.lvlReq, r.lvlReq < 60 ? 0 : Rand() % 100) || !Sellable(r)) {
			continue;
		}
		// PD2 also needs the base item (0x1030dad0)
		D2RL::DataTables::RowView base {.structSize = D2RL::DataTables::RowViewSize};
		if (tables->findRowByCode(g_context, bank, D2RL::DataTables::TableId::Items, r.code, &base) == D2RL::DataTables::Result::Success) {
			return row;
		}
	}
	return -1;
}

auto BuildStock(Game* game, Unit* npc) noexcept -> bool {
	const D2RL::DataTableService* tables = nullptr;
	D2RL::DataTables::TableView view {.structSize = D2RL::DataTables::TableViewSize};
	const auto bank = static_cast<D2RL::DataTables::Bank>(d2r::DataFlag(game));
	if (g_context->QueryService(&tables) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasDataTableServiceField(tables, D2RL::DataTableServiceRequiredSize) ||
	    tables->getTable(g_context, bank, D2RL::DataTables::TableId::UniqueItems, &view) != D2RL::DataTables::Result::Success ||
	    view.rowSize <= UqLvlReq) {
		plog::Warn("gheed store: no uniqueitems table (bank %u), D2R's stock instead", static_cast<uint32_t>(bank));
		return false;
	}
	SetMapEventSpawns(game, StockBuilt);
	char rows[MaxUniques * 12] = "";
	int32_t made = 0;
	int32_t row = Pick(view, tables, bank);
	for (int32_t tries = MakeTries; row >= 0 && tries > 0 && made < MaxUniques; --tries) {
		const auto* rec = static_cast<const uint8_t*>(view.rows) + static_cast<size_t>(row) * view.rowSize;
		Unit* item = MakeUnique(game, npc, row, At<uint32_t>(rec, UqCode));
		if (item == nullptr) {
			continue;  // PD2 tries the same row again
		}
		++made;
		const size_t used = std::strlen(rows);
		std::snprintf(rows + used, sizeof(rows) - used, "%s%d(q%d)", made > 1 ? "," : "", row,
		              d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::sig::ItemQuality)(item));
		row = Pick(view, tables, bank);
	}
	g_stocks.fetch_add(1, std::memory_order_relaxed);
	g_uniques.fetch_add(static_cast<uint32_t>(made), std::memory_order_relaxed);
	plog::Info("gheed store: stock for Gheed in level %d (bank %u): %d unique(s), rows %s", LevelOf(npc),
	           static_cast<uint32_t>(bank), made, made != 0 ? rows : "none");
	return true;
}

auto Fill(Game* game, Unit* player, Unit* npc) noexcept -> void {
	// shortcut: a later restock of the map's Gheed (D2R's refresh) gets D2R's plain stock; PD2 keeps its one store all game
	if (game == nullptr || MapEvent(game) != GheedEvent || MapEventSpawns(game) >= StockBuilt || !IsMapGheed(npc)) {
		g_fill(game, player, npc);
		return;
	}
	for (auto& s : g_stock) {  // a new game's stock: the last game's ids may come back
		s.store(0, std::memory_order_relaxed);
	}
	if (!BuildStock(game, npc)) {
		g_fill(game, player, npc);
		return;
	}
	// what D2R's fill does first: stamp the store record's time, or its restock check fires at once
	uint64_t created = 0;
	if (uint8_t* record = g_record(game, npc, &created); record != nullptr) {
		*reinterpret_cast<uint64_t*>(record + StoreStamp) = g_now();
	}
}

auto ThinkNpc(Game* game, Unit* unit, uint8_t* tick) noexcept -> void {
	if (IsMapGheed(unit)) {
		g_idles.fetch_add(1, std::memory_order_relaxed);
		d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(d2r::sig::AiIdle)(game, unit, IdleFrames);
		return;
	}
	ai_registry::Vanilla(NpcAiRow)(game, unit, tick);
}

// the target of the e8 at `base + site.offset`, or 0 when the bytes around it differ
auto CallTarget(uint64_t base, const CallSite& site) noexcept -> uint64_t {
	const auto* at = reinterpret_cast<const uint8_t*>(base + site.offset);
	if (std::memcmp(at - sizeof(site.before), site.before, sizeof(site.before)) != 0 || at[0] != 0xe8 ||
	    std::memcmp(at + 5, site.after, sizeof(site.after)) != 0) {
		return 0;
	}
	int32_t rel = 0;
	std::memcpy(&rel, at + 1, sizeof(rel));
	return base + site.offset + 5 + static_cast<int64_t>(rel);
}

}  // namespace

auto InstallGheedStore(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	g_context = context;
	uint64_t maker = 0, think = 0, fill = 0;
	const bool found = d2r::sig::UnitRoom.state == sigscan::State::Ok && d2r::sig::RoomLevelId.state == sigscan::State::Ok &&
	                   d2r::sig::ItemSpawn.state == sigscan::State::Ok && d2r::sig::SetItemFlag.state == sigscan::State::Ok &&
	                   d2r::sig::ItemQuality.state == sigscan::State::Ok && d2r::sig::AiIdle.state == sigscan::State::Ok &&
		                   d2r::sig::UnitStat.state == sigscan::State::Ok &&
	                   sigscan::ScanUnique(context->exeBase, MakerPattern, 0, &maker) == 1 &&
	                   sigscan::ScanUnique(context->exeBase, NpcThinkPattern, 0, &think) == 1 &&
	                   sigscan::ScanUnique(context->exeBase, FillPattern, 0, &fill) == 1;
	const uint64_t record = found ? CallTarget(context->exeBase + fill, FillRecord) : 0;
	const uint64_t now = found ? CallTarget(context->exeBase + fill, FillNow) : 0;
	const uint64_t free = found ? CallTarget(context->exeBase + maker, MakerFree) : 0;
	if (!found || record == 0 || now == 0 || free == 0) {
		plog::Warn("gheed store: a signature or call site was not found (store fill, item maker, Npc think or a shared entry), off");
		std::snprintf(report, reportSize, "map Gheed FAILED (signature)");
		return 0;
	}
	g_maker = reinterpret_cast<MakerFn>(context->exeBase + maker);
	g_record = reinterpret_cast<RecordFn>(record);
	g_now = reinterpret_cast<NowFn>(now);
	g_free = reinterpret_cast<FreeFn>(free);
	// the item creation hook first: the fill hook is what calls into it
	const bool store = sigscan::HookBySignature(context, "StoreItemCreate", CreatePattern, 0, CreateEntry, sizeof(CreateEntry),
	                                            &CreateItem, &g_create) &&
	                   RedirectPrices(context) &&
	                   sigscan::HookBySignature(context, "TradeFill", FillPattern, 0, FillEntry, sizeof(FillEntry), &Fill, &g_fill);
	const bool still = ai_registry::RegisterAt(context, NpcAiRow, context->exeBase + think, GheedClass, &ThinkNpc);
	std::snprintf(report, reportSize, "map Gheed: unique stock%s, stands still%s", store ? "" : " FAILED", still ? "" : " FAILED");
	return store && still ? 1 : 0;
}

auto GheedStoreCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "gheed store: %u stocks, %u uniques, %u idles", g_stocks.load(), g_uniques.load(), g_idles.load());
}
