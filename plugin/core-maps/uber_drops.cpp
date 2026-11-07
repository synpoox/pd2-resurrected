// PD2's uber boss deaths (ProjectDiablo.dll 0x10378d80; docs/research/pd2-uber-bosses-dll.md, section 6). So far
// Diablo Clone's (0x103763d0): Annihilus, at tiers 1-2 a 1-in-200 roll for one of five uniques, and a gold portal to
// Harrogath at the body.
//
// key_drops' monster-death listener calls it on the server thread. The portal is engine calls there, as map_open's
// portals: no SDK service makes an object. The items go through the SDK's createItem (unique row and item level set,
// on the ground at the body). It needs the local player's handle, which the SDK gives only on the UI thread, and the
// game thread, so a drop hops UI -> game thread.
#include "uber_drops.h"

#include "log.h"
#include "map_open.h"

#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/threads.h>

#include <atomic>
#include <cstdio>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;
namespace items = D2RL::Items;

constexpr int32_t Dclone = 806;            // uberdiablonew (PD2 789)
constexpr int32_t GoldPortal = 589;        // xactportal_gold (PD2 586)
constexpr int32_t Harrogath = 109;
constexpr uint32_t TierKey = 0x55425254;   // 'UBRT', as map_open.cpp
constexpr uint32_t UniqueOdds = 200;       // tiers 1-2

struct Unique {
	uint32_t code;
	uint32_t row;    // uniqueitems runtime row (the merged file's row - 1, past `Expansion`)
	uint32_t level;  // PD2's item level
};
constexpr Unique Annihilus {D2RL::Items::MakeItemCode("cm1"), 381, 130};
// Itherael's Path, Overlord's Helm, Dark Abyss, Aidan's Scar, Hadriel's Hand
constexpr Unique Rolled[] = {{D2RL::Items::MakeItemCode("utb"), 438, 131}, {D2RL::Items::MakeItemCode("uh9"), 439, 131},
                             {D2RL::Items::MakeItemCode("uth"), 440, 131}, {D2RL::Items::MakeItemCode("7qr"), 441, 131},
                             {D2RL::Items::MakeItemCode("7bs"), 442, 131}};

const D2RL::PluginContext* g_context = nullptr;
const D2RL::ItemService* g_items = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ThreadService* g_threads = nullptr;

// A drop on its way to the game thread. A death fills a free slot; the game-thread callback frees it.
struct Drop {
	std::atomic<bool> busy {false};
	Unique item {};
	int32_t x = 0, y = 0;
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
};
Drop g_drops[8];

std::atomic<uint32_t> g_deaths {0};
std::atomic<uint32_t> g_dropped {0};
std::atomic<uint32_t> g_failed {0};
std::atomic<uint32_t> g_rolls {0};
std::atomic<uint32_t> g_hits {0};
std::atomic<uint32_t> g_portals {0};

void CreateNow(const D2RL::PluginContext*, void* user) noexcept {
	auto* drop = static_cast<Drop*>(user);
	const items::ItemCreateSpec spec {
		.structSize = items::ItemCreateSpecSize,
		.flags = items::ItemCreateFlagBit(items::ItemCreateFlag::AllowDuplicateUnique),
		.code = drop->item.code,
		.quality = items::Quality::Unique,
		.qualityRecordId = drop->item.row,
		.itemLevel = drop->item.level,
		.seedMode = items::SeedMode::Random,
		.quantity = items::DefaultValue,
		.durability = items::DefaultValue,
		.destination = {.structSize = items::ItemDestinationSize,
		                .container = items::ItemContainer::Ground,
		                .placement = items::Placement::Exact,
		                .x = static_cast<uint32_t>(drop->x),
		                .y = static_cast<uint32_t>(drop->y)}};
	D2RL::ItemHandle made = D2RL::InvalidItemHandle;
	const items::Result status = g_items->createItem(g_context, drop->player, &spec, &made);
	items::ItemInfo info {.structSize = items::ItemInfoSize};
	const bool read = status == items::Result::Success && g_items->getItemInfo(g_context, made, &info) == items::Result::Success;
	(status == items::Result::Success ? g_dropped : g_failed).fetch_add(1, std::memory_order_relaxed);
	plog::Info("uber drops: %.4s unique row %u level %u at (%d, %d): result %u, made row %d level %u",
	           reinterpret_cast<const char*>(&drop->item.code), drop->item.row, drop->item.level, drop->x, drop->y,
	           static_cast<uint32_t>(status), read ? info.qualityRecordId : -1, read ? info.itemLevel : 0);
	drop->busy = false;
}

