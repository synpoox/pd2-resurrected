// A native table write: no PluginSDK service runs or replaces a monster's AI (data_tables only reads monai.txt). D2R
// calls a monster's think from its monai row in rva::AiTable; a registered row's slot gets one of the trampolines
// below, which hands registered classes to their port and every other monster to the row's D2R think.
#include "ai_hooks.h"

#include "log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <utility>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int MaxRows = 10, MaxClasses = 8;
constexpr int MonAiRows = 155;  // D2R's monai.txt

struct Row {
	int32_t row = -1;
	AiThinkFn vanilla = nullptr;
	int32_t classes[MaxClasses] {};
	AiThinkFn thinks[MaxClasses] {};
	int count = 0;
};
// Filled at install, before the game runs a think; read-only after
Row g_rows[MaxRows];

// The test aid's request (RequestBossLife): -1 for none
std::atomic<int32_t> g_lifePercent {-1};
std::atomic<int32_t> g_lifeClass {0};
// When it was made (steady clock, ms): a request no boss of its class took within LifeRequestMs is dropped, so it
// never lands in a later game
std::atomic<int64_t> g_lifeAt {0};
constexpr int64_t LifeRequestMs = 10000;

auto NowMs() noexcept -> int64_t {
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
		.count();
}

// Sets a ported boss's life on its own think, on the server thread, so the console thread never writes a unit
auto ApplyLifeRequest(Unit* unit, int32_t classId) noexcept -> void {
	const int32_t want = g_lifeClass.load();
	if (g_lifePercent.load() < 0 || want != classId) {
		return;
	}
	if (NowMs() - g_lifeAt.load() > LifeRequestMs) {
		g_lifePercent.store(-1);
		plog::Info("ai hooks: test aid: the request for class %d expired unused", want);
		return;
	}
	const int32_t percent = g_lifePercent.exchange(-1);
	if (percent < 0) {
		return;
	}
	const int64_t max = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::UnitMaxLife)(unit);
	const auto life = static_cast<int32_t>(std::max<int64_t>(256, max * percent / 100));
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::rva::SetUnitStat)(unit, d2r::rva::StatLife,
	                                                                                      life, 0);
	plog::Info("ai hooks: test aid: class %d unit %u life set to %d%% (%d of %lld)", classId,
	           At<uint32_t>(unit, d2r::rva::UnitIdField), percent, life, static_cast<long long>(max));
}

template <int K>
void Trampoline(Game* game, Unit* unit, uint8_t* tick) noexcept {
	const Row& r = g_rows[K];
	if (unit != nullptr && At<int32_t>(unit, d2r::rva::UnitType) == d2r::rva::UnitMonster) {
		const int32_t classId = At<int32_t>(unit, d2r::rva::UnitClassId);
		for (int i = 0; i < r.count; ++i) {
			if (r.classes[i] == classId) {
				ApplyLifeRequest(unit, classId);
				r.thinks[i](game, unit, tick);
				return;
			}
		}
	}
	r.vanilla(game, unit, tick);
}

template <int... K>
constexpr auto MakeTrampolines(std::integer_sequence<int, K...>) noexcept {
	return std::array<AiThinkFn, sizeof...(K)> {&Trampoline<K>...};
}
constexpr auto Trampolines = MakeTrampolines(std::make_integer_sequence<int, MaxRows> {});

auto Slot(int32_t row) noexcept -> uint64_t {
	return d2r::rva::AiTable + static_cast<uint64_t>(row) * d2r::rva::AiRowSize;
}

}  // namespace

auto RegisterAiThink(const D2RL::PluginContext* context, int32_t monaiRow, uint64_t d2rThink, int32_t classId,
                     AiThinkFn think) noexcept -> bool {
	if (monaiRow < 0 || monaiRow >= MonAiRows || think == nullptr) {
		return false;
	}
	int k = 0;
	while (k < MaxRows && g_rows[k].row != monaiRow && g_rows[k].row >= 0) {
		++k;
	}
	if (k == MaxRows) {
		plog::Info("ai hooks: no free row for monai %d", monaiRow);
		return false;
	}
	Row& r = g_rows[k];
	if (r.row < 0) {
		// D2R's think is the expected value: a slot another plugin changed refuses the write
		const uint64_t vanilla = d2r::imageBase + d2rThink;
		uint8_t expected[8];
		std::memcpy(expected, &vanilla, sizeof(expected));
		if (!context->PatchWriteU64(Slot(monaiRow), expected, sizeof(expected), reinterpret_cast<uint64_t>(Trampolines[k]))) {
			plog::Warn("ai hooks: monai %d think not swapped (the slot is not D2R's think)", monaiRow);
			return false;
		}
		r.row = monaiRow;
		r.vanilla = reinterpret_cast<AiThinkFn>(vanilla);
	}
	if (r.count == MaxClasses) {
		return false;
	}
	r.classes[r.count] = classId;
	r.thinks[r.count] = think;
	++r.count;
	plog::Info("ai hooks: monai %d class %d -> port", monaiRow, classId);
	return true;
}

