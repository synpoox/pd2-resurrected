// PD2's to-hit chance (#2010): ProjectDiablo.dll 0x10304e70 (installed; PD2.gpr 0x10304e60), called by PD2's to-hit
// roll 0x10305140 (over 1.13c D2Game +0xdde90) as chance(defense, AR, defender level, attacker level), and by its
// client display 0x10305340. D2R's roll (93847 0x44ba60) computes the chance inline: 100 x AR / (AR + def) x 2 x alvl /
// (alvl + dlvl) clamped to 5-95 (0x44bd31-0x44bd64). Its only callers are MeleeRoll (0x44b840; core-rules' splash and
// Blade Sentinel reach it through g_toHitRoll) and missile collision (0x463c5f), both direct calls, and D2RCore has no
// copy, so the inline block is replaced with a call to PD2's math, keeping D2R's AR, defense and levels. No SDK service
// covers the to-hit roll; the block goes through the MutationService (PatchBytes).
#include "common.h"

#include "hot_log.h"
#include "log.h"
#include "sig.h"

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// PD2's 0x10304e70, step by step (32-bit integer math, each division truncated as idiv does)
constexpr auto Pd2Chance(int32_t def, int32_t ar, int32_t dlvl, int32_t alvl) noexcept -> int32_t {
	// a negative side moves onto the other one (D2R's inputs are already moved, so this is a no-op here)
	if (def < 0) {
		ar -= def;
		def = 0;
	}
	if (ar < 0) {
		def -= ar;
		ar = 0;
	}
	const int32_t sum = ar + def;
	const int32_t ratio = sum != 0 ? 111 * ar / sum : 100;
	const int32_t levels = dlvl + alvl;
	if (levels == 0) {
		return 5;
	}
	const int32_t chance = ratio * alvl * 2 / levels;
	if (chance < 5) {
		return 5;
	}
	if (chance <= 95 || alvl == 0) {
		return chance < 100 ? chance : 100;
	}
	// the ratio's part above the one that gives 95 is scaled by 90/111 (through sum, as PD2 does), then the level factor
	int32_t over = ratio - 95 * levels / (2 * alvl);
	if (sum != 0) {
		over = sum * over / 111 * 90 / sum;
	}
	const int32_t soft = over * alvl * 2 / levels + 95;
	return soft < 100 ? soft : 100;
}

// Pinned values (tools/tests/test_to_hit.py mirrors these): def, AR, dlvl, alvl -> chance
static_assert(Pd2Chance(1000, 1000, 50, 50) == 55);   // D2R: 50
static_assert(Pd2Chance(1000, 3000, 50, 50) == 83);   // D2R: 75
static_assert(Pd2Chance(500, 5000, 50, 50) == 99);    // D2R: 90
static_assert(Pd2Chance(100, 10000, 85, 85) == 100);  // D2R: 95
static_assert(Pd2Chance(500, 5000, 99, 90) == 95);    // D2R: 85
static_assert(Pd2Chance(1000, 10, 80, 10) == 5);      // D2R: 5
static_assert(Pd2Chance(0, 0, 50, 50) == 100);        // no AR and no defense: PD2 skips the 90/111 step
static_assert(Pd2Chance(100, 100, 0, 0) == 5);

// D2R 93847 0x44bd31: lea ecx,[rax+rbx] ... the 100 x ratio, the level factor, the 5-95 clamp into edi. In: eax AR,
// ebx defense (both moved to >= 0), [rsp+0x38] defender level, r13d attacker level. The next instruction (mov rcx,rsi)
// sets up a call, so every volatile register is dead after the block.
constexpr const char* ChanceBlock =
    "8d 0c 18 85 c9 74 08 6b c0 64 99 f7 f9 8b d0 8b 4c 24 38 bf 5f 00 00 00 41 03 cd 41 0f af d5 8d 04 12 99 f7 f9 "
    "b9 05 00 00 00 3b c1 0f 4f c8 3b cf 0f 4c f9";
constexpr uint8_t ChanceStock[52] = {0x8d, 0x0c, 0x18, 0x85, 0xc9, 0x74, 0x08, 0x6b, 0xc0, 0x64, 0x99, 0xf7, 0xf9,
                                     0x8b, 0xd0, 0x8b, 0x4c, 0x24, 0x38, 0xbf, 0x5f, 0x00, 0x00, 0x00, 0x41, 0x03,
                                     0xcd, 0x41, 0x0f, 0xaf, 0xd5, 0x8d, 0x04, 0x12, 0x99, 0xf7, 0xf9, 0xb9, 0x05,
                                     0x00, 0x00, 0x00, 0x3b, 0xc1, 0x0f, 0x4f, 0xc8, 0x3b, 0xcf, 0x0f, 0x4c, 0xf9};

std::atomic<uint32_t> g_rolls {0};
uint64_t g_exeBase = 0;
// The roll's own return address, from our call inside it: D2R's frame is 5 pushes + 0x490 (0x44ba63-0x44ba6a), so it
// sits 0x4b8 above the rsp the block calls from. 0x44b845 is MeleeRoll (melee, splash, Blade Sentinel), 0x463c64
// collision (missiles); for the hot log only.
constexpr int RollFrame = 0x4b8;

auto Chance(int32_t def, int32_t ar, int32_t dlvl, int32_t alvl) noexcept -> int32_t {
	const int32_t chance = Pd2Chance(def, ar, dlvl, alvl);
	if (const uint32_t n = ++g_rolls; pd2::HotSample(n)) {
		const auto* slot = static_cast<const uint8_t*>(_AddressOfReturnAddress());
		const uint64_t from = *reinterpret_cast<const uint64_t*>(slot + 8 + RollFrame) - g_exeBase;
		plog::Info("to-hit: ar %d def %d alvl %d dlvl %d -> %d%%, from 0x%llx (%u so far)", ar, def, alvl, dlvl, chance,
		           static_cast<unsigned long long>(from), n);
	}
	return chance;
}

}  // namespace

auto InstallToHit(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	uint64_t rva = 0;
	g_exeBase = context->exeBase;
	bool on = sigscan::ScanUnique(context->exeBase, ChanceBlock, 0, &rva) == 1;
	if (on) {
		// mov ecx,ebx / mov edx,eax / mov r8d,[rsp+0x38] / mov r9d,r13d / mov rax,Chance / call rax / mov edi,eax /
		// jmp over the rest (int3)
		uint8_t block[sizeof(ChanceStock)];
		std::memset(block, 0xcc, sizeof(block));
		const uint8_t head[] = {0x8b, 0xcb, 0x8b, 0xd0, 0x44, 0x8b, 0x44, 0x24, 0x38, 0x45, 0x8b, 0xcd, 0x48, 0xb8};
		std::memcpy(block, head, sizeof(head));
		const uint64_t target = reinterpret_cast<uint64_t>(&Chance);
		std::memcpy(block + sizeof(head), &target, 8);
		constexpr size_t Tail = sizeof(head) + 8;
		const uint8_t tail[] = {0xff, 0xd0, 0x8b, 0xf8, 0xeb, static_cast<uint8_t>(sizeof(block) - (Tail + 6))};
		std::memcpy(block + Tail, tail, sizeof(tail));
		on = context->PatchBytes(rva, ChanceStock, sizeof(ChanceStock), block, sizeof(block));
	}
	plog::Info("to-hit: pd2 chance %s", on ? "on" : "FAILED (chance block signature or bytes)");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; to-hit%s", on ? "" : " FAILED");
	}
	return on;
}
