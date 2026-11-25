// ally_buff
#include "common.h"
#include "registry.h"

#include "ai_registry.h"
#include "../item_rules.h"
#include "line_spread.h"
#include "log.h"
#include "../missile_functions.h"
#include "parry_roll.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"
#include "skill_router.h"
#include "../skill_functions.h"
#include "../core-events/arena_spots.h"

#include <D2RLPlugin/mutations.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <map>
#include <mutex>
#include <span>

using namespace pd2r::skills;

namespace pd2r::skills {

// --- Frozen Armor and Enchant on allies (PD2 srvdofunc 25, #1870) ----------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 25 at 0x103aec40 (installed 0x103aecb0), its per-unit callback 0x1036c920.
// D2R's srvdo 25 (0x553650) puts aurastate on one unit (the skill's target, else the caster). PD2's builds
// {skill, level, auralencalc, auratargetstate, aurastat1-6, their aurastatcalc values, divisor 2}, runs the callback
// on the caster, then on every other unit within aurarangecalc that aurafilter takes (D2Game's area search 0xa0c70,
// D2R's AreaUnits, which skips the caster; flag 1). The callback halves each value (a tenth for enchant/coldenchant on
// a revive) for another player, a hireling or a unit whose flags +0xc4 (D2R +0x124) have the top bit; the caster and
// its other pets get the full value. The row's own tooltip says allies get half.
std::atomic<uint32_t> g_allyBuffs {0};

}  // namespace pd2r::skills

namespace {

SrvDoFn g_srvDo25 = nullptr;  // the slot's own function (D2R's srvdo 25), for rows with no aurafilter


struct AllyBuff {
	int32_t skillId, level, frames, state;
	StatValue stats[AuraStats] {};
	size_t count = 0;
	int32_t units = 0, divided = 0;  // for the log
};

// PD2's divisor rule in 0x1036c920: `divide` is another player, a hireling or a top-bit flagged unit.

// PD2's divisor rule in 0x1036c920: `divide` is another player, a hireling or a top-bit flagged unit.
constexpr auto AllyBuffValue(int32_t value, bool divide, bool enchantOnRevive) noexcept -> int32_t {
	return divide ? value / (enchantOnRevive ? 10 : 2) : value;
}

static_assert(AllyBuffValue(41, true, false) == 20);

static_assert(AllyBuffValue(41, true, true) == 4);

static_assert(AllyBuffValue(41, false, true) == 41);

// One unit (PD2's 0x1036c920 with srvdo 25's block; its slowmissiles branch never runs for these states).

// One unit (PD2's 0x1036c920 with srvdo 25's block; its slowmissiles branch never runs for these states).
auto AllyBuffOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	using namespace d2r::rva;
	auto* buff = static_cast<AllyBuff*>(area->arg);
	auto* game = static_cast<Game*>(area->game);
	if (victim == nullptr || game == nullptr || buff->frames < 1) {
		return 0;
	}
	const int32_t type = At<int32_t>(victim, UnitType);
	const bool hireling = type == UnitMonster && std::find(std::begin(HirelingClasses), std::end(HirelingClasses),
	                                                       At<int32_t>(victim, UnitClassId)) != std::end(HirelingClasses);
	const bool divide = (type == 0 && victim != area->unit) || At<int32_t>(victim, UnitMainFlags) < 0 || hireling;
	const bool enchantOnRevive =
		(buff->state == Enchant || buff->state == ColdEnchant) &&
		d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(victim, StateRevive) != 0;
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	StatValue stats[AuraStats] {};
	for (size_t i = 0; i < buff->count; i++) {
		const StatValue& s = buff->stats[i];
		const int32_t value = ImmuneHalved(s.value, s.stat, type != 0 && !hireling, unitStat(victim, s.stat, 0));
		stats[i] = {s.stat, AllyBuffValue(value, divide, enchantOnRevive)};
	}
	TimedStatList(d2r::DataFlag(game), area->unit, victim, buff->skillId, buff->level, buff->frames, buff->state, stats,
	              stats + buff->count);
	++buff->units;
	buff->divided += divide ? 1 : 0;
	return 1;
}

