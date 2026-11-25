// fend_splash
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

namespace {

// Fend, Zeal and Fury splash radius (#1936). PD2's srvst 37 (ProjectDiablo.dll 0x103b6310 in PD2.gpr, +0x10 installed)
// keeps or makes the attacker's list on the row's aurastate (temp_skill_buff) and SETS its aurastats from aurastatcalc
// (1.13c D2Game +0x448e0, D2Common #10261 is a set) on every start: inc_splash_radius = 1. Its srvdo 13 hit
// (0x10303a50, installed 0x10303a60, over 1.13c's melee hit call +0x4dc46 to +0xded30) then adds 20 to that list, when
// it has one, before the hit: 3 hits run with 21, 41, 61 (+1, +2, +3 radius), and the 61 stays until the next srvst 37,
// srvst 78 or srvdo 163. D2R's srvst 37 (93847 0x5582a0) and srvdo 13 (0x5568e0) have neither, so both slots are
// wrapped: the slot's own function is kept and called (no SDK service runs a skill's start or do function).
// shortcut: PD2's list never expires; a new one here lasts SplashFrames (10 minutes, as Frenzy's), and PD2 refuses
// the start with no target before its list step, where this sets the stat anyway. Upgrade if a later Charge or
// splash must see a leftover older than 10 minutes.
constexpr int32_t SrvStMeleeSplashSlot = 37, SrvDoMeleeSplashSlot = 13;

constexpr int32_t MeleeSplashRadius = 20;

SrvDoFn g_srvSt37 = nullptr, g_srvDo13 = nullptr;

// The merged data has temp_skill_buff (false: the rule is off)

// The merged data has temp_skill_buff (false: the rule is off)
auto MeleeSplashTables(Game* game) noexcept -> bool {
	const auto* tables =
		game != nullptr ? d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(d2r::rva::DataTables)(d2r::DataFlag(game))
		                : nullptr;
	return tables != nullptr && At<uint64_t>(tables, d2r::rva::TablesStateCount) > TempSkillBuff;
}


auto SrvStMeleeSplash(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const int64_t ran = g_srvSt37(game, unit, skillId, level);
	if (ran == 0 || unit == nullptr || !MeleeSplashTables(game)) {
		return ran;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr || At<int16_t>(skill, SkAuraState) != TempSkillBuff || At<int16_t>(skill, SkAuraStat1) < 0) {
		return ran;  // the three rows' aurastate; other srvst 37 rows have none
	}
	const int32_t stat = At<int16_t>(skill, SkAuraStat1);
	const int32_t value = d2r::EvalCalc(game, unit, skill, SkAuraStatCalc1, skillId, level);
	void* list = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, TempSkillBuff);
	const char* made = "kept";
	if (list != nullptr) {
		d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat)(dataFlag, list, stat,
		                                                                                          value, 0);
	} else {
		made = TimedStatList(dataFlag, unit, unit, skillId, level, SplashFrames, TempSkillBuff, {{stat, value}});
	}
	return ran;
}


auto SrvDoMeleeSplash(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	void* list = unit != nullptr && MeleeSplashTables(game)
	                 ? d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, TempSkillBuff)
	                 : nullptr;
	if (list != nullptr) {
		const uint32_t dataFlag = d2r::DataFlag(game);
		const auto listStat = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(ListStatValue);
		const int32_t value = listStat(dataFlag, list, IncSplashRadius, 0) + MeleeSplashRadius;
		d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat)(dataFlag, list,
		                                                                                          IncSplashRadius, value, 0);
	}
	return g_srvDo13(game, unit, skillId, level);
}

// --- Revive's aura event (PD2 auraeventfunc 32, ProjectDiablo.dll 0x10347370) ---------------------------------------
// Revive's row has auraevent1 domeleedamage: each melee hit of a revived monster runs proc_SplashDamage's do function
// (PD2 358, merged 430) at level 1 from it, called directly as PD2 does. PD2's splash finds the struck unit on the
// revive's path; this passes the unit the hit landed on.

}  // namespace

auto InstallFendSplash(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	// srvst 37 and srvdo 13 (Fend, Zeal, Fury): the tables by signature, each slot's current function kept and called.
	// The start first: alone it only sets the stat to 1 each start, which is inert without the hits' 20.
	const auto wrapSlot = [context](const sigscan::Entry& table, int slot, SrvDoFn& own, SrvDoFn function) {
		if (table.state != sigscan::State::Ok) {
			return false;
		}
		const uint64_t slotRva = table.rva + static_cast<uint64_t>(slot) * 8;
		uint64_t current = 0;
		std::memcpy(&current, reinterpret_cast<const void*>(context->exeBase + slotRva), sizeof(current));
		own = reinterpret_cast<SrvDoFn>(current);
		return current != 0 &&
		       context->PatchWriteU64(slotRva, &current, sizeof(current), reinterpret_cast<uint64_t>(function));
	};
	const bool meleeSplash =
		wrapSlot(d2r::sig::SrvStTable, SrvStMeleeSplashSlot, g_srvSt37, SrvStMeleeSplash) &&
		wrapSlot(d2r::sig::SrvDoTable, SrvDoMeleeSplashSlot, g_srvDo13, SrvDoMeleeSplash);
	plog::Info("fend/zeal/fury: PD2 splash radius 1 per start, +20 per hit %s",
	           meleeSplash ? "on" : "FAILED (table signature or slot)");
	return meleeSplash;
}

PD2R_RULE(InstallFendSplash, -80)

