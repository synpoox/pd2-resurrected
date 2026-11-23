// Chilling Armor's bolt starts at the attacker, as in PD2 (#2017).
//
// PD2's one patch is 1.13c D2Game +0x435bd (`44` -> `4c`) in aura event func 1 (`hitbymissile`, +0x434e0): the
// missile-create param block's +0x08 (start unit) gets ecx (the missile's owner, the attacker) instead of eax (the
// wearer). The +0x04 owner stays the wearer, so damage and credit are unchanged; only the start point moves.
// D2R: aura event func 1 is 93847 0x551d70 (table 0x238e5c0 slot 1). r12 = wearer, rbx = the hitting missile's owner
// (0x490300). Params at rsp+0x40: flags 0x20, +0x08 owner = r12 (0x551f8f), +0x10 start unit = r12 (0x551f99); the
// create call 0x5371a0 takes its start x/y from +0x10 when flags bit 0 is clear. rbx stays live to the call.
// chillingarmorbolt has no speed (a Blizzard shard falling in place), so with D2R's start it lands on the wearer.
#include "common.h"
#include "registry.h"

#include "log.h"
#include "sig.h"

#include <cstdio>
#include <cstring>

namespace {

// movups [rsp+0x50],xmm0; mov [rsp+0x48],r12; movups [rsp+0x60],xmm0; mov [rsp+0x50],r12
constexpr const char* StartStore = "0f 11 44 24 50 4c 89 64 24 48 0f 11 44 24 60 4c 89 64 24 50";
constexpr int StartStoreAt = 15;  // the second mov

}  // namespace

auto InstallChillingArmor(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	uint64_t rva = 0;
	static constexpr uint8_t Stock[] = {0x4c, 0x89, 0x64, 0x24, 0x50};  // mov [rsp+0x50],r12 (wearer)
	static constexpr uint8_t Pd2[] = {0x48, 0x89, 0x5c, 0x24, 0x50};    // mov [rsp+0x50],rbx (attacker)
	const bool on = sigscan::ScanUnique(context->exeBase, StartStore, StartStoreAt, &rva) == 1 &&
	                context->PatchBytes(rva, Stock, sizeof(Stock), Pd2, sizeof(Pd2));
	plog::Info("chilling-armor: bolt starts at attacker %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; chilling-armor: bolt starts at attacker %s", on ? "on" : "FAILED");
	}
	return on;
}

PD2R_RULE(InstallChillingArmor, 220)