void PickPlayer(const D2RL::PluginContext*, void* user) noexcept {
	auto* drop = static_cast<Drop*>(user);
	if (g_inventory->getLocalPlayer(g_context, &drop->player) != D2RL::Inventory::Result::Success ||
	    g_threads->runOnGameThread(g_context, CreateNow, drop) != D2RL::Threads::Result::Success) {
		plog::Warn("uber drops: %.4s not made (no local player or game thread)", reinterpret_cast<const char*>(&drop->item.code));
		g_failed.fetch_add(1, std::memory_order_relaxed);
		drop->busy = false;
	}
}

void Queue(const Unique& item, int32_t x, int32_t y) noexcept {
	for (Drop& drop : g_drops) {
		if (!drop.busy.exchange(true)) {
			drop.item = item;
			drop.x = x;
			drop.y = y;
			if (g_threads->runOnUiThread(g_context, PickPlayer, &drop) != D2RL::Threads::Result::Success) {
				drop.busy = false;
				break;
			}
			return;
		}
	}
	plog::Warn("uber drops: %.4s not queued", reinterpret_cast<const char*>(&item.code));
	g_failed.fetch_add(1, std::memory_order_relaxed);
}

auto Tier(Game* game) noexcept -> int32_t {
	uint32_t key = TierKey;
	const int32_t* v = d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
	return v != nullptr ? *v : 0;
}

}  // namespace

auto InstallUberDrops(const D2RL::PluginContext* context) noexcept -> bool {
	g_context = context;
	const bool items = context->QueryService(&g_items) == D2RL::ServiceQueryResult::Success &&
	                   D2RL::HasItemServiceField(g_items, D2RL::ItemServiceRequiredSize);
	const bool inventory = context->QueryService(&g_inventory) == D2RL::ServiceQueryResult::Success &&
	                       D2RL::HasInventoryServiceField(g_inventory, D2RL::InventoryServiceRequiredSize);
	const bool threads = context->QueryService(&g_threads) == D2RL::ServiceQueryResult::Success &&
	                     D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize);
	if (!(items && inventory && threads)) {
		g_items = nullptr;
	}
	return g_items != nullptr;
}

auto UberDeathDrops(Game* game, Unit* monster, uint32_t* seed) noexcept -> void {
	using namespace d2r::rva;
	if (g_items == nullptr || At<int32_t>(monster, UnitClassId) != Dclone) {
		return;
	}
	int32_t x = 0, y = 0;
	if (!d2r::UnitXY(monster, &x, &y)) {
		plog::Warn("uber drops: Diablo Clone has no position");
		return;
	}
	const int32_t tier = Tier(game);
	const uint32_t deaths = g_deaths.fetch_add(1, std::memory_order_relaxed) + 1;
	Queue(Annihilus, x, y);
	if (tier >= 1) {
		const uint32_t roll = d2r::RandomBelow(seed, UniqueOdds);
		g_rolls.fetch_add(1, std::memory_order_relaxed);
		if (roll == 0) {
			const Unique& item = Rolled[d2r::RandomBelow(seed, static_cast<uint32_t>(std::size(Rolled)))];
			g_hits.fetch_add(1, std::memory_order_relaxed);
			Queue(item, x, y);
		}
		plog::Info("uber drops: tier %d, 1 in %u roll %u%s", tier, UniqueOdds, roll, roll == 0 ? ": a unique" : "");
	}
	const bool portal =
		OpenPortal(game, d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(monster), GoldPortal, x, y, Harrogath) != nullptr;
	g_portals.fetch_add(portal ? 1 : 0, std::memory_order_relaxed);
	plog::Info("uber drops: Diablo Clone died at (%d, %d), tier %d, gold portal %s (%u so far)", x, y, tier,
	           portal ? "open" : "FAILED", deaths);
}

auto UberDropsCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "uber drops: %u Diablo Clone deaths, %u items made, %u failed, %u of %u unique rolls hit, "
	              "%u gold portals", g_deaths.load(), g_dropped.load(), g_failed.load(), g_hits.load(), g_rolls.load(),
	              g_portals.load());
}
