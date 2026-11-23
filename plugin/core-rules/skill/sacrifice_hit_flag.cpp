// Sacrifice's self-hit takes the caster's damage reduction, as in PD2 (#2018).
//
// PD2's one patch is 1.13c D2Game +0x99364 (`push 0` -> `push 1`) in Sacrifice's srvdo 64 (+0x99290): the 3rd stack
// argument of the hit apply call at +0x99371 (to +0xde0c0, PD2's copy 0x103059a0 installed). That call is the
// caster's life cost (attacker = defender = the caster); the hit on the target is the separate call at +0x99387
// (+0xded30), which keeps its own 0. The argument is the hit apply's missile flag: non-zero runs 1.13c's damage
// reduction step +0xdc0b0 (PD2 0x10305a83 -> stub 0x103114a0 -> +0xdc0b0) and picks events 6/2 over 5/1 and, in PD2,
// halves leech (0x10303a00).
// D2R: srvdo 64 is 93847 0x5645c0; its HitEvents (0x44ce80) call at 0x564839 passes r9d = 0 (`xor r9d,r9d` at
// 0x564828). In HitEvents a non-zero flag runs 0x44df10 (D2R's +0xdc0b0: event 11, the defender's damage resist %
// (stat 36) and flat damage reduction (stat 34), resists and absorbs, block total +0x134 rebuilt, event 16) and picks
// events 6/2. The events need result bit 1, which the block (result 0, hit flag 0x1000) lacks, and its leech fields
// are 0, so neither changes. What changes is the life cost: the caster's own damage reduction lowers it, and an
// absorbdamage skill on the caster (Energy Shield, Bone Armor, Cyclone Armor) takes its share.
// No SDK service reaches a call argument: a byte patch found by signature. 6 bytes for `mov r9d,1` do not fit in the
// 3 of the xor, so `push 1 / pop r9` and `push rbx / pop rdx` free them inside the argument set-up.
#include "common.h"

#include "log.h"
#include "sig.h"

#include <cstdio>
#include <cstring>

namespace {

// lea rax,[rsp+0x40]; mov [rbp+0x74],esi; xor r9d,r9d; mov [rsp+0x20],rax; mov r8,rbx; mov rdx,rbx; mov rcx,r15; call
constexpr const char* SelfHitCall = "48 8d 44 24 40 89 75 74 45 33 c9 48 89 44 24 20 4c 8b c3 48 8b d3 49 8b cf e8";
constexpr int SelfHitArgAt = 8;  // the xor

}  // namespace

auto InstallSacrificeHitFlag(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	uint64_t rva = 0;
	// xor r9d,r9d; mov [rsp+0x20],rax; mov r8,rbx; mov rdx,rbx
	static constexpr uint8_t Stock[] = {0x45, 0x33, 0xc9, 0x48, 0x89, 0x44, 0x24, 0x20, 0x4c, 0x8b, 0xc3, 0x48, 0x8b, 0xd3};
	// push 1; pop r9; mov [rsp+0x20],rax; mov r8,rbx; push rbx; pop rdx
	static constexpr uint8_t Pd2[] = {0x6a, 0x01, 0x41, 0x59, 0x48, 0x89, 0x44, 0x24, 0x20, 0x4c, 0x8b, 0xc3, 0x53, 0x5a};
	static_assert(sizeof(Stock) == sizeof(Pd2));
	const bool on = sigscan::ScanUnique(context->exeBase, SelfHitCall, SelfHitArgAt, &rva) == 1 &&
	                context->PatchBytes(rva, Stock, sizeof(Stock), Pd2, sizeof(Pd2));
	plog::Info("sacrifice: pd2 hit flag %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; sacrifice: pd2 hit flag %s", on ? "on" : "FAILED");
	}
	return on;
}
