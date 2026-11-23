// Ormus' Gidbinn ring is corrupted (#1950). PD2 redirects 1.13c's one quest item call in the Gidbinn reward (D2Game
// +0xe97d7, the rare `rin `) to ProjectDiablo.dll 0x10372150, which makes the item, then runs its normal Corruption roll
// (0x103649d0, flag 0) on it. D2R makes the ring in a deferred event's execute (0x5e9ba0 in build 93847: FindUnit, then
// QuestCreateItem(game, player, 'rin ', ilvl, quality 6, drop 1)) whose tail returns QuestCreateItem's result, so the
// wrapped execute returns the ring. The signature pins the whole function, that tail included.
// No SDK service reports a quest reward or a newly made item, so the grant itself is wrapped.
#include "ormus_ring.h"

#include "corruption_roll.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig_hook.h"

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int ClosureGuid = 0x08;  // the execute's closure: +8 player guid, +0x10 game
constexpr int ClosureGame = 0x10;
constexpr int ItemModeStored = 0;

constexpr char RingPattern[] =
    "40 53 48 83 ec 30 44 8b 41 08 48 8b d9 48 8b 49 10 33 d2 e8 ?? ?? ?? ?? 48 85 c0 74 26 44 8b 4b 18 "
    "41 b8 72 69 6e 20 48 8b 4b 10 48 8b d0 c7 44 24 28 01 00 00 00 c7 44 24 20 06 00 00 00 e8 ?? ?? ?? ?? "
    "48 83 c4 30 5b c3";
constexpr uint8_t RingEntry[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x30};

using RingFn = Unit* (*)(void* closure, void* arg) noexcept;
using FindUnitFn = Unit* (*)(Game* game, int32_t type, uint32_t id) noexcept;

RingFn g_original = nullptr;
std::atomic<uint32_t> g_rings {0};

auto OrmusRing(void* closure, void* arg) noexcept -> Unit* {
	Unit* ring = g_original(closure, arg);
	auto* game = closure != nullptr ? At<Game*>(closure, ClosureGame) : nullptr;
	if (ring == nullptr || game == nullptr || !CorruptItem(game, ring, false, "ormus ring")) {
		return ring;
	}
	g_rings.fetch_add(1, std::memory_order_relaxed);
	// The roll changed the server's ring only: resend it to its owner as the Desecration brick does (item flag 1).
	// shortcut: a ring dropped on the ground (full inventory) is not resent; its client copy shows no corruption until
	// it is picked up again.
	using namespace d2r::rva;
	Unit* player = d2r::Engine<FindUnitFn>(d2r::sig::FindUnit)(game, 0, At<uint32_t>(closure, ClosureGuid));
	if (player != nullptr && At<int32_t>(ring, UnitMode) == ItemModeStored) {
		d2r::Engine<void (*)(Game*, Unit*, Unit*, uint32_t, uint32_t, int32_t, uint8_t, uint8_t, uint32_t) noexcept>(SendItem)(
		    game, player, ring, 1, 0, ItemModeStored, d2r::Engine<uint8_t (*)(Unit*) noexcept>(d2r::sig::ItemPage)(ring),
		    d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemStorePage)(ring),
		    d2r::Engine<uint32_t (*)(Unit*) noexcept>(d2r::sig::UnitPackedXY)(ring));
	}
	return ring;
}

}  // namespace

auto InstallOrmusRing(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	const bool on = d2r::sig::FindUnit.state == sigscan::State::Ok && d2r::sig::ItemPage.state == sigscan::State::Ok &&
	                d2r::sig::UnitPackedXY.state == sigscan::State::Ok &&
	                sigscan::HookBySignature(context, "OrmusRingExecute", RingPattern, 0, RingEntry, sizeof(RingEntry), OrmusRing,
	                                         &g_original);
	std::snprintf(report, reportSize, "ormus ring hook%s", on ? "" : " FAILED (signature)");
	return on ? 1 : 0;
}

auto OrmusRingCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "ormus ring: %u corrupted", g_rings.load());
}
