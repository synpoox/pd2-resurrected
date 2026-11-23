// A running player keeps their defense, as in PD2 (#2074).
//
// 1.13c's melee roll D2Game +0xde5a0 sets the hit flag without a to-hit roll when the defender is a player in mode 3
// (run) (+0xde5ea: `cmp [esi+0x10],3` / `mov ebx,1`, then `test bl,1` skips the roll +0xdde90). PD2's roll 0x103048b0
// (installed ProjectDiablo.dll, sha256 e7eec256...; 31 of the 34 call sites) drops that check and always calls its
// to-hit roll 0x10305140, which reads defense with no mode check. The 3 sites PD2 leaves (+0x4f872 in srvst 16, which
// no PD2 skill uses; +0x77788 and +0xa1185, whose functions nothing in D2Game calls or points to) never run. 1.13c has
// the rule nowhere else: the missile roll (+0x5ef71), D2Common's defense (#10672) and PD2's client to-hit display
// 0x10305340 have no run check.
// D2R 93847: MeleeRoll 0x44b720 keeps it at 0x44b7d7 (defender type 0, UnitMode 0x34ab60 == 3 -> ebx = 1, which skips
// the to-hit roll 0x44ba60). Its missile roll and defense 0x347b10 have none. D2R's client character sheet 0x14e5f34
// blanks Defense while the player runs (UnitMode == 3 writes CharacterSheetBlankValue, else 0x347b10); 1.13c's D2Client
// sheet reads #10672 with no mode check, so PD2 always shows it. D2RCore and Cartographer carry neither copy.
// Out of scope: the running block-chance rule (0x44b930, UnitMode `cmp eax,2`). No SDK service reaches a branch:
// both sites are `jne` -> `jmp`.
#include "common.h"

#include "log.h"
#include "sig.h"

#include <cstdio>
#include <cstring>

namespace {

// mov rcx,rdi / call UnitMode / cmp eax,3 / jne +3 / mov ebx,r13d / mov rcx,rsi / call
constexpr const char* RunCheck = "48 8b cf e8 ?? ?? ?? ?? 83 f8 03 75 03 41 8b dd 48 8b ce e8";
constexpr int RunJumpAt = 11;  // the jne
// character sheet: mov rcx,rsi / call UnitMode / cmp eax,3 / jne (to the defense read) / lea rax,[CharacterSheetBlankValue]
constexpr const char* SheetCheck = "48 8b ce e8 ?? ?? ?? ?? 83 f8 03 75 78 48 8d 05";
constexpr int SheetJumpAt = 11;

auto Patch(const D2RL::PluginContext* context, const char* pattern, int at, uint8_t jump) noexcept -> bool {
	uint64_t rva = 0;
	const uint8_t stock[] = {0x75, jump};  // jne: a running player is always hit / shows no Defense
	const uint8_t pd2[] = {0xeb, jump};    // jmp: as for any other mode
	return sigscan::ScanUnique(context->exeBase, pattern, at, &rva) == 1 &&
	       context->PatchBytes(rva, stock, sizeof(stock), pd2, sizeof(pd2));
}

}  // namespace

auto InstallRunningDefense(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool roll = Patch(context, RunCheck, RunJumpAt, 0x03);
	const bool sheet = Patch(context, SheetCheck, SheetJumpAt, 0x78);
	const char* r = roll ? "on" : "FAILED";
	const char* c = sheet ? "on" : "FAILED";
	plog::Info("running-defense: to-hit roll on runners %s, sheet defense while running %s", r, c);
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used,
		              "; running-defense: to-hit roll on runners %s, sheet defense while running %s", r, c);
	}
	return roll && sheet;
}

#include "registry.h"
PD2R_RULE(InstallRunningDefense, 200)
