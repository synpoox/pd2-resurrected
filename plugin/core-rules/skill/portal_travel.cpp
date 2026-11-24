// Portals: the act-quest check of a portal's destination covers every portal class, and object 60 (the red portal)
// skips it only into levels that are not maps, as in PD2 (#2021).
//
// 1.13c's portal operate (D2Game +0x6d0f0, objects OperateFn 15) checks the destination's levels.txt QuestFlag
// (QuestFlagEx in expansion) only for object 59, a town portal that is not the player's own: `cmp [op+0x10],0x3b;
// jne` at +0x6d2d2. PD2 NOPs that jne (+0x6d2d6), so every class runs the check, and sends the D2Common #10174 quest
// test at +0x6d314 to installed 0x103714f0 (PD2.gpr 0x10371480, through the thunk 0x103a2680 that adds edi, the
// operate args): for class 60 a level its map test 0x1037c580 rejects (137-210 but 202-206) passes without the test.
// PD2 reads that level from [op+0xc]+4, which is game+0x10f0 (the object control) +4, a byte of the object seed, not
// the destination; we take the destination level, the level the check itself reads (docs/pd2/origin-bugs.md).
// D2R 93847: the portal operate is 0x58f680 (op: game +0, object +8, player +0x10, class +0x20), its class test
// `mov rax,[rbp+0x38]; cmp dword [rax+0x20],0x3b; jne` at 0x58f856, the destination getter called at 0x58f8d8 and
// the quest test (0x325c50) at 0x58f928. The 14 test bytes become a call to a relay stub and a jne to the stock target:
// the stub's `test al,al` on SkipCheck, then rax = op again, as the stock code had it. Only 59 and 60 use OperateFn 15
// in our objects.txt, so the change reaches object 60: Cartographer's map portals and our other red portals. No
// levels.txt row from 137 on has a QuestFlag, so the check passes into every map today. D2RCore holds no copy of the
// function; no SDK service reaches the test.
#include "common.h"

#include "hot_log.h"
#include "log.h"
#include "sig.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

// mov rax,[rbp+0x38]; cmp dword [rax+0x20],0x3b; jne rel32; mov rcx,[rax+8]
constexpr const char* ClassTest = "48 8b 45 38 83 78 20 3b 0f 85 ?? ?? ?? ?? 48 8b 48 08";
constexpr size_t SiteSize = 14;
// call <destination level getter>; movzx edx,ax; ... movzx ecx,byte [rcx+0x106]; call <levels record>; mov rdx,rax
constexpr const char* LevelCall =
	"e8 ?? ?? ?? ?? 0f b7 d0 48 8b 45 38 48 8b 08 0f b6 89 06 01 00 00 e8 ?? ?? ?? ?? 48 8b d0";

using PortalLevelFn = uint16_t (*)(void* object) noexcept;
PortalLevelFn g_level = nullptr;

// PD2's map test 0x1037c580
constexpr auto IsMapLevel(int32_t level) noexcept -> bool {
	return level >= 137 && level <= 210 && (level < 202 || level > 206);
}
static_assert(IsMapLevel(137) && IsMapLevel(201) && !IsMapLevel(202) && !IsMapLevel(206) && IsMapLevel(207));
static_assert(IsMapLevel(210) && !IsMapLevel(136) && !IsMapLevel(211));

// True: no act-quest check (the stock jne to the check's end)
auto SkipCheck(const uint8_t* op) noexcept -> bool {
	int32_t objectClass = 0;
	std::memcpy(&objectClass, op + 0x20, sizeof(objectClass));
	if (objectClass != 60) {
		return false;
	}
	void* object = nullptr;
	std::memcpy(&object, op + 8, sizeof(object));
	const int32_t level = g_level(object);
	const bool skip = !IsMapLevel(level);
	return skip;
}

