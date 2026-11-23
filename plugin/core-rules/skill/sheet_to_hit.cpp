// The character sheet's chance to hit is PD2's (#2048), the same Pd2Chance the server roll uses (to_hit.cpp, #2010).
//
// PD2's client 0x10305340 (installed ProjectDiablo.dll, sha256 e7eec256...; PD2.gpr 0x10305330) takes the sheet's AR,
// the monster's defense (monstats, x5/6 as 1.13c, then 0x10304f40's target-defense stats) and level, and the player's
// level (stat 12), and returns PD2's chance 0x10304e70 unclamped; its sheet printer 0x102ab290 clamps it to 5-100.
// D2R 93847: the sheet's formatter 0x14e7fe0 (called from 0x14e83fa, 0x14e8476, 0x14e85d1, 0x14e866b) calls the
// chance 0x1514700 (its only caller, 0x14e8011) and clamps the result to 5-95 (0x14e8066). 0x1514700 computes 100 x
// AR / (AR + def) x 2 x alvl / (alvl + dlvl) and clamps to 5-95 (0x1514976-0x15149d5). Both blocks are inline, so
// they are patched (PatchBytes): the chance block saves AR and defense, keeps D2R's level read (stat 12) and calls
// Pd2Chance with D2R's own inputs; the formatter's 95 becomes 100. D2RCore and Cartographer hook neither.
// Multiplayer: the sheet is client-side; a joined client runs the same patch on its own unit, and Pd2Chance is the
// server roll's function, so a host and a joined client show the same chance for the same AR, defense and levels.
#include "common.h"

#include "hot_log.h"
#include "log.h"
#include "sig.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// 0x1514976: test r13d,r13d (AR, moved to >= 0; edx defense, edi monster level) ... the 100 x ratio into [rbp-0x35],
// D2R's level read (call at +0x21), restores of r13/r12 (+0x26), then the level factor and the 5-95 clamp into eax.
constexpr const char* ChanceTail =
    "45 85 ed 75 04 85 d2 74 0e 41 6b c5 64 42 8d 0c 2a 99 f7 f9 89 45 cb 45 33 c0 49 8b cc 41 8d 50 0c e8 ?? ?? ?? ?? "
    "4c 8b ac 24 b8 00 00 00 4c 8b a4 24 c0 00 00 00 8d 0c 38 0f af 45 cb 48 8b bc 24 f0 00 00 00 03 c0 99 f7 f9 83 f8 "
    "05 7d 07 b8 05 00 00 00 eb 0a b9 5f 00 00 00 3b c1 0f 4f c1";
// +0 (23 bytes): the ratio, up to the level read's xor r8d,r8d
constexpr uint8_t RatioStock[23] = {0x45, 0x85, 0xed, 0x75, 0x04, 0x85, 0xd2, 0x74, 0x0e, 0x41, 0x6b, 0xc5,
                                    0x64, 0x42, 0x8d, 0x0c, 0x2a, 0x99, 0xf7, 0xf9, 0x89, 0x45, 0xcb};
// +0x36 (42 bytes, eax = player level): the level factor, the restore of rdi, the clamp; the epilogue follows
constexpr int MathAt = 0x36;
constexpr uint8_t MathStock[42] = {0x8d, 0x0c, 0x38, 0x0f, 0xaf, 0x45, 0xcb, 0x48, 0x8b, 0xbc, 0x24, 0xf0, 0x00, 0x00,
                                   0x00, 0x03, 0xc0, 0x99, 0xf7, 0xf9, 0x83, 0xf8, 0x05, 0x7d, 0x07, 0xb8, 0x05, 0x00,
                                   0x00, 0x00, 0xeb, 0x0a, 0xb9, 0x5f, 0x00, 0x00, 0x00, 0x3b, 0xc1, 0x0f, 0x4f, 0xc1};

