// Taunt affects every eligible unit around the caster (PD2 srvdo 71, #2221).
// PD2.gpr: 0x103b0080 -> area 0x103a1450 -> callback 0x1036f280; D2R 93847: 0x568ad0.
// No SDK service exposes the area cast or Taunt's AI steps. Reuse D2R's per-unit tests/curse/AI helpers,
// found by signatures, and replace only srvdo 71. The skill row supplies the filter, radius, duration and stats.
#include "pd2_engine.h"
#include "log.h"
#include "registry.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

sigscan::Entry CanTaunt {"TauntCanApply", "skills", sigscan::Kind::Code, 0, 0,
    "40 53 48 83 ec 30 48 8b ca 48 8b da e8 ?? ?? ?? ?? 83 f8 01 75 52 44 0f b6 c8"};
// Calls in D2R's Taunt: clear the AI target, then set the path target after requeuing AI event 2.
sigscan::Entry ClearAi {"TauntClearAi", "skills", sigscan::Kind::Rip, 6, 10,
    "33 d2 49 8b ce e8 ?? ?? ?? ?? 45 33 c9 49 8b d6 48 8b ce 45 8d 41 02"};
sigscan::Entry SetTarget {"TauntSetTarget", "skills", sigscan::Kind::Rip, 7, 11,
    "48 8b d3 49 8b ce e8 ?? ?? ?? ?? 8b c7 eb 02 33 c0"};

using TestFn = int32_t (*)(Game*, Unit*) noexcept;
using CurseFn = int64_t (*)(Unit*, d2r::CurseParams*) noexcept;
using TargetFn = void (*)(Unit*, Unit*) noexcept;
using RemoveEventFn = void (*)(Game*, Unit*, int32_t, int32_t) noexcept;
using AddEventFn = void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept;
using AreaFn = void (*)(Game*, Unit*, d2r::AreaCenter*, int32_t, uint32_t,
    int32_t (*)(d2r::AreaContext*, Unit*), void*, int32_t) noexcept;

auto TauntOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	if (victim == nullptr) {
		return 0;
	}
	auto* curse = static_cast<d2r::CurseParams*>(area->arg);
	if (!d2r::Engine<TestFn>(CanTaunt)(curse->game, victim) ||
	    !d2r::Engine<CurseFn>(d2r::sig::ApplyCurse)(victim, curse)) {
		return 0;
	}
	d2r::Engine<TargetFn>(ClearAi)(victim, nullptr);
	d2r::Engine<RemoveEventFn>(d2r::sig::RemoveUnitEvent)(curse->game, victim, 2, 0);
	d2r::Engine<AddEventFn>(d2r::sig::AddUnitEvent)(curse->game, victim, 2,
	    At<int32_t>(curse->game, d2r::rva::GameFrame) + 1, 0, 0, 0);
	d2r::Engine<TargetFn>(SetTarget)(victim, curse->caster);
	return 1;
}

auto SrvDoTaunt(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint8_t) noexcept>(d2r::sig::DataTables)(
	    static_cast<uint8_t>(d2r::DataFlag(game)));
	if (skill == nullptr || tables == nullptr) {
		return 0;
	}
	// ApplyCurse also reads two event/stat pairs at +0x60/+0x68; Taunt has none.
	struct {
		d2r::CurseParams curse;
		int32_t events[4];
	} block {};
	auto& curse = block.curse;
	curse.game = game;
	curse.caster = unit;
	curse.pad10[0] = 1;  // monster curse: native removal callback and AI state
	curse.skill = skillId;
	curse.level = level;
	curse.key = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::sig::UnitStateKey)(unit);
	curse.state = At<int16_t>(skill, SkAuraTargetState);
	if (curse.state < 0 || static_cast<uint64_t>(curse.state) >= At<uint64_t>(tables, TablesStateCount)) {
		return 0;
	}
	const auto calc = [&](int field) { return d2r::EvalCalc(game, unit, skill, field, skillId, level); };
	curse.frames = calc(SkAuraLenCalc);
	// DataTables +0x1260: itemstatcost count, as D2R's srvdo 71 validates aurastat1-6.
	for (int i = 0; i < 6; ++i) {
		const int32_t stat = At<int16_t>(skill, SkAuraStat1 + 2 * i);
		if (stat < 0 || static_cast<uint64_t>(stat) >= At<uint64_t>(tables, 0x1260)) {
			if (i == 0) {
				return 0;  // PD2 refuses a row without its first stat
			}
			curse.stats[i] = -1;
			curse.values[i] = -1;
		} else {
			curse.stats[i] = stat;
			curse.values[i] = calc(SkAuraStatCalc1 + 4 * i);
		}
	}
	const int32_t radius = calc(SkAuraRangeCalc);
	if (radius > 0) {  // native area search asserts below 1
		d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::UnitRoom)(unit), 0, 0};
		d2r::Engine<AreaFn>(d2r::sig::AreaUnits)(game, unit, &center, radius,
		    At<uint32_t>(skill, SkAuraFilter), TauntOne, &curse, 1);
	}
	return 1;
}

}  // namespace

auto InstallTaunt(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	sigscan::Entry* local[] = {&CanTaunt, &ClearAi, &SetTarget};
	sigscan::Resolve(local, std::size(local), context->exeBase, nullptr, 0);
	const sigscan::Entry* uses[] = {&CanTaunt, &ClearAi, &SetTarget, &d2r::sig::ApplyCurse,
	    &d2r::sig::RemoveUnitEvent, &d2r::sig::AddUnitEvent, &d2r::sig::AreaUnits,
	    &d2r::sig::UnitRoom, &d2r::sig::UnitStateKey, &d2r::sig::DataTables, &d2r::sig::SrvDoTable};
	bool on = d2r::imageBase == context->exeBase && d2r::api.getSkillRecord && d2r::api.evalSkillCalc &&
	    std::all_of(std::begin(uses), std::end(uses), [](const auto* e) { return e->state == sigscan::State::Ok; });
	if (on) {
		const uint64_t slot = d2r::sig::SrvDoTable.rva + 71 * sizeof(uint64_t);
		const uint64_t own = At<uint64_t>(reinterpret_cast<const void*>(context->exeBase + slot), 0);
		on = own != 0 && context->PatchWriteU64(slot, &own, sizeof(own), reinterpret_cast<uint64_t>(&SrvDoTaunt));
	}
	plog::Info("taunt: PD2 area cast %s", on ? "on" : "FAILED (image, signatures or slot)");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; taunt area%s", on ? "" : " FAILED");
	}
	return on;
}

PD2R_RULE(InstallTaunt, 420)
