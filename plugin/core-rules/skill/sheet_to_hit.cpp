// The character sheet's chance to hit is PD2's (#2048), the same Pd2Chance the server roll uses (to_hit.cpp, #2010).
//
// PD2's client 0x10305340 (installed ProjectDiablo.dll, sha256 e7eec256...; PD2.gpr 0x10305330) takes the AR its sheet
// shows, the monster's monstats defense (x10/12 outside Normal, as 1.13c) and level, runs 0x10304f40 (its only caller,
// 0x10305429) on them with the player's stats, and returns PD2's chance 0x10304e70 unclamped; its sheet printer
// 0x102ab290 clamps it to 5-100. 0x10304f40 (PD2's copy of the target rules D2R's server applies in 0x4503c0):
// item_ignoretargetac (115) zeroes defense, item_fractionaltargetac (116) takes min(v, 100)% off it (v halved against a
// monstats boss), item_demon_tohit (123) and item_undead_tohit (124) add AR against monstats demons / undead. Two
// globals (0x105b1c6c / 0x105b1c48, set by PD2's monster hover 0x102f3c2a from MonsterData type flags 0x02 superunique
// / 0x08 unique of the last hovered monster) also block 115 and halve 116 (superunique) for those monsters.
// D2R 93847: the sheet's formatter 0x14e7fe0 (called from 0x14e83fa, 0x14e8476, 0x14e85d1, 0x14e866b) calls the
// chance 0x1514700 (its only caller, 0x14e8011) and clamps the result to 5-95 (0x14e8066). 0x1514700 adds charstats
// +0x38 (ToHitFactor, which the shown AR already holds) to AR at 0x151493b, which PD2's 0x10305340 does not, then
// computes 100 x AR / (AR + def) x 2 x alvl / (alvl + dlvl) clamped to 5-95 (0x1514976-0x15149d5); it reads neither
// stat. All inline, so patched (PatchBytes): the ToHitFactor add goes, the chance block saves AR, defense and the
// player and calls SheetChance, which applies PD2's 0x10304f40 (monstats flags from D2R's row lookup 0x976e0, as
// 0x1514700 reads the row) and Pd2Chance with D2R's monster level (max of monstats level and 0x90be0's area level).
// The formatter's 95 becomes 100. D2RCore and Cartographer hook neither.
// Approximated: D2R's sheet keeps a monster class, not a unit, so the superunique / unique gates are never set (the
// chance is the one against a plain monster of that class).
// Multiplayer: the sheet is client-side; a joined client runs the same patch on its own unit, and Pd2Chance is the
// server roll's function, so a host and a joined client show the same chance for the same AR, defense and levels.
#include "common.h"

#include "log.h"
#include "sig.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// PD2 0x10304f40's monstats flags (1.13c MonStatsTxt +0x0c; D2R's row holds the same dword at +0x3c, as its own tests
// 0x3aeff0 / 0x3af190 / 0x3af2c0 read it)
constexpr uint32_t FlagBoss = 0x40, FlagDemon = 0x2000, FlagUndead = 0x1800;
constexpr int RowFlags = 0x3c;

// 0x10304f40 on defense: 115 zeroes it (not a boss), else 116 takes min(v, 100)% off (v / 2 against a boss)
constexpr auto TargetDefense(int32_t def, int32_t ignore, int32_t fraction, uint32_t flags) noexcept -> int32_t {
	if (ignore != 0 && (flags & FlagBoss) == 0) {
		def = 0;
	}
	if (fraction > 0) {
		int32_t v = (flags & FlagBoss) != 0 ? fraction / 2 : fraction;
		v = v < 1 ? 0 : (v > 99 ? 100 : v);
		def -= static_cast<int32_t>(int64_t {def} * v / 100);
	}
	return def;
}

// 0x10304f40 on AR: 123 against a demon, 124 against an undead
constexpr auto TargetAr(int32_t ar, int32_t demon, int32_t undead, uint32_t flags) noexcept -> int32_t {
	if (demon != 0 && (flags & FlagDemon) != 0) {
		ar += demon;
	}
	if (undead != 0 && (flags & FlagUndead) != 0) {
		ar += undead;
	}
	return ar;
}

// Pinned values (tools/tests/test_sheet_to_hit.py mirrors these)
static_assert(TargetDefense(1000, 1, 0, 0) == 0);
static_assert(TargetDefense(1000, 1, 0, FlagBoss) == 1000);
static_assert(TargetDefense(1000, 0, 30, 0) == 700);
static_assert(TargetDefense(1000, 0, 30, FlagBoss) == 850);
static_assert(TargetDefense(1000, 0, 150, 0) == 0);
static_assert(TargetDefense(1000, 0, 1, FlagBoss) == 1000);
static_assert(TargetDefense(999, 0, 33, 0) == 670);
static_assert(TargetAr(500, 100, 0, FlagDemon) == 600);
static_assert(TargetAr(500, 100, 50, 0x0800) == 550);
static_assert(TargetAr(500, 100, 50, 0x1000) == 550);
static_assert(TargetAr(500, 100, 50, FlagBoss) == 500);

