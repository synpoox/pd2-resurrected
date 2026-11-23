// Hunger and fenris rage take corpses without the monstats2 `soft` flag on the client too, as in PD2 (#2067).
//
// 1.13c: cltst 51 (D2Client +0xc4990, Hunger and fenris rage) tests its target at +0xc49da with D2Common #10096 (bit 7
// corpseSel and bit 12 soft). PD2 points that call at its stub 0x1030b610 (record 0x49cfc4), which resolves #10678: the
// same test without bit 12. The servers need nothing: PD2's Hunger srvdo 165 (0x103b2830 installed) takes the path's
// target with no corpse test, as core-rules' Hunger (router 190) does; fenris rage's srvdo 137 (D2Game +0xdd50) keeps
// #10096 at +0xddcb in PD2, as D2R's srvdo 137 (0x5861f0) keeps 0x33a510 at 0x586302.
//
// D2R 93847: cltst 51 (0x2b97a0) calls 0x33a510 (#10096) at 0x2b97d5: that call is redirected to 0x33a5c0 (#10678), by
// signature; the shared tests stay untouched.
#include "common.h"

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

// cltst 51: mov rbx,rax / test rax,rax / je / mov rcx,rax / call 0x33a510 / test eax,eax / je / mov eax,[rbx] /
// mov [rsp+30],rdi / xor edi,edi / sub eax,2 / je (cltst 0x2b0fc0 has `xor edx,edx` before its mov and calls 0x33a9a0)
constexpr const char* HungerCallPattern =
    "48 8b d8 48 85 c0 0f 84 ?? ?? ?? ?? 48 8b c8 e8 ?? ?? ?? ?? 85 c0 0f 84 ?? ?? ?? ?? 8b 03 48 89 7c 24 30 33 ff "
    "83 e8 02 74 1e";
constexpr uint32_t HungerCallAt = 15;

using CorpseTestFn = int32_t (*)(Unit*) noexcept;
uint64_t g_stockTest = 0;  // 0x33a510, for the log only; set by RedirectCall before the call can land
CorpseTestFn g_corpseSel = nullptr;
std::atomic<uint32_t> g_taken {0};

auto HungerCorpse(Unit* corpse) noexcept -> int32_t {
	const int32_t ok = g_corpseSel(corpse);
	if (ok != 0 && pd2::HotSample(g_taken)) {
		const bool stock = reinterpret_cast<CorpseTestFn>(g_stockTest)(corpse) != 0;
		plog::Info("hunger-corpse: client takes monstats %d, D2R's soft test %s (%u so far)",
		           d2r::At<int32_t>(corpse, d2r::rva::UnitClassId), stock ? "passes" : "refuses", g_taken.load());
	}
	return ok;
}

}  // namespace

auto InstallHungerCorpse(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	uint64_t corpseSel = 0;
	bool on = sigscan::ScanUnique(context->exeBase, CorpseSelPattern, 0, &corpseSel) == 1;
	if (on) {
		g_corpseSel = reinterpret_cast<CorpseTestFn>(context->exeBase + corpseSel);
		on = RedirectCall(context, HungerCallPattern, HungerCallAt, reinterpret_cast<uint64_t>(&HungerCorpse), &g_stockTest);
	}
	plog::Info("hunger-corpse: pd2 client corpses %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; hunger-corpse: pd2 client corpses %s", on ? "on" : "FAILED");
	}
	return on;
}

#include "registry.h"
PD2R_RULE(InstallHungerCorpse, 240)
