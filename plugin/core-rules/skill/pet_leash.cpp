// PD2's pet leash before every think (#1989) and its mercenary follow rework (#1926).
//
// 1.13c's AI dispatcher (D2Game +0x9d3b0) runs a pre-think (+0x9cd90) before each think; non-zero skips the think. Its
// last step, the minion-to-leader walk (+0x9cb30, called at +0x9cdc5), is replaced by ProjectDiablo.dll 0x103606e0
// (PD2.gpr 0x10360670). For a monster whose AI owner (1.13c AI control +0x2c id, +0x30 type) is a player:
// - no owner unit, or not a player: 1.13c's step; AI 106 (ShadowMaster, leashed in its think): nothing; AI 102
//   (BladeCreeper): 1.13c's step;
// - skipped (0) when the monster has no walk mode (monstats2 mWL, the same test as 1.13c's step), velocity 0 (monstats
//   +0x32), or a dead owner;
// - otherwise PD2's leash (skill/common.h, the ShadowMaster think's rule) with the AI tick's target, which the
//   dispatcher has not filled yet, else the path target, else the AI target search (0x103a54c0: 1.13c +0xb1bc0, D2R
//   0x595f80, the Hireable think's own search), kept when not the owner, alive, a player or monster and within 1155:
//   walk back past 1.0 x 361 squared (3.2 x 361 with such a target), warp at 0x900, step aside within 5, wander one think in 12. A move skips the think.
// PD2 also skips mercenaries (its class ids) on a game flag of its own (game +0x2702); D2R has no such flag.
// D2R 93847 inlines the step in its pre-think (0x4a3a20, the leader walk at 0x4a3b50), so the dispatcher's one call of
// the pre-think (0x4a2bbb in 0x4a2a00's AI tick) is redirected: D2R's pre-think runs first, and when it lets the think
// run, a player's pet gets the leash. No SDK service runs before an AI think.
//
// The Hireable think (1.13c +0x1dd40, D2R 0x5beac0) loses its own follow (PD2's five patches): it never warps past
// 100, never runs past the run distance, never walks past the walk distance, never steps aside and never wanders. The
// leash moves the merc instead, so the five byte patches go in only once the leash redirect is in.
#include "common.h"
#include "registry.h"

#include "../item_rules.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// The dispatcher's pre-think call: lea r8,[rbp-0x51] (the AI tick); rdx unit, rcx game; E8 at +14
constexpr const char* PreThinkCall = "4C 8D 45 AF 48 89 7D 07 49 8B D6 49 8B CF E8 ?? ?? ?? ?? 85 C0 0F 85";
constexpr uint32_t PreThinkCallAt = 14;
// The Hireable think's AI target search call (0x5bee51 -> 0x595f80; 1.13c +0x1e03a -> +0xb1bc0, PD2's 0x103a54c0), then
// its `distance < 25` test; E8 at +19
constexpr const char* SearchCall = "4C 8D 8C 24 B0 00 00 00 48 8B D7 4C 8D 44 24 40 48 8B CD E8 ?? ?? ?? ?? 48 85 C0 74 ?? 83 7C 24 40 19";
constexpr uint32_t SearchCallAt = 19;

// D2R's AI tick (0x4a2a00): +0x00 AI control, +0x28 monstats record, +0x30 monstats2 record
constexpr int TickAi = 0x00, TickMonStats = 0x28, TickMonStats2 = 0x30;
// AI control: the owner's id and unit type (D2R's owner getter 0x4a53c0; 1.13c +0x2c, +0x30)
constexpr int AiOwnerId = 0x68, AiOwnerType = 0x6c;
// monstats AI (GetAiRecord 0x4a36c0 reads +0x52) and Velocity (D2R's field table: offset 0x68, a word); monstats2
// mode flags (+0xf0, bit 2 mWL, as the inlined leader step tests it)
constexpr int MonAi = 0x52, MonVelocity = 0x68, Mon2Modes = 0xf0;
constexpr uint8_t ModeWalk = 1 << 2;
constexpr int16_t AiShadowMaster = 106, AiBladeCreeper = 102;
constexpr int32_t WalkRadius = 3;  // PD2: 0x10356a40(..., 3, ...)

using PreThinkFn = int32_t (*)(Game*, Unit*, uint8_t* tick) noexcept;
using FindUnitFn = Unit* (*)(Game*, int32_t type, uint32_t id) noexcept;
using UnitDeadFn = int32_t (*)(Unit*) noexcept;

PreThinkFn g_preThink = nullptr;
leash::Engine g_leash;
std::atomic<uint32_t> g_calls {0}, g_moves[6] {};

