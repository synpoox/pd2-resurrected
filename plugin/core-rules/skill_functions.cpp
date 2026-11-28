// PD2 skill functions (skills.txt srvdofunc / cltdofunc / srvstfunc / cltstfunc) in D2R's free table slots, and the
// install of those and of the missile functions (missile_functions.cpp).
//
// The server calls slot `srvdofunc` of rva::SrvDoTable at the cast frame as
//     int64_t fn(Game* game, Unit* caster, int32_t skillId, int32_t skillLevel)   -> non-zero = the skill ran.
// The client calls slot `cltdofunc` of rva::CltDoTable as
//     int32_t fn(Unit* caster, int32_t skillId, int32_t skillLevel)               -> non-zero plays the do sound.
// Start functions (`srvstfunc` in rva::SrvStTable, `cltstfunc` in rva::CltStTable) take the same arguments at the
// start frame; zero refuses the cast. srvdofunc 190 is a router: one slot shared by several PD2 do functions, picked
// by skill id.
// PD2's channel take-over is an inline hook on D2R's mode gate for a busy player (BusyModeGate), which also logs the
// Run/Concentration answers W19's game checks read. Each slot is written with PatchWriteU64 and an expected value of
// eight zero bytes (D2R's own function for the slots it wraps: aura events 32 and 16, srvdo 58), so a slot D2R or
// another plugin already uses is refused and logged, never overwritten. Slots keep PD2's own numbers where free (docs/features/core-rules.md).
#include "skill_functions.h"

#include "install_report.h"
#include "log.h"
#include "missile_functions.h"
#include "skill_router.h"
#include "skill/common.h"
#include "skill/registry.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace pd2r::skills;

namespace {
int g_skillTotal = 0;
pd2r::installs::Report g_installs;  // the session's results: `core-rules installs`
}  // namespace

auto InstallRouter(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	std::vector<pd2r::skills::RouteEntry> routes = pd2r::skills::Routes();  // each rule file adds its own (PD2R_ROUTE)
	std::sort(routes.begin(), routes.end(),
	          [](const pd2r::skills::RouteEntry& x, const pd2r::skills::RouteEntry& y) { return x.order < y.order; });
	for (const auto& r : routes) {
		if (!skill_router::Add(r.skillId, r.function)) {
			plog::Warn("srvdo router: skill %d not routed", r.skillId);
		}
	}
	char routerReport[160];
	const bool router = skill_router::InstallRouter(context, routerReport, sizeof(routerReport));
	if (used < reportSize) {
		const int n = std::snprintf(report + used, reportSize - used, "; %s", routerReport);
		used += n > 0 ? static_cast<size_t>(n) : 0;
	}
	return router;
}

PD2R_RULE(InstallRouter, -140)

auto InstallStrikeForAll(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	// Power Strike's cltdo 22 is PD2's (1.13c) function plus a local-player check; jumping over the check lets every
	// client draw everyone's strikes, as PD2 does.
	static constexpr uint8_t Je[1] = {0x74};
	const bool strike = context->PatchWriteU8(d2r::rva::CltDoStrikeLocalOnly, Je, sizeof(Je), 0xeb);
	if (used < reportSize) {
		const int n = std::snprintf(report + used, reportSize - used, "; cltdofunc 22 strike for all%s",
		                            strike ? "" : " FAILED (bytes differ)");
		used += n > 0 ? static_cast<size_t>(n) : 0;
	}
	return strike;
}

PD2R_RULE(InstallStrikeForAll, -130)

