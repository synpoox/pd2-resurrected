// The Uber Tristram primes summon their minions (UB3, #483). PD2 fights the primes with the plain act-boss AIs and
// writes a wrapper for each into D2Game's AI table (ProjectDiablo.dll 0x10361150): Mephisto 0x10357ba0, Diablo
// 0x10357d50, BaalCrab 0x10357f00. Each runs on the AI tick, rolls for one minion near the prime, then calls the
// original think. The same AI rows run the act bosses, so a wrapper acts only for its prime's class.
// No PluginSDK service runs code on a monster's AI tick or spawns a monster, so the three AI table slots are written
// (PatchWriteU64, D2R's think as the expected value), and the spawn is the engine's, as map_events.cpp does it.
#include "prime_minions.h"

#include "log.h"

#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

using ThinkFn = void (*)(Game* game, Unit* monster, uint8_t* tick) noexcept;

// PD2's prime ids (the port keeps vanilla's monstats rows at their ids)
constexpr int32_t UberMephisto = 704, UberDiablo = 705, UberBaal = 709;
// PD2's sets 0x105ddebc, 0x105dde74 and 0x105ddfa8 (filled at start by 0x10167bd0, 0x10167c40, 0x10167c90)
constexpr int32_t MephistoMinions[] = {725, 726, 727, 728, 729, 730};  // skeleton8, sk_archer11, skmage_*
constexpr int32_t DiabloMinions[] = {712};                             // megademon6
constexpr int32_t BaalMinions[] = {731, 732};                          // vampire9, wraith9
constexpr int32_t ModeDead = 12;
constexpr int32_t ModeSkill1 = 8;  // PD2's spawn mode

struct Prime {
	int32_t classId;
	const int32_t* minions;
	size_t count;
	bool needsTarget;
	int32_t cap;  // spawns while fewer of its minions live
	ThinkFn original;
};

Prime g_primes[] = {
	{UberMephisto, MephistoMinions, std::size(MephistoMinions), true, 40, nullptr},
	{UberDiablo, DiabloMinions, std::size(DiabloMinions), true, 15, nullptr},
	{UberBaal, BaalMinions, std::size(BaalMinions), false, 20, nullptr},
};

std::atomic<uint32_t> g_ticks {0};
std::atomic<uint32_t> g_spawned {0};
std::atomic<uint32_t> g_failed {0};

// shortcut: PD2 rolls the chance with 0x1036f320 (not read) and the pick with the CRT's rand(); both use the CRT's
// rand here, seeded once per thread as map_events.cpp does
auto Rand() noexcept -> int32_t {
	static thread_local const bool seeded = (std::srand(static_cast<unsigned>(__rdtsc())), true);
	(void)seeded;
	return std::rand();
}

// PD2 0x10357a20: the prime's minions that are not dead, in its room and the rooms by it.
// shortcut: PD2 also removes the corpses of dead non-primes there (0x1036d1e0); add it if corpses pile up
auto LiveMinions(const Prime& prime, Unit* boss) noexcept -> int32_t {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(boss);
	void** rooms = nullptr;
	uint32_t count = 0;
	if (room != nullptr) {
		d2r::Engine<void (*)(void*, void***, uint32_t*) noexcept>(NearRooms)(room, &rooms, &count);
	}
	int32_t live = 0;
	for (uint32_t r = 0; rooms != nullptr && r < count; ++r) {
		for (auto* unit = d2r::Engine<Unit* (*)(void*) noexcept>(RoomFirstUnit)(rooms[r]); unit != nullptr;
		     unit = d2r::Engine<Unit* (*)(Unit*) noexcept>(NextUnitInRoom)(unit)) {
			if (unit != boss && At<int32_t>(unit, UnitType) == UnitMonster && At<int32_t>(unit, UnitMode) != ModeDead &&
			    std::find(prime.minions, prime.minions + prime.count, At<int32_t>(unit, UnitClassId)) !=
			        prime.minions + prime.count) {
				++live;
			}
		}
	}
	return live;
}

