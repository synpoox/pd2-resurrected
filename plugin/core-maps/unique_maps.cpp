// The unique maps' boss deaths (MS7b; docs/research/pd2-map-system-dll.md, section 6). PD2's monster-death handler
// (ProjectDiablo.dll 0x1037a330) switches on the dead monster's class (0x10378d80); each unique map boss's entry acts
// only while the game's map event (+0x1df4) holds its map's value, which opening the map sets (map_events.cpp):
// - 0x1e Zhar's Sanctum: ZharTheMad (0x10376ea0) opens a gold portal to Harrogath at the body and drops a magic grand
//   charm at item level 99; each of his three mini-bosses (0x10376dc0) drops one at 90, 1 in 2, until Zhar dies.
// - 0x1f Warlord of Blood: WarlordOfBlood (0x10376fb0) opens the gold portal.
// - 0x20 Fallen Gardens: Iskatu (0x103774f0) opens the gold portal and drops one of the eight Rainbow Facets
//   (0x103841d0, uniques 392-399); Rakanoth (0x10377490) opens a red portal to the Diamond Gate.
// - 0x22 Imperial Palace: the mini-boss Kamyr (0x103775b0) opens a red portal into the boss arena and drops a Puzzle
//   Piece; the boss (0x10377670) opens the gold portal and drops a Puzzle Box.
// - 0x23 City of Ureh: the boss (0x10377270) opens the gold portal.
// The trigger is the SDK's monster-death listener (key_drops.cpp); the portal and the drop are the engine calls rathma.cpp
// makes, as no SDK service makes either. What is left out is in docs/deviations.md.
#include "unique_maps.h"

#include "log.h"
#include "map_events.h"
#include "map_open.h"

#include <D2RLPlugin/item.h>

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

std::atomic<uint32_t> g_deaths {0};
std::atomic<uint32_t> g_portals {0};
std::atomic<uint32_t> g_drops {0};

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
	d2r::Engine<void* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t) noexcept>(DropItemAt)(
		game, monster, row, quality, uniqueRow + 1, 0);
	plog::Info("unique maps: dropped %.4s (quality %d) (%u)", reinterpret_cast<const char*>(&code), quality, ++g_drops);
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

auto UniqueMapsCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "unique maps: %u boss deaths, %u portals, %u drops", g_deaths.load(), g_portals.load(),
	              g_drops.load());
}
