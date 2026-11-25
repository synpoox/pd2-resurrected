// mind_blast
#include "common.h"
#include "registry.h"

#include "ai_registry.h"
#include "../item_rules.h"
#include "line_spread.h"
#include "log.h"
#include "../missile_functions.h"
#include "parry_roll.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"
#include "skill_router.h"
#include "../skill_functions.h"
#include "../core-events/arena_spots.h"

#include <D2RLPlugin/mutations.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <map>
#include <mutex>
#include <span>

using namespace pd2r::skills;

namespace {

// --- Mind Blast's stacks and damage only (#1946) ---------------------------------------------------------------------
// PD2's srvdo 51 (ProjectDiablo.dll 0x103af800) and cltdo 8 (0x103aaf10). The server reads `had`, aurastat1 on the
// caster's aurastate list (a new list starts at 0), raises it by 1 while under calc2, sets the list's life to
// max(had, 1) x auralencalc frames and searches around the target; its per-unit callback (0x10304a90) only deals the
// copied damage block (0x10305760). The client draws the blast missile at the radius from its own copy of the list.
// The radius follows PD2's published numbers (wiki: +1.33 / 2.66 / 4 yards at 1 / 2 / 3+ stacks): aurarangecalc +
// the count after this cast's raise x calc1. The DLL as decoded uses the count before the raise, (max(had, 1) - 1)
// x calc1 (docs/deviations.md).
// D2R 93847's srvdo 51 (0x570780) takes its radius from the call at 0x570801 (prgcalc by D2R's progressive flag, which
// Mind Blast lacks, so 0 and then aurarangecalc) and never stacks; its cltdo 8 (0x2b3020) evaluates aurarangecalc at
// 0x2b30b9; its per-unit callback (0x56e590, used by srvdo 51 alone) first rolls vanilla's convert (0x5727e0, called
// only there: calc1 % for calc2 + rand(Param4) frames, the monster then taking no damage). The radius calls are
// redirected and the convert call becomes `xor eax,eax` (not converted: the damage follows), all by signature: no SDK
// service reaches a skill's area radius or its per-unit step.
constexpr const char* MindBlastRangeCall =
    "4c 89 a4 24 10 02 00 00 44 8b c6 48 8b d7 48 8b cb e8 ?? ?? ?? ?? 44 8b e0 85 c0 75 ?? 45 8b 86 84 00 00 00";

constexpr uint32_t MindBlastRangeCallAt = 17;

constexpr const char* MindBlastDrawCall =
    "44 8b 83 84 00 00 00 44 8b cd 48 8b d6 44 89 7c 24 20 41 0f b6 ce e8 ?? ?? ?? ?? 0f 57 c0";

constexpr uint32_t MindBlastDrawCallAt = 22;

constexpr const char* MindBlastConvertCall =
    "48 8b 71 18 48 8b fa 4c 8b c2 48 8b d9 48 8b 51 08 4c 8b ce 48 8b 09 e8 ?? ?? ?? ?? 85 c0 0f 85";

constexpr uint32_t MindBlastConvertCallAt = 23;

using MindBlastRangeFn = int32_t (*)(Game*, Unit*, int32_t, int32_t) noexcept;

using CalcFn = int32_t (*)(uint32_t, Unit*, int32_t, int32_t, int32_t) noexcept;

MindBlastRangeFn g_mindBlastRange = nullptr;  // D2R's, unused: PD2's do never reads prgcalc

CalcFn g_mindBlastDrawCalc = nullptr;


constexpr auto MindBlastNext(int32_t had, int32_t cap) noexcept -> int32_t {
	return had < cap ? had + 1 : had;
}

constexpr auto MindBlastRadius(int32_t range, int32_t had, int32_t perStack, int32_t cap) noexcept -> int32_t {
	return range + MindBlastNext(had, cap) * perStack;
}
// Mind Blast's row: aurarangecalc 4 at 1 hard point, calc1 2, calc2 3. Fast casts 1-4: 6, 8, 10, 10 (subtiles).

// Mind Blast's row: aurarangecalc 4 at 1 hard point, calc1 2, calc2 3. Fast casts 1-4: 6, 8, 10, 10 (subtiles).
static_assert(MindBlastRadius(4, 0, 2, 3) == 6 && MindBlastRadius(4, 1, 2, 3) == 8 && MindBlastRadius(4, 2, 2, 3) == 10 &&
              MindBlastRadius(4, 3, 2, 3) == 10);

static_assert(MindBlastNext(0, 3) == 1 && MindBlastNext(2, 3) == 3 && MindBlastNext(3, 3) == 3);

// aurastat1 on the unit's aurastate list (0 with no list), or -1 when the row has no such pair

// aurastat1 on the unit's aurastate list (0 with no list), or -1 when the row has no such pair
auto MindBlastStacks(uint32_t dataFlag, Unit* unit, const uint8_t* skill) noexcept -> int32_t {
	const int32_t state = At<int16_t>(skill, d2r::rva::SkAuraState), stat = At<int16_t>(skill, d2r::rva::SkAuraStat1);
	if (state <= 0 || stat < 0) {
		return -1;
	}
	void* list = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(d2r::sig::UnitStateList)(unit, state);
	return list != nullptr ? d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(d2r::sig::ListStatValue)(
	                             dataFlag, list, stat, 0)
	                       : 0;
}

// Server: PD2's stack step, then the radius (D2R takes aurarangecalc when this returns 0).
// shortcut: NewStateList refuses a recast at a lower level than the running list's, which then keeps its count and
// life; PD2 raises and extends it. Upgrade if a cast after losing +skills mid-chain matters.

// Server: PD2's stack step, then the radius (D2R takes aurarangecalc when this returns 0).
// shortcut: NewStateList refuses a recast at a lower level than the running list's, which then keeps its count and
// life; PD2 raises and extends it. Upgrade if a cast after losing +skills mid-chain matters.
auto MindBlastRange(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = game != nullptr ? d2r::DataFlag(game) : 0;
	const uint8_t* skill = game != nullptr && unit != nullptr ? d2r::api.getSkillRecord(dataFlag, skillId) : nullptr;
	if (skill == nullptr) {
		return 0;
	}
	const int32_t range = d2r::EvalCalc(game, unit, skill, SkAuraRangeCalc, skillId, level);
	const int32_t had = MindBlastStacks(dataFlag, unit, skill);
	if (had < 0) {
		return range;
	}
	const int32_t state = At<int16_t>(skill, SkAuraState), stat = At<int16_t>(skill, SkAuraStat1);
	// PD2's life: casts must come under auralencalc apart to build to calc2; at the cap the list lasts 3 x auralencalc
	const int32_t frames = std::max(had, 1) * d2r::EvalCalc(game, unit, skill, SkAuraLenCalc, skillId, level);
	const int32_t perStack = d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level);
	const int32_t cap = d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level);
	const int32_t next = MindBlastNext(had, cap);
	d2r::StateListRequest request {unit, unit, skillId, level, frames, -1, 0, state,
	                               d2r::Engine<void (*)()>(d2r::sig::SkillStateExpired)};
	void* list = d2r::Engine<void* (*)(d2r::StateListRequest*) noexcept>(d2r::sig::NewStateList)(&request);
	if (list != nullptr) {
		// set on a new list too: a replaced one (another level) keeps PD2's count
		d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(d2r::sig::SetListStat)(dataFlag, list,
		                                                                                                stat, next, 0);
		d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::SetStateGfxFlag)(unit, state, 1);
	}
	const int32_t radius = MindBlastRadius(range, had, perStack, cap);
	return radius;
}

