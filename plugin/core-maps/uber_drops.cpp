// PD2's uber boss deaths (ProjectDiablo.dll 0x10378d80; docs/research/pd2-uber-bosses-dll.md, section 6). So far
// Diablo Clone's (0x103763d0): Annihilus, and at tiers 1-2 a 1-in-200 roll for one of five uniques.
// shortcut: PD2's gold portal to Harrogath at the body is left out; a later UB4 slice adds it.
//
// key_drops' monster-death listener calls it on the server thread, and the items drop by the body through D2R's own
// monster drop with the unique row forced (rva::DropItemAt, as key_drops). Native, not the SDK's createItem: its ground
// destination (the only way to put an item at a body) crashed the game a frame after Annihilus appeared, on a null
// room, with exact and automatic placement alike (Slot4, #229). The drop's item level is the monster's level stat, so
// Dclone's (110) is raised to PD2's item level for the drop: at 110, Annihilus (lvl 130) came out magic.
#include "uber_drops.h"

#include "log.h"

#include <D2RLPlugin/item.h>

#include <atomic>
#include <cstdio>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t Dclone = 806;           // uberdiablonew (PD2 789)
constexpr uint32_t TierKey = 0x55425254;  // 'UBRT', as map_open.cpp
constexpr uint32_t UniqueOdds = 200;      // tiers 1-2
constexpr int32_t QualityUnique = 7;

constexpr int32_t StatLevel = 12;

struct Unique {
	uint32_t code;
	int32_t row;    // uniqueitems runtime row (the merged file's row - 1, past `Expansion`)
	int32_t level;  // PD2's item level
};
constexpr Unique Annihilus {D2RL::Items::MakeItemCode("cm1"), 381, 130};
// Itherael's Path, Overlord's Helm, Dark Abyss, Aidan's Scar, Hadriel's Hand
constexpr Unique Rolled[] = {
	{D2RL::Items::MakeItemCode("utb"), 438, 131}, {D2RL::Items::MakeItemCode("uh9"), 439, 131},
	{D2RL::Items::MakeItemCode("uth"), 440, 131}, {D2RL::Items::MakeItemCode("7qr"), 441, 131},
	{D2RL::Items::MakeItemCode("7bs"), 442, 131}};

std::atomic<uint32_t> g_deaths {0};
std::atomic<uint32_t> g_dropped {0};
std::atomic<uint32_t> g_failed {0};
std::atomic<uint32_t> g_rolls {0};
std::atomic<uint32_t> g_hits {0};

auto Tier(Game* game) noexcept -> int32_t {
	uint32_t key = TierKey;
	const int32_t* v = d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
	return v != nullptr ? *v : 0;
}

void Drop(Game* game, Unit* monster, const Unique& item) noexcept {
	using namespace d2r::rva;
	const int32_t row =
		d2r::Engine<int32_t (*)(uint8_t, uint32_t) noexcept>(ItemRowByCode)(static_cast<uint8_t>(d2r::DataFlag(game)), item.code);
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const auto setStat = d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat);
	const int32_t level = stat(monster, StatLevel, 0);
	setStat(monster, StatLevel, item.level, 0);
	auto* made = row >= 0 ? static_cast<Unit*>(d2r::Engine<void* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t) noexcept>(
								DropItemAt)(game, monster, row, QualityUnique, item.row + 1, 0))
	                      : nullptr;
	setStat(monster, StatLevel, level, 0);
	const int32_t quality = made != nullptr ? d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemQuality)(made) : -1;
	(quality == QualityUnique ? g_dropped : g_failed).fetch_add(1, std::memory_order_relaxed);
	plog::Info("uber drops: %.4s unique row %d: item row %d, %s, quality %d", reinterpret_cast<const char*>(&item.code),
	           item.row, row, made != nullptr ? "dropped" : "NOT dropped", quality);
}

}  // namespace

auto UberDeathDrops(Game* game, Unit* monster, uint32_t* seed) noexcept -> void {
	if (At<int32_t>(monster, d2r::rva::UnitClassId) != Dclone) {
		return;
	}
	const int32_t tier = Tier(game);
	const uint32_t deaths = g_deaths.fetch_add(1, std::memory_order_relaxed) + 1;
	plog::Info("uber drops: Diablo Clone died, tier %d (%u so far)", tier, deaths);
	Drop(game, monster, Annihilus);
	if (tier >= 1) {
		const uint32_t roll = d2r::RandomBelow(seed, UniqueOdds);
		g_rolls.fetch_add(1, std::memory_order_relaxed);
		plog::Info("uber drops: tier %d, 1 in %u roll %u%s", tier, UniqueOdds, roll, roll == 0 ? ": a unique" : "");
		if (roll == 0) {
			g_hits.fetch_add(1, std::memory_order_relaxed);
			Drop(game, monster, Rolled[d2r::RandomBelow(seed, static_cast<uint32_t>(std::size(Rolled)))]);
		}
	}
}

auto UberDropsCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "uber drops: %u Diablo Clone deaths, %u uniques dropped, %u failed, %u of %u unique rolls hit",
	              g_deaths.load(), g_dropped.load(), g_failed.load(), g_hits.load(), g_rolls.load());
}
