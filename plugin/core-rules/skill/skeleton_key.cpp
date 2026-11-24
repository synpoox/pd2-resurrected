// Skeleton Key Unlimited (#2030): PD2 sends 1.13c D2Game's key use (+0xd4140: the first type-41 key in the inventory
// loses one quantity, or is removed at 1) from the chest operate (+0x6f09d) and the locked-door operate (+0x6aa53) to
// ProjectDiablo.dll 0x10382c90 (PD2.gpr 0x10382c20). It walks the player's inventory list and returns 1 for an item
// whose items.txt code is `rkey` on page 0 (inventory) or 3 (cube), using nothing; otherwise it runs 1.13c's key use.
// D2R's key use is 0x4712a0 (game, player) -> 1 when a key was used (D2Game\src\Items\ItemMode.cpp), called by the
// chest operate 0x58e2f0 and the door operate 0x58e090 only; D2RCore jumps neither it nor them. No SDK service answers
// "may this lock open", so its entry is hooked: the rkey test first, then D2R's own key use.
#include "common.h"
#include "registry.h"

#include "hot_log.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"

#include <D2RLPlugin/item.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Game;
using d2r::Unit;

// 0x4712a0 entry through UnitInventory(player, "ItemMode.cpp", 0x8c0)
constexpr const char* KeyUsePattern =
    "48 8b c4 53 55 41 56 48 83 ec 60 48 8b ea 4c 8b f1 48 85 c9 75 11 88 48 08 48 8d 48 08 e8 ?? ?? ?? ?? 84 c0 74 01 cc "
    "48 85 ed 75 1a 48 8d 8c 24 80 00 00 00 40 88 ac 24 80 00 00 00 e8 ?? ?? ?? ?? 84 c0 74 01 cc 48 89 b4 24 88 00 00 00 "
    "48 8d 15 ?? ?? ?? ?? 48 89 bc 24 90 00 00 00 41 b8 c0 08 00 00";
// mov rax,rsp / push rbx / push rbp / push r14 / sub rsp,0x60 / mov rbp,rdx / mov r14,rcx: 7 whole instructions
constexpr uint8_t KeyUseEntry[] = {0x48, 0x8b, 0xc4, 0x53, 0x55, 0x41, 0x56, 0x48, 0x83,
                                   0xec, 0x60, 0x48, 0x8b, 0xea, 0x4c, 0x8b, 0xf1};
const uint32_t SkeletonKey = D2RL::Items::MakeItemCode("rkey");

using KeyUseFn = int64_t (*)(Game*, Unit*) noexcept;
KeyUseFn g_keyUse = nullptr;

auto HoldsSkeletonKey(Unit* player) noexcept -> bool {
	void* inventory = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(d2r::sig::UnitInventory)(player, __FILE__, __LINE__);
	if (inventory == nullptr) {
		return false;
	}
	const auto next = d2r::Engine<Unit* (*)(Unit*) noexcept>(d2r::sig::InventoryNextItem);
	const auto page = d2r::Engine<uint8_t (*)(Unit*) noexcept>(d2r::sig::ItemPage);
	const auto code = d2r::Engine<uint32_t (*)(Unit*) noexcept>(d2r::sig::ItemCode);
	for (Unit* item = d2r::Engine<Unit* (*)(void*) noexcept>(d2r::sig::InventoryFirstItem)(inventory); item != nullptr;
	     item = next(item)) {
		const uint8_t where = page(item);
		if ((where == 0 || where == d2r::rva::CubePageCube) && code(item) == SkeletonKey) {
			return true;
		}
	}
	return false;
}

auto KeyUse(Game* game, Unit* player) noexcept -> int64_t {
	if (game != nullptr && player != nullptr && HoldsSkeletonKey(player)) {
		return 1;
	}
	const int64_t used = g_keyUse(game, player);
	return used;
}

}  // namespace

auto InstallSkeletonKey(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	using sigscan::State;
	const bool on = d2r::sig::UnitInventory.state == State::Ok && d2r::sig::InventoryFirstItem.state == State::Ok &&
	                d2r::sig::InventoryNextItem.state == State::Ok && d2r::sig::ItemPage.state == State::Ok &&
	                d2r::sig::ItemCode.state == State::Ok &&
	                sigscan::HookBySignature(context, "KeyUse", KeyUsePattern, 0, KeyUseEntry, sizeof(KeyUseEntry), &KeyUse,
	                                         &g_keyUse);
	plog::Info("skeleton-key: pd2 unlimited %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; skeleton-key: pd2 unlimited %s", on ? "on" : "FAILED");
	}
	return on;
}

PD2R_RULE(InstallSkeletonKey, 100)