// Client: the drawn radius. The client draws before the server's raise for this cast reaches its copy of the list,
// so it raises the count it holds (0 with no list) the way the server does.

// Client: the drawn radius. The client draws before the server's raise for this cast reaches its copy of the list,
// so it raises the count it holds (0 with no list) the way the server does.
auto MindBlastDrawRange(uint32_t dataFlag, Unit* unit, int32_t calc, int32_t skillId, int32_t level) noexcept -> int32_t {
	const int32_t range = g_mindBlastDrawCalc(dataFlag, unit, calc, skillId, level);
	const uint8_t* skill = unit != nullptr ? d2r::api.getSkillRecord(dataFlag, skillId) : nullptr;
	const int32_t had = skill != nullptr ? MindBlastStacks(dataFlag, unit, skill) : -1;
	if (had < 0) {
		return range;
	}
	const auto calcOf = [&](int field) {
		return d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, field), skillId, level);
	};
	return MindBlastRadius(range, had, calcOf(d2r::rva::SkCalc1), calcOf(d2r::rva::SkCalc2));
}

// The convert call in srvdo 51's per-unit callback becomes `xor eax,eax; nop dword [rax]` (expected: its E8 call)

// The convert call in srvdo 51's per-unit callback becomes `xor eax,eax; nop dword [rax]` (expected: its E8 call)
auto PatchMindBlastConvert(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t match = 0;
	if (sigscan::ScanUnique(context->exeBase, MindBlastConvertCall, 0, &match) != 1) {
		return false;
	}
	const uint64_t at = match + MindBlastConvertCallAt;
	uint8_t stock[5];
	std::memcpy(stock, reinterpret_cast<const void*>(context->exeBase + at), sizeof(stock));
	static constexpr uint8_t NotConverted[5] = {0x31, 0xc0, 0x0f, 0x1f, 0x00};
	return stock[0] == 0xe8 && context->PatchBytes(at, stock, sizeof(stock), NotConverted, sizeof(NotConverted));
}

