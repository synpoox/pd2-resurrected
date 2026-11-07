// The unique maps' boss deaths (MS7b, MS7c; docs/research/pd2-map-system-dll.md, section 6). PD2's monster-death
// handler (ProjectDiablo.dll 0x1037a330) switches on the dead monster's class (0x10378d80); each unique map boss's entry
// acts only while the game's map event (+0x1df4) holds its map's value, which opening the map sets (map_events.cpp):
// - 0x1e Zhar's Sanctum: ZharTheMad (0x10376ea0) opens a gold portal to Harrogath at the body and drops a magic grand
//   charm at item level 99; each of his three mini-bosses (0x10376dc0) drops one at 90, 1 in 2, until Zhar dies.
// - 0x1f Warlord of Blood: WarlordOfBlood (0x10376fb0) opens the gold portal, drops 2-3 random uniques and kills the
//   `demonpillar`s in his room and the rooms next to it.
// - 0x20 Fallen Gardens: Iskatu (0x103774f0) opens the gold portal and drops one of the eight Rainbow Facets
//   (0x103841d0, uniques 392-399); Rakanoth (0x10377490) opens a red portal to the Diamond Gate.
// - 0x22 Imperial Palace: the mini-boss Kamyr (0x103775b0) opens a red portal into the boss arena and drops a Puzzle
//   Piece; the boss (0x10377670) opens the gold portal and drops a Puzzle Box.
// - 0x23 City of Ureh: the boss (0x10377270) opens the gold portal and drops 3 random uniques of socketable bases.
// A random unique is a uniqueitems row PD2 accepts (enabled, rarity above 0, level 1-99): PD2 drops a unique of that
// row's base item (0x103a3a50), and the engine's own unique roll then picks the row, as here.
// The trigger is the SDK's monster-death listener (key_drops.cpp); the portal, the drop and the kill are engine calls
// (rathma.cpp, lucion.cpp), as no SDK service makes them. The uniques' bases are read from DataTableService after each
// table load, as that service answers Busy on the death listener's thread. What is left out is in docs/deviations.md.
#include "unique_maps.h"

#include "log.h"
#include "lucion.h"
#include "map_events.h"
#include "map_open.h"

#include <D2RLPlugin/data_tables.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/lifecycle_events.h>

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// The bosses, in the port's monstats ids (PD2's hcIdx + 17)
constexpr int32_t Zhar = 1075, Warlord = 1077, Iskatu = 1111, Rakanoth = 1113, Kamyr = 1165, ImperialBoss = 1183,
                  UrehBoss = 1191;
constexpr int32_t ZharMiniBosses[] = {1078, 1079, 1080};  // ZharMiniBossBigHead, ZharMiniBossBaboon, ZharMiniBossCantor
constexpr int32_t DemonPillar = 1074;                     // demonpillar (PD2 1057)
// The map event values (PD2 0x10383060)
constexpr int32_t ZharEvent = 0x1e, WarlordEvent = 0x1f, GardensEvent = 0x20, ImperialEvent = 0x22, UrehEvent = 0x23;
// Port level ids: Harrogath, the Diamond Gate (PD2 184), the Imperial Boss Map (PD2 194)
constexpr int32_t Harrogath = 109, DiamondGate = 180, ImperialArena = 190;
constexpr int32_t GoldPortal = 589;  // xactportal_gold (PD2 586)
constexpr int32_t RedPortal = 587;   // xactportal_red (PD2 584)
constexpr int32_t QualityMagic = 4, QualityUnique = 7;
constexpr uint32_t GrandCharm = D2RL::Items::MakeItemCode("cm3");
constexpr uint32_t PuzzlePiece = D2RL::Items::MakeItemCode("lpp");
constexpr uint32_t PuzzleBox = D2RL::Items::MakeItemCode("lbox");
constexpr uint32_t Jewel = D2RL::Items::MakeItemCode("jew");
constexpr int32_t FirstFacet = 392;  // Rainbow Facet, runtime uniqueitems rows 392-399 (merge/ids-uniqueitems.tsv)
// PD2's game +0x262a, Zhar's Sanctum's progress: below 4 the mini-bosses drop charms; Zhar's death sets it to 4
constexpr uint32_t ZharKey = 0x4d5a4852;  // 'MZHR'
constexpr int MaxPillars = 32;

// D2R's compiled uniqueitems row (uniqueitems.bin, 348 bytes): 1.13c's to +0x2c, then 4 more bytes before rarity
constexpr uint32_t UniqueRowSize = 348;
constexpr int UqCode = 0x28, UqFlags = 0x2c, UqRarity = 0x34, UqLevel = 0x38;
// PD2 tests the flags for non-zero, which in 1.13c means the enabled bit; D2R also sets 0x8 on rows that never spawn
constexpr uint32_t UqSpawnable = 1;
constexpr uint32_t ItemRowSize = 448;  // weapons.bin, armor.bin and misc.bin rows
constexpr int ItemGemSockets = 0x148;  // uint8 gemsockets: 1.13c's +0x138, which PD2's Ureh drop tests
constexpr int MaxBases = 4096;