// 0x1514938: mov eax,[rax+0x38] / add r14d,eax (the charstats AR add, +3) ... the moves of a negative side
// (+0x3e: test r13d,r13d; r13d AR and edx defense, moved; eax/ecx the moves), the 100 x ratio into [rbp-0x35], D2R's
// level read (call at +0x5f), restores of r13/r12, then (+0x74, eax player level, edi monster level) the level factor
// and the 5-95 clamp into eax.
constexpr const char* ChanceTail =
    "8b 40 38 44 03 f0 4c 8b bc 24 b0 00 00 00 41 8b c5 48 8b b4 24 e8 00 00 00 41 8b ce 2b cb 85 db 0f 49 c3 41 0f "
    "49 ce 48 8b 9c 24 e0 00 00 00 85 c9 8b d0 44 0f 49 e9 2b d1 85 c9 0f 49 d0 45 85 ed 75 04 85 d2 74 0e 41 6b c5 "
    "64 42 8d 0c 2a 99 f7 f9 89 45 cb 45 33 c0 49 8b cc 41 8d 50 0c e8 ?? ?? ?? ?? 4c 8b ac 24 b8 00 00 00 4c 8b a4 "
    "24 c0 00 00 00 8d 0c 38 0f af 45 cb 48 8b bc 24 f0 00 00 00 03 c0 99 f7 f9 83 f8 05 7d 07 b8 05 00 00 00 eb 0a "
    "b9 5f 00 00 00 3b c1 0f 4f c1";
constexpr int HitFactorAt = 3;
constexpr uint8_t HitFactorStock[3] = {0x44, 0x03, 0xf0};  // add r14d,eax
constexpr uint8_t HitFactorNop[3] = {0x0f, 0x1f, 0x00};
constexpr int RatioAt = 0x3e;  // 23 bytes: the ratio, up to the level read's xor r8d,r8d
constexpr uint8_t RatioStock[23] = {0x45, 0x85, 0xed, 0x75, 0x04, 0x85, 0xd2, 0x74, 0x0e, 0x41, 0x6b, 0xc5,
                                    0x64, 0x42, 0x8d, 0x0c, 0x2a, 0x99, 0xf7, 0xf9, 0x89, 0x45, 0xcb};
constexpr int MathAt = 0x74;  // 42 bytes: the level factor, the restore of rdi, the clamp; the epilogue follows
constexpr uint8_t MathStock[42] = {0x8d, 0x0c, 0x38, 0x0f, 0xaf, 0x45, 0xcb, 0x48, 0x8b, 0xbc, 0x24, 0xf0, 0x00, 0x00,
                                   0x00, 0x03, 0xc0, 0x99, 0xf7, 0xf9, 0x83, 0xf8, 0x05, 0x7d, 0x07, 0xb8, 0x05, 0x00,
                                   0x00, 0x00, 0xeb, 0x0a, 0xb9, 0x5f, 0x00, 0x00, 0x00, 0x3b, 0xc1, 0x0f, 0x4f, 0xc1};
// 0x15147b9: movzx esi,al (data flag) / call / cwde (monster class) ... mov [rbp-0x31],eax / call 0x976e0 (+0x12), the
// monstats row of (data flag, class)
constexpr const char* RowCall = "0f b6 f0 e8 ?? ?? ?? ?? 98 40 0f b6 ce 8b d0 89 45 cf e8 ?? ?? ?? ?? 48 8b f0";
constexpr int RowCallAt = 0x12;

// 0x1514700's locals from rbp-0x39 (the block passes that address): data flag [rbp-0x38], AR [rbp-0x35], monster
// class [rbp-0x31] (read by D2R before), defense [rbp-0x21] and player [rbp-0x19] (dead locals the block writes)
constexpr int LocDataFlag = 0x01, LocAr = 0x04, LocClass = 0x08, LocDef = 0x18, LocPlayer = 0x20;

constexpr int32_t StatIgnoreAc = 115, StatFractionalAc = 116, StatDemonHit = 123, StatUndeadHit = 124;

using MonStatsRowFn = const uint8_t* (*)(uint32_t dataFlag, int32_t monster) noexcept;
MonStatsRowFn g_row = nullptr;

