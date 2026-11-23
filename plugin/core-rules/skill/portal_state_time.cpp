// Portals: the just-portaled state (state 102) lasts 15 frames, as in PD2 (#2022).
//
// PD2's one patch is 1.13c D2Game +0x6d5b4 (`add ebx, 0x4b` -> 0xf) in the portal operate (+0x6d0f0): ebx = frame + 75
// is the expiry frame for both the state list (D2Common 0xa6a0, type 2) and the timer event of type 0xc (0x8ea80) that
// removes the state. D2R's compiler recomputes frame + 75 for each use, so the one PD2 site is two D2R sites in the
// portal operate (93847 0x58f680): `add edx, 0x4b` at 0x58fc36 (the state list, call 0x2f7300 with type 2) and
// `add r9d, 0x4b` at 0x58fc71 (the type 0xc timer event, call 0x48b720). Both change, or the state and its removal
// would end at different frames. D2RCore holds no copy of either. No SDK service reaches the state's duration: a byte
// patch of each imm8, found by signature.
#include "common.h"

#include "log.h"
#include "sig.h"

#include <cstdio>
#include <cstring>

namespace {

// mov edx,[r8+0x170] (game frame); xor r8d,r8d; add edx,0x4b; lea ecx,[r8+2]; call
constexpr const char* StateExpiry = "41 8b 90 70 01 00 00 45 33 c0 83 c2 4b 41 8d 48 02 e8";
constexpr int StateExpiryAt = 12;  // the imm8
// mov r9d,[rcx+0x170] (game frame); add r9d,0x4b; call
constexpr const char* TimerExpiry = "44 8b 89 70 01 00 00 41 83 c1 4b e8";
constexpr int TimerExpiryAt = 10;  // the imm8

}  // namespace

auto InstallPortalStateTime(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	static constexpr uint8_t Stock[] = {0x4b};
	static constexpr uint8_t Pd2[] = {0x0f};
	uint64_t state = 0;
	uint64_t timer = 0;
	const bool on = sigscan::ScanUnique(context->exeBase, StateExpiry, StateExpiryAt, &state) == 1 &&
	                sigscan::ScanUnique(context->exeBase, TimerExpiry, TimerExpiryAt, &timer) == 1 &&
	                context->PatchBytes(state, Stock, sizeof(Stock), Pd2, sizeof(Pd2)) &&
	                context->PatchBytes(timer, Stock, sizeof(Stock), Pd2, sizeof(Pd2));
	plog::Info("portal-state: pd2 15 frames %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; portal-state: pd2 15 frames %s", on ? "on" : "FAILED");
	}
	return on;
}