// The base codes of the rows a random unique may take: all of them, and those whose base takes sockets (Ureh)
uint32_t g_anyBases[MaxBases];
uint32_t g_socketBases[MaxBases];
std::atomic<int> g_anyCount {0};
std::atomic<int> g_socketCount {0};
const D2RL::DataTableService* g_tables = nullptr;

std::atomic<uint32_t> g_deaths {0};
std::atomic<uint32_t> g_portals {0};
std::atomic<uint32_t> g_drops {0};
std::atomic<uint32_t> g_pillars {0};

auto Counter(Game* game, uint32_t key) noexcept -> int32_t* {
	return d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

// shortcut: the portal stands at the body, as rathma.cpp's, where PD2 first moves it to a free spot (0x10389ad0); in
// the Imperial Palace it can land behind bars. Use event_portals.cpp's FindFreeSpot if players can't reach one
auto Portal(Game* game, Unit* monster, int32_t object, int32_t level) noexcept -> void {
	using namespace d2r::rva;
	int32_t xy[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(monster, xy);
	if (OpenPortalAt(game, d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(monster), object, xy[0], xy[1], level) !=
	    nullptr) {
		plog::Info("unique maps: portal %d to level %d at (%d, %d) (%u)", object, level, xy[0], xy[1], ++g_portals);
	}
}

// shortcut: the item takes the monster's item level, where PD2 names one (90, 99 or 130); set it if a drop ever
// differs by level
auto Drop(Game* game, Unit* monster, uint32_t code, int32_t quality, int32_t uniqueRow) noexcept -> void {
	using namespace d2r::rva;
	const int32_t row = d2r::Engine<int32_t (*)(uint8_t, uint32_t) noexcept>(ItemRowByCode)(
		static_cast<uint8_t>(d2r::DataFlag(game)), code);
	if (row < 0) {
		plog::Warn("unique maps: no item \"%.4s\"", reinterpret_cast<const char*>(&code));
		return;
	}
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t) noexcept>(DropItemAt)(
		game, monster, row, quality, uniqueRow + 1, 0);
	plog::Info("unique maps: dropped %.4s (quality %d) (%u)", reinterpret_cast<const char*>(&code), quality, ++g_drops);
}

// PD2 draws rows at random until one passes (20 tries for the Warlord, 40 for Ureh, then it drops no more); here the
// draw is over the passing rows only
auto RandomUniques(Game* game, Unit* monster, uint32_t* seed, uint32_t count, bool socketed) noexcept -> void {
	const uint32_t* bases = socketed ? g_socketBases : g_anyBases;
	const int n = (socketed ? g_socketCount : g_anyCount).load(std::memory_order_acquire);
	if (n <= 0) {
		plog::Warn("unique maps: no random unique bases read");
		return;
	}
	for (uint32_t i = 0; i < count; ++i) {
		Drop(game, monster, bases[d2r::RandomBelow(seed, static_cast<uint32_t>(n))], QualityUnique, -1);
	}
}

// PD2 0x10376fb0: the pillars in the Warlord's room and its neighbours, picked first so no kill runs while a room's
// units are walked
auto KillPillars(Game* game, Unit* warlord) noexcept -> void {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(warlord);
	void** rooms = nullptr;
	uint32_t count = 0;
	if (room != nullptr) {
		d2r::Engine<void (*)(void*, void***, uint32_t*) noexcept>(NearRooms)(room, &rooms, &count);
	}
	Unit* found[MaxPillars] = {};
	int n = 0;
	for (uint32_t r = 0; rooms != nullptr && r < count; ++r) {
		for (auto* unit = d2r::Engine<Unit* (*)(void*) noexcept>(RoomFirstUnit)(rooms[r]); unit != nullptr && n < MaxPillars;
		     unit = d2r::Engine<Unit* (*)(Unit*) noexcept>(NextUnitInRoom)(unit)) {
			if (At<int32_t>(unit, UnitType) == UnitMonster && At<int32_t>(unit, UnitClassId) == DemonPillar &&
			    d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(unit) == 0) {
				found[n++] = unit;
			}
		}
	}
	for (int i = 0; i < n; ++i) {
		KillQuietly(game, found[i]);
	}
	g_pillars.fetch_add(static_cast<uint32_t>(n), std::memory_order_relaxed);
	plog::Info("unique maps: %d demon pillars killed in %u rooms", n, count);
}

void __cdecl ReadUniqueBases(const D2RL::PluginContext* context, const D2RL::Lifecycle::DataTablesLoadedEvent*,
                             void*) noexcept {
	using namespace D2RL::DataTables;
	g_anyCount.store(0, std::memory_order_release);
	g_socketCount.store(0, std::memory_order_release);
	TableView view {.structSize = TableViewSize};
	// shortcut: the RotW bank's rows only; a LoD or Classic game draws from them too, and a base its bank lacks drops
	// nothing (Drop's "no item"). Read each game's own bank if those modes ever get the maps
	if (g_tables->getTable(context, Bank::Rotw, TableId::UniqueItems, &view) != Result::Success ||
	    view.rowSize != UniqueRowSize || view.rows == nullptr) {
		plog::Warn("unique maps: no uniqueitems rows (row size %u), no random uniques", view.rowSize);
		return;
	}
	int any = 0, socketed = 0;
	for (uint32_t i = 0; i < view.rowCount && any < MaxBases; ++i) {
		const auto* row = static_cast<const uint8_t*>(view.rows) + static_cast<size_t>(i) * UniqueRowSize;
		const uint16_t level = At<uint16_t>(row, UqLevel);
		if ((At<uint32_t>(row, UqFlags) & UqSpawnable) == 0 || At<uint32_t>(row, UqRarity) == 0 || level == 0 ||
		    level > 99) {
			continue;
		}
		const uint32_t code = At<uint32_t>(row, UqCode);
		g_anyBases[any++] = code;
		RowView item {.structSize = RowViewSize};
		if (g_tables->findRowByCode(context, Bank::Rotw, TableId::Items, code, &item) == Result::Success &&
		    item.rowSize == ItemRowSize && item.row != nullptr && At<uint8_t>(item.row, ItemGemSockets) != 0) {
			g_socketBases[socketed++] = code;
		}
	}
	g_anyCount.store(any, std::memory_order_release);
	g_socketCount.store(socketed, std::memory_order_release);
	plog::Info("unique maps: %d of %u uniques can drop at random, %d of them socketable", any, view.rowCount, socketed);
}

}  // namespace

