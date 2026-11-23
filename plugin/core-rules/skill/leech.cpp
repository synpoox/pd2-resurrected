// Life and mana leech take PD2's reductions (#2009).
//
// PD2's hit apply copy (ProjectDiablo.dll 0x10305990 PD2.gpr, 0x103059a0 installed; over 1.13c D2Game +0xde0c0) calls
// its leech step 0x103039f0 (PD2.gpr; 0x10303a00 installed) in place of 1.13c's +0xdba40 with the hit apply's 4th
// argument, the missile flag (0: events 5/1, the melee hits; non-zero: events 6/2, missiles and area hits). A non-zero
// flag halves life and mana leech (`shr [edx+0x38],1` / `shr [edx+0x3c],1` at 0x10303a2d, unsigned) before 1.13c's
// leech. Its PvP-level zeroing is not ported (PvP is cut).
// PD2's Leap Attack landing (srvdo 153, 0x103b1300) hits through the melee splash callback 0x10303190 with leech
// divisor 3 in its block (0x103b1300's block +0x1c): life leech / 3, at least 1 when not 0; mana leech kept. Its hit
// goes through PD2's event step 0x10305c30, which calls the leech step with flag 0 (no halving). Our srvdo 172 hits
// with D2R's Leap Attack hit (skill_functions.cpp LeapHitOne), which names the victim here for that one hit.
//
// D2R: HitEvents (93847 0x44ce80) has the same 4th argument (missile 1, melee 0) and calls its leech 0x450c90 once,
// with no halving. Neither has an SDK service, so both are found by signature: an inline hook on HitEvents' entry keeps
// the flag for the call (nested hits from its event dispatch keep their own), and HitEvents' one call to the leech
// (0x44d038, its only caller) is redirected to the rule. Not a hook on the leech's entry: Cartographer hooks that
// entry (part LeechStep, map life steal), so its bytes are a jmp in the live image; the call chains through it (#2061).
// Leech runs on the server only; life and mana sync to clients as stats.
#include "common.h"
#include "../item_rules.h"
#include "registry.h"

#include "hot_log.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig_hook.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Game;
using d2r::Unit;

// HitEvents (game, attacker, target, missile, &damage): push rbp .. push r15 / lea rbp,[rsp-0x118] / sub rsp,0x218 /
// the cookie / mov rdi,[rbp+0x170] / mov ebx,r9d (the flag)
constexpr const char* HitEventsPattern =
    "40 55 53 56 57 41 56 41 57 48 8d ac 24 e8 fe ff ff 48 81 ec 18 02 00 00 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 85 "
    "00 01 00 00 48 8b bd 70 01 00 00 41 8b d9";
constexpr uint8_t HitEventsEntry[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57,
                                      0x48, 0x8d, 0xac, 0x24, 0xe8, 0xfe, 0xff, 0xff};
// HitEvents' call to the leech (game, attacker, target, &damage): and [rdi+4],0xfffd / jmp / mov r9,rdi / mov r8,rsi /
// mov rdx,r14 / mov rcx,r15 / call 0x450c90 / cmp r13d,1 (one match in 93847, the call at +26)
constexpr const char* LeechCallPattern =
    "b9 fd ff 00 00 66 21 4f 04 e9 ?? ?? ?? ?? 4c 8b cf 4c 8b c6 49 8b d6 49 8b cf e8 ?? ?? ?? ?? 41 83 fd 01";
constexpr uint32_t LeechCallAt = 26;
constexpr uint32_t LeapLifeDivisor = 3;

using HitEventsFn = void (*)(Game*, Unit*, Unit*, int32_t, void*) noexcept;
using LeechFn = void (*)(Game*, Unit*, Unit*, uint8_t*) noexcept;
HitEventsFn g_hitEvents = nullptr;
LeechFn g_leech = nullptr;
thread_local int32_t t_missile = 0;          // the flag of the innermost HitEvents on this thread
thread_local Unit* t_leapUnit = nullptr;     // the Leap Attack hit in progress (LeapAttackLeech)
thread_local Unit* t_leapVictim = nullptr;
std::atomic<uint32_t> g_halved {0}, g_leapDivided {0};

// The leech fields of the block: unsigned, as PD2's shr and 1.13c's div read them
auto Field(uint8_t* damage, int offset) noexcept -> uint32_t& {
	return *reinterpret_cast<uint32_t*>(damage + offset);
}

auto HitEvents(Game* game, Unit* unit, Unit* target, int32_t missile, void* damage) noexcept -> void {
	const int32_t outer = t_missile;
	t_missile = missile;
	g_hitEvents(game, unit, target, missile, damage);
	t_missile = outer;
}

auto Leech(Game* game, Unit* unit, Unit* target, uint8_t* damage) noexcept -> void {
	using d2r::rva::DamageLifeLeech, d2r::rva::DamageManaLeech;
	if (damage != nullptr) {
		uint32_t& life = Field(damage, DamageLifeLeech);
		uint32_t& mana = Field(damage, DamageManaLeech);
		if (t_missile != 0 && (life != 0 || mana != 0)) {
			const uint32_t lifeIn = life, manaIn = mana;
			life >>= 1;
			mana >>= 1;
			if (pd2::HotSample(g_halved)) {
				plog::Info("leech: missile hit halved, unit type %d, life %u -> %u, mana %u -> %u",
				           unit != nullptr ? d2r::At<int32_t>(unit, d2r::rva::UnitType) : -1, lifeIn, life, manaIn, mana);
			}
		}
		if (unit != nullptr && unit == t_leapUnit && target == t_leapVictim && life != 0) {
			const uint32_t lifeIn = life;
			life = std::max(life / LeapLifeDivisor, 1u);
			if (pd2::HotSample(g_leapDivided)) {
				plog::Info("leech: leap attack life %u -> %u, mana %u kept", lifeIn, life, mana);
			}
		}
	}
	g_leech(game, unit, target, damage);
}

}  // namespace

auto LeapAttackLeech(Unit* unit, Unit* victim) noexcept -> void {
	t_leapUnit = unit;
	t_leapVictim = victim;
}

auto InstallLeech(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	// The leech call first: until both are in, the HitEvents hook only keeps the flag
	const bool on = RedirectCall(context, LeechCallPattern, LeechCallAt, reinterpret_cast<uint64_t>(&Leech),
	                             reinterpret_cast<uint64_t*>(&g_leech)) &&
	                sigscan::HookBySignature(context, "HitEvents", HitEventsPattern, 0, HitEventsEntry,
	                                         sizeof(HitEventsEntry), reinterpret_cast<void*>(&HitEvents),
	                                         reinterpret_cast<void**>(&g_hitEvents));
	plog::Info("leech: pd2 reductions %s", on ? "on" : "FAILED (leech call or HitEvents signature or bytes)");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; leech reductions%s", on ? "" : " FAILED");
	}
	return on;
}

PD2R_RULE(InstallLeech, 30)