auto PreThink(Game* game, Unit* unit, uint8_t* tick) noexcept -> int32_t {
	const int32_t stock = g_preThink(game, unit, tick);
	if (stock != 0 || game == nullptr || unit == nullptr || tick == nullptr) {
		return stock;
	}
	const uint8_t* ai = At<const uint8_t*>(tick, TickAi);
	const uint8_t* monstats = At<const uint8_t*>(tick, TickMonStats);
	const uint8_t* monstats2 = At<const uint8_t*>(tick, TickMonStats2);
	if (ai == nullptr || monstats == nullptr || monstats2 == nullptr || At<int32_t>(ai, AiOwnerType) != 0) {
		return 0;
	}
	const int16_t row = At<int16_t>(monstats, MonAi);
	if (row == AiShadowMaster || row == AiBladeCreeper || (At<uint8_t>(monstats2, Mon2Modes) & ModeWalk) == 0 ||
	    At<int16_t>(monstats, MonVelocity) == 0) {
		return 0;
	}
	Unit* owner = d2r::Engine<FindUnitFn>(d2r::sig::FindUnit)(game, 0, At<uint32_t>(ai, AiOwnerId));
	if (owner == nullptr || d2r::Engine<UnitDeadFn>(d2r::sig::UnitDead)(owner) != 0) {
		return 0;
	}
	++g_calls;
	int64_t d2 = -1;
	int32_t move = leash::MoveNone;
	bool fighting = false;
	const uint64_t moved = leash::Leash(g_leash, game, unit, nullptr, owner, WalkRadius, leash::TickModes(tick), g_moves, &d2, &move, &fighting);
	return moved != 0 ? 1 : 0;
}

// PD2's five Hireable-think patches (1.13c +0x1deb3, +0x1ded6, +0x1defb, +0x1e0eb, +0x1e11c): pattern, offset of the
// two bytes, D2R's, PD2's
struct Site {
	const char* name;
	const char* pattern;
	int offset;
	uint8_t from[2];
	uint8_t to[2];
};
constexpr Site Sites[] = {
	{"no warp past 100", "8B 4C 24 40 8B D0 83 F9 64 76 23 33 DB", 9, {0x76, 0x23}, {0xeb, 0x23}},             // 0x5bec54
	{"no run", "3B CB 76 2B C6 44 24 30 00 41 B9 01 00 00 00", 2, {0x76, 0x2b}, {0xeb, 0x2b}},                   // 0x5bec7b
	{"no walk", "41 3B CC 76 13 83 C0 FE A9 FB FF FF FF", 3, {0x76, 0x13}, {0xeb, 0x13}},                        // 0x5becab
	{"no step aside", "89 44 24 40 83 F8 01 77 29 41 FE CD", 7, {0x77, 0x29}, {0xeb, 0x29}},                      // 0x5befe9
	{"no wander", "6B C2 64 2B C8 83 F9 05 0F 8D ?? ?? ?? ?? 44 0F B6 4C 24 44", 6, {0xf9, 0x05}, {0xf9, 0x00}},  // 0x5bf043: < 5 -> < 0
};

}  // namespace

auto InstallPetLeash(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	uint64_t search = 0;
	const bool engine = d2r::sig::FindUnit.state == sigscan::State::Ok && d2r::sig::UnitDead.state == sigscan::State::Ok &&
	                    leash::Find(context->exeBase, &g_leash) &&
	                    sigscan::ScanUnique(context->exeBase, SearchCall, SearchCallAt, &search) == 1;
	if (engine) {
		int32_t rel = 0;
		std::memcpy(&rel, reinterpret_cast<const void*>(context->exeBase + search + 1), sizeof(rel));
		g_leash.search = reinterpret_cast<leash::SearchFn>(context->exeBase + search + 5 + static_cast<int64_t>(rel));
	}
	const bool leashOn = engine && RedirectCall(context, PreThinkCall, PreThinkCallAt, reinterpret_cast<uint64_t>(&PreThink),
	                                            reinterpret_cast<uint64_t*>(&g_preThink));
	// Fail safe: without the leash the Hireable think keeps its own follow
	uint64_t rvas[std::size(Sites)] = {};
	bool follow = leashOn;
	for (size_t i = 0; follow && i < std::size(Sites); ++i) {
		follow = sigscan::ScanUnique(context->exeBase, Sites[i].pattern, Sites[i].offset, &rvas[i]) == 1;
		if (!follow) {
			plog::Warn("pet-leash: merc site %s not found", Sites[i].name);
		}
	}
	// All five found before any is patched; a failed write leaves the merc on the leash with part of its follow
	for (size_t i = 0; follow && i < std::size(Sites); ++i) {
		follow = context->PatchBytes(rvas[i], Sites[i].from, 2, Sites[i].to, 2);
	}
	const char* followState = !leashOn ? "skipped" : follow ? "on" : "FAILED";
	plog::Info("pet-leash: pre-think leash %s; merc follow patches %s", leashOn ? "on" : "FAILED", followState);
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; pet-leash: leash %s, merc follow %s", leashOn ? "on" : "FAILED",
		              followState);
	}
	return leashOn;
}

PD2R_RULE(InstallPetLeash, 180)
