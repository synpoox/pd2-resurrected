// Map boss loot follows the map tier (#1423). A map boss's superuniques.txt row names one fixed Map Boss TC (T1-T3, and
// the Blood variant in Hell), but the same boss opens maps of every tier, and Cartographer takes at most 16 bosses.txt
// entries (a row per boss and tier would need 23). So the TC is chosen at the drop: D2R's TC drop (rva 0x4404f0, found
// by signature; no SDK service covers it) is wrapped, and when a superunique of a native map drops one of the Map Boss
// T1-T3 classes, the class of the open map's tier (map_events.cpp: MapTier) drops instead, in the same family
// (plain or Blood). Anything else (other monsters, other TCs, no map open) goes through unchanged.
#include "map_boss_tc.h"

#include "log.h"
#include "map_boss_tier.h"
#include "map_events.h"
#include "pd2_engine.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// D2R's monster type flags (UnitData + MonDataTypeFlags): 0x02 is a superunique (D2MOO's MONTYPE_SUPERUNIQUE)
constexpr uint8_t MonTypeSuperUnique = 0x02;
constexpr uint8_t TcDropEntry[] = {0x4c, 0x8b, 0xdc, 0x55, 0x53, 0x49, 0x8d, 0xab, 0x08, 0xf7, 0xff, 0xff};

using TcDropFn = uint64_t (*)(Game*, Unit*, Unit*, const uint8_t*, int32_t, int32_t, int32_t, void*, int32_t*, int32_t) noexcept;
TcDropFn g_original = nullptr;
std::atomic<uint32_t> g_swapped {0}, g_kept {0};

auto Record(Game* game, const char* name) noexcept -> const uint8_t* {
	using namespace d2r::rva;
	const auto dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const uint8_t* tables = d2r::Engine<const uint8_t* (*)(uint8_t) noexcept>(d2r::sig::DataTables)(dataFlag);
	const int32_t index = tables == nullptr ? 0
	                                        : d2r::Engine<int32_t (*)(void*, const char*, int32_t) noexcept>(d2r::sig::TcIndexByName)(
												  At<void*>(tables, TablesTcNames), name, 0);
	return index > 0 ? d2r::Engine<const uint8_t* (*)(uint8_t, uint16_t, int32_t) noexcept>(d2r::sig::TcRecord)(
						   dataFlag, static_cast<uint16_t>(index), 0)
	                 : nullptr;
}

auto OnTcDrop(Game* game, Unit* dropper, Unit* killer, const uint8_t* tc, int32_t quality, int32_t level, int32_t a7, void* items,
              int32_t* count, int32_t max) noexcept -> uint64_t {
	using namespace d2r::rva;
	const int32_t tier = game != nullptr && dropper != nullptr && At<int32_t>(dropper, UnitType) == UnitMonster ? MapTier(game) : 0;
	const auto* data = tier != 0 ? At<const uint8_t*>(dropper, UnitData) : nullptr;
	// shortcut: the dropper's level is not checked, as only the cartoboss: rows name Map Boss T1-T3; check it if a non-map monster ever uses them
	if (data != nullptr && (data[MonDataTypeFlags] & MonTypeSuperUnique) != 0) {
		for (const auto& family : pd2::mapboss::TcName) {
			for (const char* name : family) {
				if (tc == nullptr || Record(game, name) != tc) {
					continue;
				}
				const char* want = pd2::mapboss::TcForTier(name, tier);
				const uint8_t* swap = want != nullptr ? Record(game, want) : nullptr;
				const bool changed = swap != nullptr && swap != tc;
				(changed ? g_swapped : g_kept).fetch_add(1, std::memory_order_relaxed);
				plog::Info("map boss tc: monster %d in a tier %d map: its row names \"%s\", rolls \"%s\"", At<int32_t>(dropper, UnitClassId), tier, name,
				           swap != nullptr ? want : name);
				tc = swap != nullptr ? swap : tc;
				return g_original(game, dropper, killer, tc, quality, level, a7, items, count, max);
			}
		}
	}
	return g_original(game, dropper, killer, tc, quality, level, a7, items, count, max);
}

}  // namespace

auto InstallMapBossTc(const D2RL::PluginContext* context, char* report, size_t size) noexcept -> int {
	const bool hook = context->InstallInlineHook(d2r::sig::TcDrop.rva, TcDropEntry, sizeof(TcDropEntry),
	                                             reinterpret_cast<void*>(&OnTcDrop), reinterpret_cast<void**>(&g_original));
	std::snprintf(report, size, "%s", hook ? "map boss TC follows the map tier" : "off: TC drop hook failed (bytes differ)");
	return hook ? 1 : 0;
}

auto MapBossTcCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "map boss tc: %u swapped to the map's tier, %u already right", g_swapped.load(), g_kept.load());
}
