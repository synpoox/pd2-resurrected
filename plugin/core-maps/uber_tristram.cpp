// The Uber Tristram primes' drop (ProjectDiablo.dll 0x10377710; docs/research/pd2-uber-bosses-dll.md, section 6): each
// death of Uber Mephisto, Diablo or Baal sets a bit in the game; once all three are set PD2 drops the Hellfire Torch
// and the Black Soulstone at item level 130 and opens a permanent town portal (object 60) to Harrogath at the body.
// The port opens PD2's red portal (xactportal_red, operate 60) instead: D2R's town-portal operate (15) on object 60
// refuses a portal with no linked partner ("I can't"). PD2 sets no done bit; the port does, so a cheat or a second
// set of primes in one game drops once.
// PD2 replaces D2R's own drop of the same three deaths (the Torch and a Standard of Heroes per player), which a
// MutationService byte patch turns off. The drop rides key_drops' monster-death listener (the SDK's), as rathma.cpp.
#include "uber_tristram.h"

#include "log.h"
#include "map_open.h"

#include <D2RLPlugin/item.h>
#include <D2RLPlugin/mutations.h>

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// ubermephisto, uberdiablo, uberbaal: the same ids in PD2 and the port; each one's bit, as PD2 sets it
struct Prime {
	int32_t classId;
	int32_t bit;
};
constexpr Prime Primes[] = {{704, 0x10}, {705, 0x20}, {709, 0x40}};
constexpr int32_t AllDead = 0x70;
constexpr int32_t Dropped = 0x80;  // the port's, not PD2's: set once the drop is made
constexpr uint32_t DeadKey = 0x55425450;  // 'UBTP'
constexpr int32_t Harrogath = 109;
constexpr int32_t RedPortal = 587;  // xactportal_red (PD2 584)
constexpr int32_t QualityUnique = 7;
constexpr int32_t TorchRow = 400;  // Hellfire Torch, runtime uniqueitems row
constexpr uint32_t Torch = D2RL::Items::MakeItemCode("cm2");
constexpr uint32_t Soulstone = D2RL::Items::MakeItemCode("dcho");

std::atomic<bool> g_on {false};
std::atomic<uint32_t> g_kills {0};
std::atomic<uint32_t> g_portals {0};

auto BitOf(int32_t classId) noexcept -> int32_t {
	for (const Prime& p : Primes) {
		if (p.classId == classId) {
			return p.bit;
		}
	}
	return 0;
}

auto Counter(Game* game, uint32_t key) noexcept -> int32_t* {
	return d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

// shortcut: the item takes the monster's item level (Hell 120, the Torch's own level), where PD2 makes it at 130
auto Drop(Game* game, Unit* monster, uint32_t code, int32_t quality, int32_t uniqueRow) noexcept -> void {
	using namespace d2r::rva;
	const uint8_t dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const int32_t row = d2r::Engine<int32_t (*)(uint8_t, uint32_t) noexcept>(ItemRowByCode)(dataFlag, code);
	if (row < 0) {
		plog::Warn("tristram: no item \"%.4s\"", reinterpret_cast<const char*>(&code));
		return;
	}
	d2r::Engine<void* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t) noexcept>(DropItemAt)(
		game, monster, row, quality, uniqueRow + 1, 0);
	plog::Info("tristram: dropped %.4s", reinterpret_cast<const char*>(&code));
}

}  // namespace

auto InstallTristram(const D2RL::PluginContext* context, char* report, size_t size) noexcept -> void {
	using namespace d2r::rva;
	const D2RL::MutationService* mutations = nullptr;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize)) {
		std::snprintf(report, size, "FAILED (no mutation service)");
		return;
	}
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		std::snprintf(report, size, "FAILED (no transaction)");
		return;
	}
	const D2RL::Mutations::BytePatchRequest patch{.structSize = D2RL::Mutations::BytePatchRequestSize,
	                                              .rva = PrimesDrop,
	                                              .expected = PrimesDropD2R,
	                                              .expectedSize = sizeof(PrimesDropD2R),
	                                              .bytes = PrimesDropOff,
	                                              .size = sizeof(PrimesDropOff)};
	D2RL::Mutations::OperationHandle operation = 0;
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	if (mutations->stageBytePatch(context, transaction, &patch, &operation) != D2RL::Mutations::Result::Success ||
	    mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		std::snprintf(report, size, "FAILED (bytes differ)");
		return;
	}
	g_on = true;
	std::snprintf(report, size, "D2R's primes' drop off; PD2's Torch, Soulstone and portal on");
}

auto TristramDeath(Game* game, Unit* monster) noexcept -> void {
	const int32_t bit = BitOf(At<int32_t>(monster, d2r::rva::UnitClassId));
	int32_t* dead = bit != 0 && g_on ? Counter(game, DeadKey) : nullptr;
	if (dead == nullptr) {
		return;
	}
	*dead |= bit;
	if ((*dead & (AllDead | Dropped)) != AllDead) {
		return;
	}
	*dead |= Dropped;
	Drop(game, monster, Torch, QualityUnique, TorchRow);
	// PD2 asks quality 7 for it too; it has no unique row, so it comes out normal
	Drop(game, monster, Soulstone, d2r::rva::QualityNormal, -1);
	int32_t xy[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(d2r::rva::UnitCoords)(monster, xy);
	if (OpenPortalAt(game, d2r::Engine<void* (*)(Unit*) noexcept>(d2r::rva::UnitRoom)(monster), RedPortal, xy[0],
	                 xy[1], Harrogath) != nullptr) {
		++g_portals;
	}
	plog::Info("tristram: primes dead (%u kills, %u portals)", ++g_kills, g_portals.load());
}

auto TristramCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "tristram: %s, %u prime kills, %u portals", g_on ? "on" : "off", g_kills.load(),
	              g_portals.load());
}