auto RequestBossLife(int32_t percent, int32_t classId) noexcept -> void {
	g_lifeClass.store(classId);
	g_lifeAt.store(NowMs());
	g_lifePercent.store(std::clamp(percent, 0, 100));
}

auto VanillaAiThink(int32_t monaiRow) noexcept -> AiThinkFn {
	for (const Row& r : g_rows) {
		if (r.row == monaiRow) {
			return r.vanilla;
		}
	}
	return nullptr;
}

namespace {

constexpr uint32_t IdleWindowKey = 0x55424957;  // 'UBIW': PD2's game+0x26fe
constexpr int32_t IdleWindowTicks = 15;
// The bosses marked alone, not yet seen by their think. The hook runs on the game's server thread, as the thinks do;
// a full list overwrites its oldest mark (a boss whose think never runs, as one with no port, leaves its mark there)
std::atomic<d2r::Unit*> g_alone[16] {};
std::atomic<uint32_t> g_aloneNext {0};
std::atomic<uint32_t> g_windows {0};

}  // namespace

auto MarkBossAlone(Unit* boss) noexcept -> void {
	for (const auto& slot : g_alone) {
		if (slot.load() == boss) {
			return;
		}
	}
	g_alone[g_aloneNext++ % std::size(g_alone)].store(boss);
}

auto IdleWindow(Game* game, Unit* boss, bool playerInLevel) noexcept -> bool {
	uint32_t key = IdleWindowKey;
	int32_t& window = *d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
	for (auto& slot : g_alone) {
		Unit* marked = boss;
		if (boss != nullptr && slot.compare_exchange_strong(marked, nullptr) && window == 0) {
			window = IdleWindowTicks;
			plog::Info("ai hooks: boss %d left alone: idle window of %d ticks (%u so far)",
			           At<int32_t>(boss, d2r::rva::UnitClassId), window, ++g_windows);
		}
	}
	if (window <= 0) {
		return false;
	}
	if (boss != nullptr && playerInLevel && --window == 0) {
		plog::Info("ai hooks: boss %d idle window over", At<int32_t>(boss, d2r::rva::UnitClassId));
	}
	return true;
}

auto ImmunityStates(d2r::Game* game, d2r::Unit* unit, bool on, bool setState) noexcept -> int {
	using namespace d2r::rva;
	constexpr int32_t StateResistAll = 8;
	constexpr int32_t StripStates[] = {2, 62};  // poison, openwounds
	if (setState) {
		// A bare state: SetStateBit is the bit UnitHasState reads (Rathma's shared life skips a resist_all partner),
		// SetStateGfxFlag after it on 1, as D2R's 0x56bfe0 (#623)
		d2r::Engine<void (*)(d2r::Unit*, int32_t, int32_t) noexcept>(SetStateBit)(unit, StateResistAll, on ? 1 : 0);
		if (on) {
			d2r::Engine<void (*)(d2r::Unit*, int32_t, int32_t) noexcept>(SetStateGfxFlag)(unit, StateResistAll, 1);
		}
	}
	int stripped = 0;
	if (!on) {
		return stripped;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	for (const int32_t s : StripStates) {
		for (int k = 0; k < 20; ++k) {
			void* list = d2r::Engine<void* (*)(d2r::Unit*, int32_t) noexcept>(UnitStateList)(unit, s);
			if (list == nullptr) {
				break;
			}
			d2r::Engine<void (*)(d2r::Unit*, int32_t, int32_t) noexcept>(SetStateBit)(unit, s, 0);
			d2r::Engine<void (*)(d2r::Unit*, void*) noexcept>(DetachStatList)(unit, list);
			d2r::Engine<void (*)(uint32_t, void*) noexcept>(FreeStatList)(dataFlag, list);
			++stripped;
		}
	}
	return stripped;
}
