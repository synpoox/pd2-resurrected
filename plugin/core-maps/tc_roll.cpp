// The odds-only TC roll split out of key_drops.cpp (#943), unchanged (tc_roll.h).
#include "tc_roll.h"

#include "log.h"

namespace {

using d2r::At;

static_assert(Odds(2250, 10, 1) == 22500);      // Jawbone, Hell
static_assert(Odds(1000, 1000, 1) == 1000000);  // Demonic Cube, Hell
static_assert(Odds(200, 0, 1) == 200);          // Worldstone Shard
static_assert(Odds(127, 1, 2) == 125);
static_assert(Odds(127, 1, 9) == 127);
static_assert(Odds(0, 10, 1) == 0);

}  // namespace

std::atomic<uint32_t> g_tcMissing {0};

auto TcOdds(uint8_t dataFlag, const char* name, int32_t players) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint8_t* tables = d2r::Engine<const uint8_t* (*)(uint8_t) noexcept>(DataTablesOf)(dataFlag);
	if (tables == nullptr) {
		return 0;
	}
	const int32_t index = d2r::Engine<int32_t (*)(void*, const char*, int32_t) noexcept>(TcIndexByName)(
		At<void*>(tables, TablesTcNames), name, 0);
	const uint8_t* tc =
		index > 0 ? d2r::Engine<const uint8_t* (*)(uint8_t, uint16_t, int32_t) noexcept>(TcRecord)(
						dataFlag, static_cast<uint16_t>(index), 0)
				  : nullptr;
	if (tc == nullptr) {
		if (plog::Sample(g_tcMissing)) {
			plog::Warn("key drops: no TC \"%s\" (%u so far)", name, g_tcMissing.load());
		}
		return 0;
	}
	return Odds(At<uint16_t>(tc, TcUnique), At<uint16_t>(tc, TcSet), players);
}
