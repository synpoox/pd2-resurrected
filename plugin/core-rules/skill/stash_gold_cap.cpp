// The stash gold cap is 25,000,000, as in PD2 (#2118). The inventory cap stays 5,000,000 (gold_cap.cpp, #2027).
//
// PD2 sends deposit and withdraw (1.13c D2Game +0xd14a2) to its own copy 0x103999f0, whose deposit clamps stat 15
// (goldbank) at 25,000,000 and whose withdraw clamps stat 14 at #10049. D2R's bank cap is 93847 0x34b310
// (`mov eax, 2500000; ret`): every bank-gold user calls it (deposit and the stash moves in 0x4ba580, the client's deposit
// limit), so its immediate becomes 25,000,000. One more 2,500,000 is inline: the shared-stash page check in D2Client
// SCmd.cpp 0x133610 (`cmp eax, 0x2625a0; jg` reject), so a page over 2,500,000 would be refused; it is raised first, and
// the cap only after it, so the game never makes gold it then refuses. No SDK service reaches either: byte patches.
#include "common.h"
#include "registry.h"

#include "log.h"
#include "sig.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr uint8_t Stock[] = {0xa0, 0x25, 0x26, 0x00};  // 2,500,000
constexpr uint8_t Pd2[] = {0x40, 0x78, 0x7d, 0x01};    // 25,000,000

}  // namespace

auto InstallStashGoldCap(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	uint64_t cap = 0;
	uint64_t page = 0;
	// int3 pad; mov eax, 2500000; ret; int3 -> the imm32 at +2
	const bool found = sigscan::ScanUnique(context->exeBase, "cc b8 a0 25 26 00 c3 cc", 2, &cap) == 1 &&
	                   // shr rax,0x20; cmp eax,0x7fffffff; ja; cmp eax,2500000; jg -> the imm32 at +0x10
	                   sigscan::ScanUnique(context->exeBase, "48 c1 e8 20 3d ff ff ff 7f 0f 87 ?? ?? ?? ?? 3d a0 25 26 00 0f 8f",
	                                       0x10, &page) == 1;
	const bool on = found && context->PatchBytes(page, Stock, sizeof(Stock), Pd2, sizeof(Pd2)) &&
	                context->PatchBytes(cap, Stock, sizeof(Stock), Pd2, sizeof(Pd2));
	plog::Info("stash-gold-cap: pd2 25000000 %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; stash-gold-cap: pd2 25000000 %s", on ? "on" : "FAILED");
	}
	return on;
}

PD2R_RULE(InstallStashGoldCap, 310)