// PD2 srvdofunc 25 for a row with an aurafilter (Frozen Armor, Enchant); D2R's own for the others (D2R's Cold and
// Lightning Enchant rows, which PD2 does not have). 0 when aurastat1 is not a stat, as PD2.

// PD2 srvdofunc 25 for a row with an aurafilter (Frozen Armor, Enchant); D2R's own for the others (D2R's Cold and
// Lightning Enchant rows, which PD2 does not have). 0 when aurastat1 is not a stat, as PD2.
auto SrvDoAllyBuff(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr) {
		return 0;
	}
	const auto mask = At<uint32_t>(skill, SkAuraFilter);
	if (mask == 0) {
		return g_srvDo25(game, unit, skillId, level);
	}
	const auto calc = [&](int field) { return d2r::EvalCalc(game, unit, skill, field, skillId, level); };
	AllyBuff buff {skillId, level, calc(SkAuraLenCalc), At<int16_t>(skill, SkAuraTargetState)};
	for (int i = 0; i < static_cast<int>(AuraStats); i++) {
		const int32_t stat = At<int16_t>(skill, SkAuraStat1 + i * 2);
		// shortcut: PD2 also drops a stat past itemstatcost's end; the merged rows name none. Upgrade if a row ever does.
		if (stat < 0) {
			if (i == 0) {
				return 0;
			}
			continue;
		}
		buff.stats[buff.count++] = {stat, calc(SkAuraStatCalc1 + i * 4)};
	}
	d2r::AreaContext self {game, unit, 1, &buff};
	AllyBuffOne(&self, unit);
	const int32_t radius = calc(SkAuraRangeCalc);
	if (radius > 0) {  // AreaUnits asserts on a radius under 1
		d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit), 0, 0};  // 0, 0: at the caster
		d2r::Engine<AreaFn>(AreaUnits)(game, unit, &center, radius, mask, AllyBuffOne, &buff, 1);
	}
	++g_allyBuffs;
	return 1;
}

// --- Holy Sword and Smite starts (PD2 srvstfunc 73, 75), Smite's crushing blow (srvdo 150) -------------------------
// Read from ProjectDiablo.dll: srvst 73 at 0x103b6990, srvst 75 at 0x103b6a20, event 16 at 0x103469a0
// (docs/research/skills/w19-holy-sword-smite.md).

}  // namespace

auto InstallAllyBuff(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	// srvdo 25: the table by signature, the slot's current function kept (expected value) for rows with no aurafilter
	constexpr int AllyBuffSlot = 25;
	bool allyBuff = false;
	if (d2r::sig::SrvDoTable.state == sigscan::State::Ok) {
		const uint64_t slotRva = d2r::sig::SrvDoTable.rva + static_cast<uint64_t>(AllyBuffSlot) * 8;
		uint64_t own = 0;
		std::memcpy(&own, reinterpret_cast<const void*>(context->exeBase + slotRva), sizeof(own));
		g_srvDo25 = reinterpret_cast<SrvDoFn>(own);
		allyBuff = own != 0 && context->PatchWriteU64(slotRva, &own, sizeof(own),
		                                              reinterpret_cast<uint64_t>(static_cast<SrvDoFn>(SrvDoAllyBuff)));
	}
	used = std::strlen(report);
	if (used < reportSize) {
		const int n = std::snprintf(report + used, reportSize - used, "; srvdofunc %d ally buff%s", AllyBuffSlot,
		                            allyBuff ? "" : " FAILED (table signature or slot)");
		used += n > 0 ? static_cast<size_t>(n) : 0;
	}
	return allyBuff;
}

PD2R_RULE(InstallAllyBuff, -90)

