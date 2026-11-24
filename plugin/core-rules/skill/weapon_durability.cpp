// Leap Attack and Blade Shield do not wear the weapon (#2155).
// skills.durability only checks broken weapons/ammo when an animation ends; it does not control hit wear.
// Both native hits call ApplyMeleeHit(..., updateDurability=1), which calls 0x4526c0 -> WeaponDurabilityLoss.
// The SDK has no combat durability policy, so redirect only Leap's call and patch Blade Shield's own call argument.
// Leap's native hit is also used by Joust: pass its actual skill id (r12d) to the redirect, keeping Joust's wear.
// Blade Sentinel already uses DealSplash (no durability step); Blade Fury uses missile hits (no weapon wear).
#include "../item_rules.h"
#include "common.h"
#include "registry.h"

#include "log.h"
#include "sig.h"

#include <cstdio>
#include <cstring>

namespace {

// 93847 0x56b4a9: xor r9d,r9d / mov byte [rsp+0x20],1 / mov r8,r14 / mov rdx,rsi / mov rcx,rdi / call
constexpr const char* LeapCallPattern =
    "45 33 c9 c6 44 24 20 01 4d 8b c6 48 8b d6 48 8b cf e8 ?? ?? ?? ?? 66 41 83 7f 6e 00";
constexpr uint32_t LeapArgumentAt = 3, LeapCallAt = 17;
// 93847 0x56e426: mov rdx,[rbx+8] / mov r9d,ebp / mov rcx,[rbx] / mov r8,rsi / mov byte [rsp+0x20],1 / call
constexpr const char* ShieldCallPattern =
    "48 8b 53 08 44 8b cd 48 8b 0b 4c 8b c6 c6 44 24 20 01 e8 ?? ?? ?? ?? f6 84 24 98 01 00 00 01";
constexpr uint32_t ShieldArgumentAt = 13;
constexpr uint8_t StockArgument[] = {0xc6, 0x44, 0x24, 0x20, 0x01};
constexpr uint8_t LeapArgument[] = {0x44, 0x89, 0x64, 0x24, 0x20};  // mov dword [rsp+0x20],r12d
constexpr uint8_t ShieldArgument[] = {0xc6, 0x44, 0x24, 0x20, 0x00};

using MeleeHitFn = void (*)(d2r::Game*, d2r::Unit*, d2r::Unit*, int32_t, uint8_t) noexcept;
MeleeHitFn g_meleeHit = nullptr;

constexpr auto WearWeapon(int32_t skillId) noexcept -> uint8_t {
	return skillId != 143;  // Leap Attack; Joust and any other user of the native hit keep wear
}
static_assert(WearWeapon(143) == 0 && WearWeapon(450) == 1 && WearWeapon(0) == 1);

auto LeapHit(d2r::Game* game, d2r::Unit* attacker, d2r::Unit* target, int32_t range, int32_t skillId) noexcept -> void {
	g_meleeHit(game, attacker, target, range, WearWeapon(skillId));
}

}  // namespace

auto InstallWeaponDurability(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	uint64_t leap = 0, shield = 0;
	const bool found = sigscan::ScanUnique(context->exeBase, LeapCallPattern, LeapArgumentAt, &leap) == 1 &&
	                   sigscan::ScanUnique(context->exeBase, ShieldCallPattern, ShieldArgumentAt, &shield) == 1;
	// Redirect first: if the argument patch fails, the original 1 still requests wear through the redirect.
	const bool on = found &&
	                RedirectCall(context, LeapCallPattern, LeapCallAt, reinterpret_cast<uint64_t>(&LeapHit),
	                             reinterpret_cast<uint64_t*>(&g_meleeHit)) &&
	                context->PatchBytes(leap, StockArgument, sizeof(StockArgument), LeapArgument, sizeof(LeapArgument)) &&
	                context->PatchBytes(shield, StockArgument, sizeof(StockArgument), ShieldArgument, sizeof(ShieldArgument));
	plog::Info("weapon-durability: Leap Attack and Blade Shield wear disabled %s", on ? "on" : "FAILED (signature or bytes)");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; skill weapon wear%s", on ? "" : " FAILED");
	}
	return on;
}

PD2R_RULE(InstallWeaponDurability, 300)
