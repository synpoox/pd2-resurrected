// Grim Ward takes corpses without the monstats2 `soft` flag too, as in PD2 (#2019).
//
// 1.13c: Grim Ward's srvdo 75 (D2Game +0x27c90) tests its corpse at +0x27cf7 with D2Common #10096: a dead monster
// (mode 12) not in the used-corpse state, with monstats2 flag bit 7 (corpseSel) and bit 12 (soft). PD2 points that one
// call at its stub 0x1030b610 (installed; PD2.gpr 0x1030b520), which resolves #10678: the same test without bit 12.
// On the client, PD2 bytes cltdo 41's bit 12 test (#10338 at D2Client +0xc7083, `push esi; call` -> `nop; mov eax,1`):
// the client draws the ward for any corpse. 1.13c cltst 28 (D2Client +0xc6350) already calls #10678 (+0xc638f), as D2R's
// cltst 28 0x230440 calls 0x33a5c0: nothing needed. PD2's client swap at +0xc49da is cltst 51 (+0xc4990, Hunger and
// fenris rage), not Grim Ward's: D2R's cltst 51 0x2b97a0 calls 0x33a510 at 0x2b97d5; out of scope, follow-up #2067.
//
// D2R 93847: 0x33a510 is #10096 (bit 7 and, through 0x34fa60, bit 12), 0x33a5c0 is #10678 (bit 7 only). Grim Ward's
// srvdo 75 (0x5682e0) calls 0x33a510 at 0x568356: that call is redirected to 0x33a5c0. Its starts (srvst 34 0x569c00,
// cltst 28 0x230440) already call 0x33a5c0 in D2R. Its cltdo 41 (0x22f610) calls 0x34fa60 at 0x22f67e: that call is
// redirected to a 1, as PD2's bytes. All by signature; the shared tests stay untouched (other skills and plugins use them).
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
// cltdo 41: mov rcx,rax / call 0x34fa60 / test eax,eax / je / mov edx,1 / mov rcx,r13 / call
constexpr const char* WardDrawCallPattern = "48 8b c8 e8 ?? ?? ?? ?? 85 c0 0f 84 ?? ?? ?? ?? ba 01 00 00 00 49 8b cd e8";
constexpr uint32_t WardDrawCallAt = 3;

using CorpseTestFn = int32_t (*)(Unit*) noexcept;
uint64_t g_stockTest = 0;  // 0x33a510, for the log only; set by RedirectCall before the call can land
uint64_t g_softTest = 0;   // 0x34fa60, for the log only
CorpseTestFn g_corpseSel = nullptr;
std::atomic<uint32_t> g_taken {0}, g_drawn {0};

auto GrimWardCorpse(Unit* corpse) noexcept -> int32_t {
	const int32_t ok = g_corpseSel(corpse);
	if (ok != 0 && pd2::HotSample(g_taken)) {
		const bool stock = reinterpret_cast<CorpseTestFn>(g_stockTest)(corpse) != 0;
		plog::Info("grim-ward: corpse of monstats %d taken, D2R's soft test %s (%u so far)",
		           d2r::At<int32_t>(corpse, d2r::rva::UnitClassId), stock ? "passes" : "refuses", g_taken.load());
	}
	return ok;
}

// PD2's `mov eax,1` in place of the client's soft test
auto WardDrawSoft(Unit* corpse) noexcept -> int32_t {
	if (corpse != nullptr && pd2::HotSample(g_drawn)) {
		const bool soft = reinterpret_cast<CorpseTestFn>(g_softTest)(corpse) != 0;
		plog::Info("grim-ward: client ward on monstats %d, soft %d (%u so far)",
		           d2r::At<int32_t>(corpse, d2r::rva::UnitClassId), soft ? 1 : 0, g_drawn.load());
	}
	return 1;
}

}  // namespace

auto InstallGrimWardCorpse(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	uint64_t corpseSel = 0;
	bool on = sigscan::ScanUnique(context->exeBase, CorpseSelPattern, 0, &corpseSel) == 1;
	if (on) {
		g_corpseSel = reinterpret_cast<CorpseTestFn>(context->exeBase + corpseSel);
		on = RedirectCall(context, GrimWardCallPattern, GrimWardCallAt, reinterpret_cast<uint64_t>(&GrimWardCorpse),
		                  &g_stockTest) &&
		     RedirectCall(context, WardDrawCallPattern, WardDrawCallAt, reinterpret_cast<uint64_t>(&WardDrawSoft), &g_softTest);
	}
	plog::Info("grim-ward: pd2 corpses %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; grim-ward: pd2 corpses %s", on ? "on" : "FAILED");
	}
	return on;
}

PD2R_RULE(InstallGrimWardCorpse, 120)