// The spawn half of PD2's wrappers: a random spot within r of the prime, r = clamp(aidist - target distance, 3, 20),
// a free spot by it, then the minion in mode 8 with no treasure and no experience.
// shortcut: PD2 then calls 0x10339ad0(minion, prime), an import PD2 resolves by ordinal (not read yet; likely ties the
// minion to the prime); read it if the minions act unlike PD2's
auto Summon(Game* game, Unit* boss, const Prime& prime, const uint8_t* tick) noexcept -> void {
	using namespace d2r::rva;
	const int32_t id = prime.minions[static_cast<uint32_t>(Rand()) % prime.count];
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
		d2r::DataFlag(game), prime.classId);
	const int32_t aidist = record != nullptr ? At<uint8_t>(record, MonAiDist + At<uint8_t>(game, GameDifficulty)) : 0;
	const int32_t r = std::clamp(aidist - At<int32_t>(tick, AiTickDistance), 3, 20);
	int32_t xy[2];
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(boss, xy);
	xy[0] += static_cast<int32_t>(static_cast<uint32_t>(Rand()) % (2 * r)) - r;
	xy[1] += static_cast<int32_t>(static_cast<uint32_t>(Rand()) % (2 * r)) - r;
	void* found = nullptr;
	d2r::Engine<void (*)(Game*, void*, int32_t*, uint32_t, uint32_t, void**, uint64_t, int32_t) noexcept>(FindFreeSpot)(
		game, d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(boss), xy, 3, 0x400, &found, 5, 100);
	Unit* minion = found != nullptr
		? d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int16_t) noexcept>(
			  SpawnMonster)(game, found, xy[0], xy[1], id, ModeSkill1, -1, 0)
		: nullptr;
	if (minion == nullptr) {
		if (plog::Sample(g_failed)) {
			plog::Warn("primes: no minion %d by prime %d (%u failed)", id, prime.classId, g_failed.load());
		}
		return;
	}
	d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(SetUnitFlags)(minion, UnitNoTreasure | UnitNoExperience, 1);
	if (plog::Sample(g_spawned)) {
		plog::Info("primes: prime %d summoned %d at (%d, %d), r %d (%u so far)", prime.classId, id, xy[0], xy[1], r,
		           g_spawned.load());
	}
}

template <size_t I>
auto Think(Game* game, Unit* monster, uint8_t* tick) noexcept -> void {
	using namespace d2r::rva;
	const Prime& prime = g_primes[I];
	if (game != nullptr && monster != nullptr && tick != nullptr && At<int32_t>(monster, UnitClassId) == prime.classId &&
	    (!prime.needsTarget || At<void*>(tick, AiTickTarget) != nullptr)) {
		++g_ticks;
		const int32_t live = LiveMinions(prime, monster);
		const int32_t roll = Rand() % 100;
		// Mephisto: (60 - n) x 2 percent; Diablo 30, Baal 20
		const int32_t chance = I == 0 ? (60 - live) * 2 : I == 1 ? 30 : 20;
		if (live < prime.cap && roll < chance) {
			Summon(game, monster, prime, tick);
		}
	}
	prime.original(game, monster, tick);
}

constexpr ThinkFn Wrappers[] = {Think<0>, Think<1>, Think<2>};
constexpr int32_t AiRows[] = {50, 51, 135};
constexpr uint64_t D2RThinks[] = {d2r::rva::AiMephisto, d2r::rva::AiDiablo, d2r::rva::AiBaalCrab};
static_assert(std::size(Wrappers) == std::size(g_primes) && std::size(AiRows) == std::size(g_primes));

}  // namespace

auto InstallPrimeMinions(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	int installed = 0;
	for (size_t i = 0; i < std::size(g_primes); ++i) {
		const uint64_t d2rThink = d2r::imageBase + D2RThinks[i];
		uint8_t expected[8];
		std::memcpy(expected, &d2rThink, sizeof(expected));
		g_primes[i].original = reinterpret_cast<ThinkFn>(d2rThink);
		installed += context->PatchWriteU64(AiTable + static_cast<uint64_t>(AiRows[i]) * AiRowSize, expected,
		                                    sizeof(expected), reinterpret_cast<uint64_t>(Wrappers[i]))
			? 1
			: 0;
	}
	std::snprintf(report, reportSize, "prime minions on AI rows 50, 51, 135%s",
	              installed == static_cast<int>(std::size(g_primes)) ? "" : " FAILED (a slot is not D2R's think)");
	return installed == static_cast<int>(std::size(g_primes)) ? 1 : 0;
}

auto PrimeMinionsCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "primes: %u summon ticks, %u minions summoned, %u failed", g_ticks.load(),
	              g_spawned.load(), g_failed.load());
}
