// The Rathma pair's last death (ProjectDiablo.dll 0x10376840; docs/research/pd2-uber-bosses-dll.md, section 6): when
// the second of the two clones dies, PD2 drops the Tainted Worldstone Shard, rolls 1 in (50 - 6 x tier) for an
// unlimited tome or key and, at tiers 1-2, 1 in 200 for one of three uniques, and opens a gold portal to Harrogath at
// the body. It rides key_drops' monster-death listener (the SDK's), with the same engine calls for the drop.
#include "rathma.h"

#include "log.h"
#include "map_open.h"

#include <D2RLPlugin/item.h>

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// rathmaBoneClone and rathmaPoisonClone (PD2 935/936), in the port's monstats ids
constexpr int32_t Clones[] = {952, 953};
constexpr uint32_t TierKey = 0x55425254;  // 'UBRT', as map_open.cpp
// Set once the pair's drop is made, so two clones dying on one frame drop once
constexpr uint32_t DoneKey = 0x55425244;  // 'UBRD'
constexpr int32_t Harrogath = 109;
constexpr int32_t GoldPortal = 589;  // xactportal_gold (PD2 586)
constexpr int32_t QualityUnique = 7;
constexpr uint32_t Shard = D2RL::Items::MakeItemCode("cwss");
constexpr uint32_t Tomes[] = {D2RL::Items::MakeItemCode("rid"), D2RL::Items::MakeItemCode("rtp"),
                              D2RL::Items::MakeItemCode("rkey")};
// The Third Eye (ram), Cage of the Unsullied (rar), Band of Skulls (rbe): code and runtime uniqueitems row
struct UniqueDrop {
	uint32_t code;
	int32_t row;
};
constexpr UniqueDrop Uniques[] = {{D2RL::Items::MakeItemCode("ram"), 455},
                                  {D2RL::Items::MakeItemCode("rar"), 454},
                                  {D2RL::Items::MakeItemCode("rbe"), 453}};

constexpr auto TomeOdds(int32_t tier) noexcept -> uint32_t {
	return static_cast<uint32_t>(50 - 6 * tier);
}
static_assert(TomeOdds(0) == 50 && TomeOdds(1) == 44 && TomeOdds(2) == 38);

std::atomic<uint32_t> g_kills {0};
std::atomic<uint32_t> g_tomes {0};
std::atomic<uint32_t> g_uniques {0};
std::atomic<uint32_t> g_portals {0};

auto IsClone(int32_t classId) noexcept -> bool {
	return classId == Clones[0] || classId == Clones[1];
}

auto Counter(Game* game, uint32_t key) noexcept -> int32_t* {
	return d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

// The other clone, alive anywhere in the game
auto PartnerAlive(Game* game, Unit* dead) noexcept -> bool {
	using namespace d2r::rva;
	for (int bucket = 0; bucket < 128; ++bucket) {
		for (auto* unit = At<Unit*>(game, GameMonsters + bucket * 8); unit != nullptr;
		     unit = At<Unit*>(unit, UnitListNext)) {
			if (unit != dead && IsClone(At<int32_t>(unit, UnitClassId)) &&
			    d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(unit) == 0) {
				return true;
			}
		}
	}
	return false;
}

// shortcut: the item takes the monster's item level, where PD2 makes these at 130; set it if a unique ever fails
// to drop for its level (the three are level 131)
auto Drop(Game* game, Unit* monster, uint32_t code, int32_t quality, int32_t uniqueRow) noexcept -> void {
	using namespace d2r::rva;
	const uint8_t dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const int32_t row = d2r::Engine<int32_t (*)(uint8_t, uint32_t) noexcept>(ItemRowByCode)(dataFlag, code);
	if (row < 0) {
		plog::Warn("rathma: no item \"%.4s\"", reinterpret_cast<const char*>(&code));
		return;
	}
	d2r::Engine<void* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t) noexcept>(DropItemAt)(
		game, monster, row, quality, uniqueRow + 1, 0);
	plog::Info("rathma: dropped %.4s", reinterpret_cast<const char*>(&code));
}

}  // namespace

auto RathmaDeath(Game* game, Unit* monster) noexcept -> void {
	using namespace d2r::rva;
	if (!IsClone(At<int32_t>(monster, UnitClassId)) || PartnerAlive(game, monster)) {
		return;
	}
	int32_t* done = Counter(game, DoneKey);
	if (done == nullptr || *done != 0) {
		return;
	}
	*done = 1;
	const int32_t* tierCounter = Counter(game, TierKey);
	const int32_t tier = tierCounter != nullptr ? *tierCounter : 0;
	auto* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(monster);
	Drop(game, monster, Shard, QualityNormal, -1);
	if (d2r::RandomBelow(seed, TomeOdds(tier)) == 0) {
		Drop(game, monster, Tomes[d2r::RandomBelow(seed, 3)], QualityNormal, -1);
		++g_tomes;
	}
	if (tier >= 1 && d2r::RandomBelow(seed, 200) == 0) {
		const UniqueDrop& u = Uniques[d2r::RandomBelow(seed, 3)];
		Drop(game, monster, u.code, QualityUnique, u.row);
		++g_uniques;
	}
	int32_t xy[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(monster, xy);
	if (OpenPortalAt(game, d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(monster), GoldPortal, xy[0], xy[1],
	                 Harrogath) != nullptr) {
		++g_portals;
	}
	plog::Info("rathma: pair dead at tier %d (%u kills, %u tome rolls hit, %u uniques, %u portals)", tier, ++g_kills,
	           g_tomes.load(), g_uniques.load(), g_portals.load());
}

auto RathmaCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "rathma: %u pair kills, %u tome rolls hit, %u uniques, %u portals", g_kills.load(),
	              g_tomes.load(), g_uniques.load(), g_portals.load());
}