// PD2's srvdo 51 searches with the fixed unit mask 0x8583; D2R's passes the row's aurafilter (Mind Blast 33667 =
// 0x8383). In D2R's mask test (0x439c10) 0x200 asks a clear line from the centre (collision test 0x363f20) and 0x400
// a valid target (unit +0x124 bit 3), so PD2 hits through walls only units that can be targeted. At 0x57099d
// `mov eax,[r14+0x70]; mov [rsp+0x28],eax` becomes `mov dword [rsp+0x28],0x8583`.

// PD2's srvdo 51 searches with the fixed unit mask 0x8583; D2R's passes the row's aurafilter (Mind Blast 33667 =
// 0x8383). In D2R's mask test (0x439c10) 0x200 asks a clear line from the centre (collision test 0x363f20) and 0x400
// a valid target (unit +0x124 bit 3), so PD2 hits through walls only units that can be targeted. At 0x57099d
// `mov eax,[r14+0x70]; mov [rsp+0x28],eax` becomes `mov dword [rsp+0x28],0x8583`.
constexpr const char* MindBlastMaskArg =
    "c7 44 24 50 34 0b 00 00 48 8b cb 48 89 44 24 48 48 8d 44 24 68 c7 44 24 40 00 00 00 00 48 89 44 24 38 48 8d 05 ?? "
    "?? ?? ?? 48 89 44 24 30 41 8b 46 70 89 44 24 28 44 89 64 24 20 e8";

constexpr uint32_t MindBlastMaskArgAt = 46;


auto PatchMindBlastMask(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t match = 0;
	static constexpr uint8_t RowFilter[8] = {0x41, 0x8b, 0x46, 0x70, 0x89, 0x44, 0x24, 0x28};
	static constexpr uint8_t Pd2Mask[8] = {0xc7, 0x44, 0x24, 0x28, 0x83, 0x85, 0x00, 0x00};
	return sigscan::ScanUnique(context->exeBase, MindBlastMaskArg, 0, &match) == 1 &&
	       context->PatchBytes(match + MindBlastMaskArgAt, RowFilter, sizeof(RowFilter), Pd2Mask, sizeof(Pd2Mask));
}

// --- Leap, Leap Attack, Leap Attack Korlic (PD2 srvdofunc 153 -> 172, cltdofunc 97 -> 115) ------------------------------
// Read from ProjectDiablo.dll: srvdo 153 at 0x103b1300, cltdo 97 at 0x103ab280 (docs/features/core-rules.md). PD2's leap path is
// D2R's (path type 9 to the skill's point); PD2 sets its own speed and, on landing, hits every unit within calc3.

}  // namespace

auto InstallMindBlast(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	size_t used = std::strlen(report);
	bool entries = true;
	for (const sigscan::Entry* e : {&d2r::sig::UnitStateList, &d2r::sig::ListStatValue, &d2r::sig::SetListStat,
	                                &d2r::sig::NewStateList, &d2r::sig::SetStateGfxFlag, &d2r::sig::SkillStateExpired}) {
		entries = entries && e->state == sigscan::State::Ok;
	}
	// the client draw first: alone (the server's redirect failing) it draws the base radius, as no list is ever made
	const bool mindBlast = entries &&
	                       RedirectCall(context, MindBlastDrawCall, MindBlastDrawCallAt,
	                                    reinterpret_cast<uint64_t>(&MindBlastDrawRange),
	                                    reinterpret_cast<uint64_t*>(&g_mindBlastDrawCalc)) &&
	                       RedirectCall(context, MindBlastRangeCall, MindBlastRangeCallAt,
	                                    reinterpret_cast<uint64_t>(&MindBlastRange),
	                                    reinterpret_cast<uint64_t*>(&g_mindBlastRange));
	plog::Info("mind blast: PD2 radius stacks %s", mindBlast ? "on" : "FAILED (srvdo 51 / cltdo 8 call signature or entries)");
	const bool noConvert = PatchMindBlastConvert(context);
	const bool mask = PatchMindBlastMask(context);
	plog::Info("mind blast: damage only, no convert %s; unit mask 0x8583 %s",
	           noConvert ? "on" : "FAILED (srvdo 51 callback signature or bytes)",
	           mask ? "on" : "FAILED (srvdo 51 area call signature or bytes)");
	used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; mind blast stacks%s, no convert%s, mask%s",
		              mindBlast ? "" : " FAILED", noConvert ? "" : " FAILED", mask ? "" : " FAILED");
	}
	int installed = 0;
	for (const bool ok : {mindBlast, noConvert, mask}) {
		installed += ok ? 1 : 0;
	}
	return installed;
}

PD2R_RULE_N(InstallMindBlast, -10, 3)

