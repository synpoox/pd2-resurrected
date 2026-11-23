// The inventory gold cap is a flat 5,000,000 at every level, as in PD2 (#2027).
//
// PD2 NOPs D2Common #10049 (1.13c +0x31940, the cap: level (stat 12) x 10000) to `push eax; call 0x102fc790`, which does
// `mov eax, 0x4c4b40; ret 4`. D2R's twin is 93847 0x34b320 (`imul eax, eax, 0x2710` over UnitStat(unit, 12, 0)); its 53
// call sites (in 28 functions: pickup, drop, buy and sell, deposit and withdraw, trade, the gold display, the corpse and
// the mercenary) all call it, none computes level x 10000 itself. So the function's own body is rewritten to
// `mov eax, 5000000; ret`. No SDK service reaches this rule: a byte patch of the function's first 6 bytes, found by
// the signature of the bytes after them (so it still finds the function once patched).
#include "common.h"
#include "registry.h"

#include "log.h"
#include "sig.h"

#include <cstdio>
#include <cstring>

namespace {

// call UnitStat; imul eax,eax,10000; add rsp,0x28; ret (entry + 6, after `sub rsp,0x28; xor r8d,r8d`)
constexpr const char* CapTail = "c0 41 8d 50 0c e8 ?? ?? ?? ?? 69 c0 10 27 00 00 48 83 c4 28 c3";
constexpr uint64_t TailAt = 6;

}  // namespace

auto GoldCapRva(const D2RL::PluginContext* context) noexcept -> uint64_t {
	uint64_t rva = 0;
	return sigscan::ScanUnique(context->exeBase, CapTail, 0, &rva) == 1 && rva >= TailAt ? rva - TailAt : 0;
}

auto InstallGoldCap(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	static constexpr uint8_t Stock[] = {0x48, 0x83, 0xec, 0x28, 0x45, 0x33};  // sub rsp,0x28; xor r8d,r8d
	static constexpr uint8_t Pd2[] = {0xb8, 0x40, 0x4b, 0x4c, 0x00, 0xc3};    // mov eax,5000000; ret
	const uint64_t rva = GoldCapRva(context);
	const bool on = rva != 0 && context->PatchBytes(rva, Stock, sizeof(Stock), Pd2, sizeof(Pd2));
	plog::Info("gold-cap: pd2 5000000 %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; gold-cap: pd2 5000000 %s", on ? "on" : "FAILED");
	}
	return on;
}

PD2R_RULE(InstallGoldCap, 50)