// 0x14e8066 (formatter): cmp esi,5 / jge / mov esi,5 / jmp / mov eax,0x5f / cmp esi,eax / cmovg esi,eax
constexpr const char* SheetClamp = "83 fe 05 7d 07 be 05 00 00 00 eb 0a b8 5f 00 00 00 3b f0 0f 4f f0";
constexpr int SheetCapAt = 12;

std::atomic<uint32_t> g_shown {0};

auto SheetChance(int32_t def, int32_t ar, int32_t dlvl, int32_t alvl) noexcept -> int32_t {
	const int32_t chance = Pd2Chance(def, ar, dlvl, alvl);
	if (pd2::HotSample(g_shown)) {
		plog::Info("sheet-to-hit: ar %d def %d alvl %d dlvl %d -> %d%% (%u so far)", ar, def, alvl, dlvl, chance,
		           g_shown.load());
	}
	return chance;
}

auto PatchChance(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t rva = 0;
	if (sigscan::ScanUnique(context->exeBase, ChanceTail, 0, &rva) != 1) {
		return false;
	}
	// mov [rbp-0x35],r13d (AR) / mov [rbp-0x31],edx (defense; both locals are dead here) / jmp to the level read
	uint8_t ratio[sizeof(RatioStock)];
	std::memset(ratio, 0xcc, sizeof(ratio));
	const uint8_t save[] = {0x44, 0x89, 0x6d, 0xcb, 0x89, 0x55, 0xcf, 0xeb, static_cast<uint8_t>(sizeof(ratio) - 9)};
	std::memcpy(ratio, save, sizeof(save));
	// mov r9d,eax (alvl) / mov r8d,edi (dlvl) / mov ecx,[rbp-0x31] / mov edx,[rbp-0x35] / mov rdi,[rsp+0xf0] (D2R's
	// restore) / mov rax,SheetChance / call rax / jmp to the epilogue (eax = the chance)
	uint8_t math[sizeof(MathStock)];
	std::memset(math, 0xcc, sizeof(math));
	const uint8_t head[] = {0x41, 0x89, 0xc1, 0x41, 0x89, 0xf8, 0x8b, 0x4d, 0xcf, 0x8b, 0x55, 0xcb,
	                        0x48, 0x8b, 0xbc, 0x24, 0xf0, 0x00, 0x00, 0x00, 0x48, 0xb8};
	std::memcpy(math, head, sizeof(head));
	const uint64_t target = reinterpret_cast<uint64_t>(&SheetChance);
	std::memcpy(math + sizeof(head), &target, 8);
	constexpr size_t Tail = sizeof(head) + 8;
	const uint8_t tail[] = {0xff, 0xd0, 0xeb, static_cast<uint8_t>(sizeof(math) - (Tail + 4))};
	std::memcpy(math + Tail, tail, sizeof(tail));
	return context->PatchBytes(rva + MathAt, MathStock, sizeof(MathStock), math, sizeof(math)) &&
	       context->PatchBytes(rva, RatioStock, sizeof(RatioStock), ratio, sizeof(ratio));
}

auto PatchCap(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t rva = 0;
	const uint8_t stock[] = {0xb8, 0x5f, 0x00, 0x00, 0x00};  // mov eax,95
	const uint8_t pd2[] = {0xb8, 0x64, 0x00, 0x00, 0x00};    // mov eax,100 (PD2's printer 0x102ab290)
	return sigscan::ScanUnique(context->exeBase, SheetClamp, SheetCapAt, &rva) == 1 &&
	       context->PatchBytes(rva, stock, sizeof(stock), pd2, sizeof(pd2));
}

}  // namespace

auto InstallSheetToHit(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool chance = PatchChance(context);
	const bool cap = PatchCap(context);
	const char* a = chance ? "on" : "FAILED";
	const char* b = cap ? "on" : "FAILED";
	plog::Info("sheet-to-hit: pd2 chance %s, cap 100 %s", a, b);
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; sheet-to-hit: pd2 chance %s, cap 100 %s", a, b);
	}
	return chance && cap;
}

#include "registry.h"
PD2R_RULE(InstallSheetToHit, 280)