PD2R_SLOT(12, MissileSrvHit, 65, static_cast<MissileSrvHitFn>(SrvHitSpawnReturn), "return spawn")
PD2R_SLOT(13, MissileSrvDo, 46, static_cast<MissileSrvDoFn>(SrvDoReturn), "return")
PD2R_SLOT(14, MissileCltDo, 78, static_cast<MissileCltDoFn>(CltDoReturn), "return")
PD2R_SLOT(20, MissileSrvHit, 66, static_cast<MissileSrvHitFn>(SrvHitBounce), "bounce")
PD2R_SLOT(21, MissileSrvDmg, 21, static_cast<MissileSrvDmgFn>(SrvDmgFirstHit), "first hit")
PD2R_SLOT(22, MissileSrvHit, 63, static_cast<MissileSrvHitFn>(SrvHitExplodeSpread), "explode spread")
PD2R_SLOT(23, MissileSrvDmg, 20, static_cast<MissileSrvDmgFn>(SrvDmgPierceFalloff), "pierce falloff")
PD2R_SLOT(25, MissileSrvHit, 61, static_cast<MissileSrvHitFn>(SrvHitState), "state hit")
PD2R_SLOT(26, MissileSrvHit, 62, static_cast<MissileSrvHitFn>(SrvHitBoltSplash), "bolt splash")
PD2R_SLOT(27, MissileSrvHit, 64, static_cast<MissileSrvHitFn>(SrvHitUberImmolation), "uber immolation")
PD2R_SLOT(30, MissileCltHit, 66, static_cast<MissileCltHitFn>(CltHitHolyLight), "holy light")
PD2R_SLOT(31, MissileCltHit, 71, static_cast<MissileCltHitFn>(CltHitFistDelay), "fist delay")
PD2R_SLOT(48, MissileCltDo, 79, static_cast<MissileCltDoFn>(CltDoChargeExplode), "charge explode")
PD2R_SLOT(49, MissileCltHit, 67, static_cast<MissileCltHitFn>(CltHitChargeBolt), "charge bolt")
PD2R_SLOT(53, MissileSrvDmg, 22, static_cast<MissileSrvDmgFn>(SrvDmgChainFalloff), "chain falloff")
PD2R_SLOT(54, MissileSrvDo, 47, static_cast<MissileSrvDoFn>(SrvDoBlessedHammer), "blessed hammer")
PD2R_SLOT(55, MissileCltDo, 80, static_cast<MissileCltDoFn>(CltDoBlessedHammer), "blessed hammer")
PD2R_SLOT(56, MissileSrvHit, 69, static_cast<MissileSrvHitFn>(SrvHitBladeFragment), "blade fragment")
PD2R_SLOT(57, MissileCltHit, 68, static_cast<MissileCltHitFn>(CltHitBladeFragment), "blade fragment")
PD2R_SLOT(58, MissileSrvDmg, 19, static_cast<MissileSrvDmgFn>(SrvDmgBladeFragment), "blade fragment")
PD2R_SLOT(63, MissileSrvHit, 67, static_cast<MissileSrvHitFn>(SrvHitSummon), "summon")
PD2R_SLOT(66, MissileSrvDmg, 18, static_cast<MissileSrvDmgFn>(SrvDmgRathmaSpear), "rathma bone spear")
PD2R_SLOT(67, MissileSrvHit, 68, static_cast<MissileSrvHitFn>(SrvHitThrowPotion), "throw potion")
PD2R_SLOT(68, MissileCltHit, 72, static_cast<MissileCltHitFn>(CltHitThrowPotion), "throw potion")

auto InstallSkillFunctions(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	static constexpr uint8_t Empty[8] = {};  // the slot must still be a null pointer
	std::vector<SlotEntry> slots = Slots();  // each rule file adds its own (PD2R_SLOT), in the report's old order
	std::sort(slots.begin(), slots.end(), [](const SlotEntry& x, const SlotEntry& y) { return x.order < y.order; });
	for (const SlotEntry& f : slots) {
		const Table& table = *f.table;
		char name[96];
		std::snprintf(name, sizeof(name), "%s %d %s", table.name, f.slot, f.name);
		g_installs.Run("slot", name, 1, [&](char* why, size_t size) -> int {
			if (f.slot < table.firstFree || f.slot >= table.slots) {
				std::snprintf(why, size, "slot out of range (free %d-%d)", table.firstFree, table.slots - 1);
				return 0;
			}
			const uint64_t rva = table.rva + static_cast<uint64_t>(f.slot) * 8;
			if (!context->PatchWriteU64(rva, Empty, sizeof(Empty), reinterpret_cast<uint64_t>(f.function))) {
				std::snprintf(why, size, "slot not empty (D2R or another plugin uses it)");
				return 0;
			}
			return 1;
		});
	}
	// Every other install is a rule file of skill/ (skill/registry.h), sorted by (order, name): the temp buff, router
	// and the other pre-existing installs run at negative orders, the new ones from 10, the mode gate and the rest from 320.
	std::vector<pd2r::RuleEntry>& rules = pd2r::Rules();  // sorted in place: this runs once
	std::sort(rules.begin(), rules.end(), [](const pd2r::RuleEntry& x, const pd2r::RuleEntry& y) {
		return x.order != y.order ? x.order < y.order : std::strcmp(x.name, y.name) < 0;
	});
	for (const pd2r::RuleEntry& rule : rules) {
		g_installs.Run("rule", rule.name, rule.installN != nullptr ? rule.tried : 1, [&](char* text, size_t size) -> int {
			return rule.installN != nullptr ? rule.installN(context, text, size) : rule.install(context, text, size) ? 1 : 0;
		});
	}
	// Startup log: the part's one summary line (parts::InstallAll), plus a WARN or ERROR per install that fell short.
	char line[pd2r::installs::DetailSize + 160];
	for (const pd2r::installs::Result& r : g_installs.Results()) {
		const pd2r::installs::Status s = r.status();
		if (s != pd2r::installs::Status::Ok) {
			pd2r::installs::Line(r, line, sizeof(line));
			if (s == pd2r::installs::Status::Partial) {
				plog::Warn("skills %s", line);
			} else {
				plog::Error("skills %s", line);
			}
		}
	}
	g_installs.Summary(report, reportSize);
	g_skillTotal = g_installs.Expected();
	return g_installs.Installed();
}