// A page within rel32 reach of the image (as portal_room_classes.cpp's AllocateRelay), from the far end of the window
auto AllocateRelay(uint64_t image) noexcept -> uint8_t* {
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	const uint64_t step = info.dwAllocationGranularity;
	const uint64_t reach = 0x70000000ull;
	const uint64_t low = image > reach ? image - reach : step;
	for (uint64_t at = (low + step - 1) & ~(step - 1); at < image; at += step) {
		if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
			return static_cast<uint8_t*>(p);
		}
	}
	return nullptr;
}

// rsp is 16-aligned at the site, so the stub's call is aligned after `sub rsp,0x28`. Volatile registers are dead
// there (both paths set rcx, rdx and r8 before use); `mov` keeps the flags of `test`.
auto WriteStub(uint8_t* at) noexcept -> void {
	static constexpr uint8_t Head[] = {0x48, 0x8b, 0x4d, 0x38, 0x48, 0x83, 0xec, 0x28, 0x48, 0xb8};  // mov rcx,[rbp+0x38]; sub rsp,0x28; mov rax,imm64
	static constexpr uint8_t Tail[] = {0xff, 0xd0, 0x48, 0x83, 0xc4, 0x28, 0x84, 0xc0, 0x48, 0x8b, 0x45, 0x38, 0xc3};  // call rax; add rsp,0x28; test al,al; mov rax,[rbp+0x38]; ret
	const uint64_t entry = reinterpret_cast<uint64_t>(&SkipCheck);
	std::memcpy(at, Head, sizeof(Head));
	std::memcpy(at + sizeof(Head), &entry, 8);
	std::memcpy(at + sizeof(Head) + 8, Tail, sizeof(Tail));
}

auto Install(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t site = 0;
	uint64_t levelCall = 0;
	if (sigscan::ScanUnique(context->exeBase, ClassTest, 0, &site) != 1 ||
	    sigscan::ScanUnique(context->exeBase, LevelCall, 0, &levelCall) != 1) {
		return false;
	}
	int32_t rel = 0;
	std::memcpy(&rel, reinterpret_cast<const void*>(context->exeBase + levelCall + 1), sizeof(rel));
	g_level = reinterpret_cast<PortalLevelFn>(context->exeBase + levelCall + 5 + static_cast<int64_t>(rel));
	uint8_t stock[SiteSize];
	std::memcpy(stock, reinterpret_cast<const void*>(context->exeBase + site), sizeof(stock));
	int32_t jneRel = 0;
	std::memcpy(&jneRel, stock + 10, sizeof(jneRel));
	const uint64_t target = site + 14 + static_cast<int64_t>(jneRel);  // the stock jne's target (the check's end)

	uint8_t* relay = AllocateRelay(context->exeBase);
	if (relay == nullptr) {
		return false;
	}
	WriteStub(relay);
	const int64_t callReach = reinterpret_cast<int64_t>(relay) - static_cast<int64_t>(context->exeBase + site + 5);
	const int32_t callRel = static_cast<int32_t>(callReach);
	const int32_t newJneRel = static_cast<int32_t>(static_cast<int64_t>(target) - static_cast<int64_t>(site + 11));
	uint8_t patch[SiteSize] = {0xe8, 0, 0, 0, 0, 0x0f, 0x85, 0, 0, 0, 0, 0x90, 0x90, 0x90};  // call stub; jne target; nop x3
	std::memcpy(patch + 1, &callRel, 4);
	std::memcpy(patch + 7, &newJneRel, 4);
	DWORD old;
	if (callReach != callRel || VirtualProtect(relay, 0x1000, PAGE_EXECUTE_READ, &old) == 0 ||
	    !context->PatchBytes(site, stock, sizeof(stock), patch, sizeof(patch))) {
		VirtualFree(relay, 0, MEM_RELEASE);
		return false;
	}
	return true;
}

}  // namespace

auto InstallPortalTravel(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool on = Install(context);
	plog::Info("portal-travel: act-quest check for every portal %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; portal-travel: act-quest check for every portal %s",
		              on ? "on" : "FAILED");
	}
	return on;
}

#include "registry.h"
PD2R_RULE(InstallPortalTravel, 230)
