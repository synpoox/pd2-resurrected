// Grim Ward takes corpses without the monstats2 `soft` flag too, as in PD2 (#2019).
//
// 1.13c: Grim Ward's srvdo 75 (D2Game +0x27c90) tests its corpse at +0x27cf7 with D2Common #10096: a dead monster
// (mode 12) not in the used-corpse state, with monstats2 flag bit 7 (corpseSel) and bit 12 (soft). PD2 points that one
// call at its stub 0x1030b610 (installed; PD2.gpr 0x1030b520), which resolves #10678: the same test without bit 12.
// Grim Ward's starts (srvst 34, cltst 28) already use #10678 in 1.13c and D2R; PD2 changes no other caller.
//
// D2R 93847: 0x33a510 is #10096 (bit 7 and, through 0x34fa60, bit 12), 0x33a5c0 is #10678 (bit 7 only). Grim Ward's
// srvdo 75 (0x5682e0) calls 0x33a510 at 0x568356; that one call is redirected to 0x33a5c0, both found by signature.
// The shared tests stay untouched (other skills and plugins use them). Server only: cursor targeting goes through
// cltst 28, already bit 7 only; cltdo 41's own bit 12 test is kept, as PD2 keeps 1.13c D2Client's.
#include "common.h"
#include "registry.h"

#include "hot_log.h"
#include "../item_rules.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Unit;

// srvdo 75: mov rcx,rax / call 0x33a510 / test eax,eax / je / mov eax,[rbx] / xor edi,edi / sub eax,2 / je
constexpr const char* GrimWardCallPattern =
    "48 8b c8 e8 ?? ?? ?? ?? 85 c0 0f 84 ?? ?? ?? ?? 8b 03 33 ff 83 e8 02 74 1e";
constexpr uint32_t GrimWardCallAt = 3;
// 0x33a5c0, the corpse test without bit 12 (#10678); 0x33a510 differs only in its tail (call 0x34fa60, not mov eax,1)
constexpr const char* CorpseSelPattern =
    "40 57 48 83 ec 20 48 8b f9 48 85 c9 74 6e e8 ?? ?? ?? ?? 83 f8 01 75 64 48 8b cf e8 ?? ?? ?? ?? 83 f8 0c 75 57 "
    "48 8b cf e8 ?? ?? ?? ?? 85 c0 75 4b 41 b8 cb 0e 00 00 48 89 5c 24 38 48 8d 15 ?? ?? ?? ?? 48 8b cf e8 ?? ?? ?? ?? "
    "48 8b cf 8b d8 e8 ?? ?? ?? ?? 8b d3 0f b6 c8 e8 ?? ?? ?? ?? 48 8b 5c 24 38 48 85 c0 75 1b 48 8d 4c 24 30 88 44 "
    "24 30 e8 ?? ?? ?? ?? 84 c0 74 01 cc 33 c0 48 83 c4 20 5f c3 0f b6 40 04 23 05 ?? ?? ?? ?? 74 ec b8 01 00 00 00";

using CorpseTestFn = int32_t (*)(Unit*) noexcept;
uint64_t g_stockTest = 0;  // 0x33a510, for the log only; set by RedirectCall before the call can land
CorpseTestFn g_corpseSel = nullptr;
std::atomic<uint32_t> g_taken {0};

auto GrimWardCorpse(Unit* corpse) noexcept -> int32_t {
	const int32_t ok = g_corpseSel(corpse);
	if (ok != 0 && pd2::HotSample(g_taken)) {
		const bool stock = reinterpret_cast<CorpseTestFn>(g_stockTest)(corpse) != 0;
		plog::Info("grim-ward: corpse of monstats %d taken, D2R's soft test %s (%u so far)",
		           d2r::At<int32_t>(corpse, d2r::rva::UnitClassId), stock ? "passes" : "refuses", g_taken.load());
	}
	return ok;
}

}  // namespace

auto InstallGrimWardCorpse(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	uint64_t corpseSel = 0;
	bool on = sigscan::ScanUnique(context->exeBase, CorpseSelPattern, 0, &corpseSel) == 1;
	if (on) {
		g_corpseSel = reinterpret_cast<CorpseTestFn>(context->exeBase + corpseSel);
		on = RedirectCall(context, GrimWardCallPattern, GrimWardCallAt, reinterpret_cast<uint64_t>(&GrimWardCorpse),
		                  &g_stockTest);
	}
	plog::Info("grim-ward: pd2 corpses %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; grim-ward: pd2 corpses %s", on ? "on" : "FAILED");
	}
	return on;
}

PD2R_RULE(InstallGrimWardCorpse, 110)