auto SheetChance(const uint8_t* locals, int32_t, int32_t dlvl, int32_t alvl) noexcept -> int32_t {
	const int32_t ar = d2r::At<int32_t>(locals, LocAr);
	const int32_t def = d2r::At<int32_t>(locals, LocDef);
	const int32_t monster = d2r::At<int32_t>(locals, LocClass);
	auto* player = d2r::At<d2r::Unit*>(locals, LocPlayer);
	const uint8_t* row = g_row != nullptr ? g_row(d2r::At<uint8_t>(locals, LocDataFlag), monster) : nullptr;
	const uint32_t flags = row != nullptr ? d2r::At<uint32_t>(row, RowFlags) : 0;
	const auto stat = [player](int32_t id) {
		return player != nullptr && d2r::api.unitStat != nullptr ? d2r::api.unitStat(player, id, 0) : 0;
	};
	const int32_t ignore = stat(StatIgnoreAc), fraction = stat(StatFractionalAc);
	const int32_t demon = stat(StatDemonHit), undead = stat(StatUndeadHit);
	const int32_t pd2Def = TargetDefense(def, ignore, fraction, flags);
	const int32_t pd2Ar = TargetAr(ar, demon, undead, flags);
	const int32_t chance = Pd2Chance(pd2Def, pd2Ar, dlvl, alvl);
	return chance;
}

auto PatchChance(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t rva = 0, call = 0;
	if (sigscan::ScanUnique(context->exeBase, ChanceTail, 0, &rva) != 1 ||
	    sigscan::ScanUnique(context->exeBase, RowCall, RowCallAt, &call) != 1) {
		return false;
	}
	int32_t rel = 0;
	std::memcpy(&rel, reinterpret_cast<const void*>(context->exeBase + call + 1), sizeof(rel));
	g_row = reinterpret_cast<MonStatsRowFn>(context->exeBase + call + 5 + static_cast<int64_t>(rel));
	// eax + r14d - ecx = the defense before the moves / mov [rbp-0x21],eax / mov [rbp-0x35],r14d (AR) /
	// mov [rbp-0x19],r12 (player) / jmp to the level read
	uint8_t ratio[sizeof(RatioStock)];
	std::memset(ratio, 0xcc, sizeof(ratio));
	const uint8_t save[] = {0x44, 0x01, 0xf0, 0x29, 0xc8, 0x89, 0x45, 0xdf, 0x44, 0x89, 0x75, 0xcb, 0x4c, 0x89, 0x65, 0xe7, 0xeb, 0x00};
	std::memcpy(ratio, save, sizeof(save));
	ratio[sizeof(save) - 1] = static_cast<uint8_t>(sizeof(ratio) - sizeof(save));
	// mov r9d,eax (alvl) / mov r8d,edi (dlvl) / lea rcx,[rbp-0x39] / mov rdi,[rsp+0xf0] (D2R's restore) /
	// mov rax,SheetChance / call rax / jmp to the epilogue (eax = the chance)
	uint8_t math[sizeof(MathStock)];
	std::memset(math, 0xcc, sizeof(math));
	const uint8_t head[] = {0x41, 0x89, 0xc1, 0x41, 0x89, 0xf8, 0x48, 0x8d, 0x4d, 0xc7,
	                        0x48, 0x8b, 0xbc, 0x24, 0xf0, 0x00, 0x00, 0x00, 0x48, 0xb8};
	std::memcpy(math, head, sizeof(head));
	const uint64_t target = reinterpret_cast<uint64_t>(&SheetChance);
	std::memcpy(math + sizeof(head), &target, 8);
	constexpr size_t Tail = sizeof(head) + 8;
	const uint8_t tail[] = {0xff, 0xd0, 0xeb, static_cast<uint8_t>(sizeof(math) - (Tail + 4))};
	std::memcpy(math + Tail, tail, sizeof(tail));
	return context->PatchBytes(rva + MathAt, MathStock, sizeof(MathStock), math, sizeof(math)) &&
	       context->PatchBytes(rva + RatioAt, RatioStock, sizeof(RatioStock), ratio, sizeof(ratio)) &&
	       context->PatchBytes(rva + HitFactorAt, HitFactorStock, sizeof(HitFactorStock), HitFactorNop, sizeof(HitFactorNop));
}

// 0x14e8066 (formatter): cmp esi,5 / jge / mov esi,5 / jmp / mov eax,0x5f / cmp esi,eax / cmovg esi,eax
constexpr const char* SheetClamp = "83 fe 05 7d 07 be 05 00 00 00 eb 0a b8 5f 00 00 00 3b f0 0f 4f f0";
constexpr int SheetCapAt = 12;

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
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; sheet-to-hit: pd2 chance %s, cap 100 %s", a, b);
	}
	return chance && cap;
}

#include "registry.h"
PD2R_RULE(InstallSheetToHit, 280)