auto WriteInstallResults(const D2RL::ConsoleCommandContext* command, bool failedOnly) noexcept -> void {
	char line[pd2r::installs::DetailSize + 160];
	g_installs.Summary(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("installs %s", line);  // asked for: the full list also goes to the log, the only per-install success lines
	int written = 0;
	for (const pd2r::installs::Result& r : g_installs.Results()) {
		if (failedOnly && r.status() == pd2r::installs::Status::Ok) {
			continue;
		}
		pd2r::installs::Line(r, line, sizeof(line));
		command->plugin->WriteConsoleMessage(line);
		plog::Info("installs %s", line);
		++written;
	}
	if (written == 0) {
		command->plugin->WriteConsoleMessage(g_installs.Results().empty() ? "no installs ran: the skills part is off"
		                                                                  : "no failed or partial installs");
	}
}

auto SkillFunctionCount() noexcept -> int {
	return g_skillTotal;  // set by InstallSkillFunctions, which every caller runs first
}
auto SkillFunctionCounters(char* out, size_t size) noexcept -> void {
	char missiles[768];
	MissileFunctionCounters(missiles, sizeof(missiles));
	std::snprintf(out, size,
	              "split throw casts %u, client %u, temp buffs %u, finds %u, refused %u, bash hits %u, knockbacks %u, "
	              "splash starts %u, leap landings %u, client %u, double throw casts %u, client %u, poison daggers %u, "
	              "client %u, blood warps %u, refused %u, desecrates %u, client %u, corpses hidden %u, revives %u, "
	              "revive events %u, splashes %u, channels %u, client %u, take-overs %u, blade fury refused %u, "
	              "held %u, charge-up hits %u, charges %u, charge hit frames %u, holy lights %u, gusts %u, hungers %u, "
	              "novas %u, client %u, item novas %u, static fields %u, vengeances %u, links %u, jousts %u, fire claws %u, hits %u, "
	              "client %u, wolf starts %u, refused %u, dragon flights %u, hits %u, holy sword starts %u, "
	              "smite starts %u, smite events %u, poison egg spawns %u, client %u, nest spawns %u, "
	              "nests full %u, cycler starts %u, frenzy hits %u, ally buffs %u, blade sentinel hits %u, units %u, %s",
	              g_splitThrowCasts.load(), g_splitThrowClientCasts.load(), g_tempBuffs.load(), g_finds.load(),
	              g_findsRefused.load(), g_bashHits.load(), g_bashKnockbacks.load(), g_splashStarts.load(), g_leaps.load(),
	              g_leapClients.load(), g_doubleThrowCasts.load(), g_doubleThrowClientCasts.load(),
	              g_poisonDaggers.load(), g_poisonDaggerClients.load(), g_bloodWarps.load(), g_bloodWarpsRefused.load(),
	              g_desecrates.load(), g_desecrateClients.load(), g_desecrateHidden.load(), g_revives.load(),
	              g_reviveEvents.load(), g_reviveSplashes.load(), g_channels.load(), g_channelClients.load(),
	              g_takeOvers.load(), g_bladeFuryRefusals.load(), g_heldRequests.load(), g_chargeHits.load(),
	              g_charges.load(), g_chargeHitFrames.load(), g_holyLights.load(), g_gusts.load(),
	              g_hungers.load(), g_novas.load(), g_novaClients.load(), g_itemNovas.load(), g_staticFields.load(), g_vengeances.load(),
	              g_vengeanceLinks.load(), g_jousts.load(), g_fireClaws.load(), g_fireClawHits.load(),
	              g_fireClawClients.load(), g_wolfStarts.load(), g_wolfRefusals.load(), g_dragonFlights.load(),
	              g_dragonFlightHits.load(), g_holySwordStarts.load(), g_smiteStarts.load(), g_smiteEvents.load(),
	              g_eggSpawns.load(), g_eggSpawnClients.load(), g_nestSpawns.load(), g_nestFull.load(),
	              g_cyclerStarts.load(), g_frenzyHits.load(), g_allyBuffs.load(), g_sentinelHits.load(),
	              g_sentinelUnits.load(), missiles);
}

auto AiOwnerGetter() noexcept -> AiOwnerFn {
	return g_petOwner;
}