auto UniqueMapDeath(Game* game, Unit* monster) noexcept -> void {
	using namespace d2r::rva;
	const int32_t event = MapEvent(game);
	if (event < ZharEvent || event > UrehEvent) {
		return;
	}
	const int32_t monsterClass = At<int32_t>(monster, UnitClassId);
	auto* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(monster);
	const bool miniBoss = monsterClass == ZharMiniBosses[0] || monsterClass == ZharMiniBosses[1] ||
	                      monsterClass == ZharMiniBosses[2];
	if (event == ZharEvent && (monsterClass == Zhar || miniBoss)) {
		int32_t* progress = Counter(game, ZharKey);
		if (progress == nullptr || *progress >= 4) {
			return;
		}
		++g_deaths;
		if (monsterClass == Zhar) {
			*progress = 4;
			Portal(game, monster, GoldPortal, Harrogath);
			// shortcut: PD2 also forces one magic affix on the charm (0x102e5110, not read); read it if the charm
			// should differ from a plain magic one
			Drop(game, monster, GrandCharm, QualityMagic, -1);
		} else if (d2r::RandomBelow(seed, 2) == 0) {
			Drop(game, monster, GrandCharm, QualityMagic, -1);
		}
	} else if ((event == WarlordEvent && monsterClass == Warlord) || (event == UrehEvent && monsterClass == UrehBoss) ||
	           (event == ImperialEvent && monsterClass == ImperialBoss) ||
	           (event == GardensEvent && monsterClass == Iskatu)) {
		++g_deaths;
		Portal(game, monster, GoldPortal, Harrogath);
		if (monsterClass == Iskatu) {
			Drop(game, monster, Jewel, QualityUnique, FirstFacet + static_cast<int32_t>(d2r::RandomBelow(seed, 8)));
		} else if (monsterClass == ImperialBoss) {
			Drop(game, monster, PuzzleBox, QualityUnique, -1);
		} else if (monsterClass == Warlord) {
			RandomUniques(game, monster, seed, 2 + d2r::RandomBelow(seed, 2), false);
			KillPillars(game, monster);
		} else {
			RandomUniques(game, monster, seed, 3, true);
		}
	} else if (event == GardensEvent && monsterClass == Rakanoth) {
		++g_deaths;
		Portal(game, monster, RedPortal, DiamondGate);
	} else if (event == ImperialEvent && monsterClass == Kamyr) {
		++g_deaths;
		// shortcut: PD2 links this portal to an `empty` marker in the arena (0x1038a1f0, 0x1038a510); here it leads to
		// the arena level's own arrival spot
		Portal(game, monster, RedPortal, ImperialArena);
		Drop(game, monster, PuzzlePiece, QualityUnique, -1);
	} else {
		return;
	}
	plog::Info("unique maps: monster %d died in map event 0x%x (%u boss deaths)", monsterClass, event, g_deaths.load());
}

auto InstallUniqueMaps(const D2RL::PluginContext* context) noexcept -> bool {
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&g_tables) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasDataTableServiceField(g_tables, D2RL::DataTableServiceRequiredSize) ||
	    context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		return false;
	}
	const D2RL::Lifecycle::DataTablesLoadedListener listener {.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
	                                                          .callback = ReadUniqueBases};
	D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
	return lifecycle->registerDataTablesLoadedListener(context, &listener, &handle) == D2RL::Lifecycle::Result::Success;
}

auto UniqueMapsCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "unique maps: %u boss deaths, %u portals, %u drops, %u pillars killed, %d random unique bases",
	              g_deaths.load(), g_portals.load(), g_drops.load(), g_pillars.load(), g_anyCount.load());
}
