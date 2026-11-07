// A native table write: no PluginSDK service runs or replaces a monster's AI (data_tables only reads monai.txt). D2R
// calls a monster's think from its monai row in rva::AiTable; a registered row's slot gets one of the trampolines
// below, which hands registered classes to their port and every other monster to the row's D2R think.
#include "ai_hooks.h"

#include "log.h"

#include <array>
#include <cstring>
#include <utility>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int MaxRows = 8, MaxClasses = 8;
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

template <int K>
void Trampoline(Game* game, Unit* unit, uint8_t* tick) noexcept {
	const Row& r = g_rows[K];
	if (unit != nullptr && At<int32_t>(unit, d2r::rva::UnitType) == d2r::rva::UnitMonster) {
		const int32_t classId = At<int32_t>(unit, d2r::rva::UnitClassId);
		for (int i = 0; i < r.count; ++i) {
			if (r.classes[i] == classId) {
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

auto RegisterAiThink(const D2RL::PluginContext* context, int32_t monaiRow, int32_t classId, AiThinkFn think) noexcept
	-> bool {
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
		// The bound image is the one rvas_*.h describes (d2r::Bind), so the slot holds D2R's think: it is the expected
		// value, and a slot another plugin changed refuses the write
		const uint64_t current = At<uint64_t>(reinterpret_cast<const void*>(d2r::imageBase + Slot(monaiRow)), 0);
		uint8_t expected[8];
		std::memcpy(expected, &current, sizeof(expected));
		if (current == 0 ||
		    !context->PatchWriteU64(Slot(monaiRow), expected, sizeof(expected), reinterpret_cast<uint64_t>(Trampolines[k]))) {
			plog::Info("ai hooks: monai %d think not swapped", monaiRow);
			return false;
		}
		r.row = monaiRow;
		r.vanilla = reinterpret_cast<AiThinkFn>(current);
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

auto VanillaAiThink(int32_t monaiRow) noexcept -> AiThinkFn {
	for (const Row& r : g_rows) {
		if (r.row == monaiRow) {
			return r.vanilla;
		}
	}
	return nullptr;
}
