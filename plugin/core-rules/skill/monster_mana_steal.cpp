// A monster's mana steal and the Mana Burn boss mod take PD2's scales (#1995).
//
// 1.13c D2Game's hit damage +0xdd450 rolls a monster's (non-pet) life and mana steal stats through +0xdcd80, which
// shifts them left 8 into the damage block (+0x38 / +0x3c). PD2 redirects only the mana call (+0xdd745) to
// ProjectDiablo.dll 0x10302520 (installed; PD2.gpr 0x10302510), which scales by 0x10 instead of 0x100 when the
// stealer is a monster (`cmp dword [esi],1; mov edi,0x10; cmovne edi,eax; imul ebx,edi`), the min and max stats both.
// D2R inlines that roll in its hit damage (93847 0x44c030): the mana block 0x44c7fb is reached only for a monster
// attacker that is not a pet (`cmp [rsp+0x50],1`, the attacker's type), with stats 63 / 62 `shl 8` at 0x44c812 /
// 0x44c837, as 1.13c. Both shifts become `shl 4` (x16, PD2's scale); life steal keeps its `shl 8`.
// 1.13c's manahit handler (umod 25, +0x221d0) sets stats 62 / 63 to its monlvl value `shl 8`; PD2 makes it `shl 1`.
// D2R's handler (0x496ce0, umod table slot 25) sets them with no shift, half PD2's: its two SetStat calls (0x496ebe,
// 0x496ed0) are redirected to one that doubles the value. Leech and umods run on the server only; mana syncs to a
// joined client as a stat.
#include "common.h"

#include "../item_rules.h"
#include "log.h"
#include "sig.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

// The monster mana steal roll: mov ebx,[rdi+0x124] / stat 63 / shl ebp,8 / cmp ebp,8 .. stat 62 / xor ecx,ecx / shl eax,8
constexpr const char* ManaRollPattern =
    "8b 9f 24 01 00 00 45 33 c0 48 8b ce 41 8d 50 3f e8 ?? ?? ?? ?? 8b e8 c1 e5 08 83 fd 08 7d 0c 85 db b8 00 00 00 00 "
    "0f 48 d8 eb 2e 45 33 c0 48 8b ce 41 8d 50 3e e8 ?? ?? ?? ?? 33 c9 c1 e0 08";
constexpr uint32_t ManaRollShifts[] = {0x19, 0x3e};  // the imm8 of each shl
// The manahit handler's stat sets: imul .. lea edx,[r9+0x3e] .. call SetStat / .. lea edx,[r9+0x3f] / call SetStat
constexpr const char* ManaBurnPattern =
    "41 0f af ce b8 1f 85 eb 51 44 8b c6 f7 e9 48 8b cb 8b fa 41 8d 51 3e c1 ff 05 8b c7 c1 e8 1f 03 f8 e8 ?? ?? ?? ?? "
    "45 33 c9 44 8b c7 48 8b cb 41 8d 51 3f e8 ?? ?? ?? ?? 45 8b c7";
constexpr uint32_t ManaBurnCalls[] = {0x21, 0x33};

using SetStatFn = uint64_t (*)(d2r::Unit*, uint32_t stat, int32_t value, uint32_t layer) noexcept;
SetStatFn g_setStat[2] = {};

template <int Call>
auto SetManaBurn(d2r::Unit* unit, uint32_t stat, int32_t value, uint32_t layer) noexcept -> uint64_t {
	const int32_t pd2 = static_cast<int32_t>(static_cast<uint32_t>(value) << 1);  // PD2's shl 1
	return g_setStat[Call](unit, stat, pd2, layer);
}

}  // namespace

auto InstallMonsterManaSteal(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	uint64_t at = 0;
	bool steal = sigscan::ScanUnique(context->exeBase, ManaRollPattern, 0, &at) == 1;
	static constexpr uint8_t Stock[] = {0x08};
	static constexpr uint8_t Pd2[] = {0x04};
	for (const uint32_t shift : ManaRollShifts) {
		steal = steal && context->PatchBytes(at + shift, Stock, sizeof(Stock), Pd2, sizeof(Pd2));
	}
	const bool burn = RedirectCall(context, ManaBurnPattern, ManaBurnCalls[0], reinterpret_cast<uint64_t>(&SetManaBurn<0>),
	                               reinterpret_cast<uint64_t*>(&g_setStat[0])) &&
	                  RedirectCall(context, ManaBurnPattern, ManaBurnCalls[1], reinterpret_cast<uint64_t>(&SetManaBurn<1>),
	                               reinterpret_cast<uint64_t*>(&g_setStat[1]));
	plog::Info("monster-mana-steal: mana steal x1/16 %s, mana burn shl 1 %s", steal ? "on" : "FAILED",
	           burn ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; monster mana steal%s, mana burn%s", steal ? "" : " FAILED",
		              burn ? "" : " FAILED");
	}
	return steal && burn;
}

#include "registry.h"
PD2R_RULE(InstallMonsterManaSteal, 270)
