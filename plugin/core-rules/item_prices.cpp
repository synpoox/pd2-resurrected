// PD2's vendor and gamble prices (#1539, docs/research/items/item-prices.md). D2R's price function (93847: 0x36f0c0,
// Items.cpp) multiplies a price by the stack's quantity unless the item is a book (or a quiver); PD2 skips it for every
// weapon too, so a stack of throwing knives prices as one knife, buying and selling. D2R's gamble price (0x376b10) uses
// the stack midpoint (min + max) / 2 of the base item as a cost factor; PD2 uses 1. Native: no PluginSDK service reaches
// either computation. Both fail safe: a signature that does not match exactly once leaves that rule out.
//
// 1. The quantity step starts at `ItemIsType(item, 0x12)` (0x37069f; the type number is built in r12 + 0x12): non-zero
//    skips it. ItemIsType (0x373890) is hooked, and its answer is changed only for the call whose return address is that
//    site, so every other caller sees D2R's own answer. A book still skips, as before.
// 2. The gamble midpoint is `imul ebp,[rbx+0xe8]` after `max(mid, 1)` (0x376c4d); `mov ebp,[rbx+0xe8]` + nop is mid = 1.
#include "item_prices.h"

#include "item_price.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"

#include <intrin.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Unit;
using namespace pd2::itemprice;

// lea edx,[r12+0x12] / mov rcx,rdi / call ItemIsType / test eax,eax / jnz / mov rcx,rdi / call 0x371e90 / test / jnz /
// mov r12d,[rbp-0x11]: the call is at +12
sigscan::Entry QuantityGuard {"PriceQuantityGuard", "prices", sigscan::Kind::Code, 0, 0,
                              "41 8d 54 24 12 48 8b cf e8 ?? ?? ?? ?? 85 c0 75 ?? 48 8b cf e8 ?? ?? ?? ?? 85 c0 75 ?? 44 8b 65 ef"};
constexpr uint64_t GuardCall = 8;
// ... mov eax,1 / cmp ebp,eax / cmovl ebp,eax / movzx eax,[rbx+0x10d] / imul ebp,[rbx+0xe8] (at +17)
sigscan::Entry GambleMid {"GambleStackMid", "prices", sigscan::Kind::Code, 0, 0,
                          "b8 01 00 00 00 3b e8 0f 4c e8 0f b6 83 0d 01 00 00 0f af ab e8 00 00 00 89 44 24 28"};
constexpr uint64_t MidImul = 17;
constexpr uint8_t MidImulStock[] = {0x0f, 0xaf, 0xab, 0xe8, 0x00, 0x00, 0x00};
constexpr uint8_t MidOne[] = {0x8b, 0xab, 0xe8, 0x00, 0x00, 0x00, 0x90};
// D2R's entry bytes (the SDK checks them): mov [rsp+10h],rbx / mov [rsp+18h],rbp / mov [rsp+20h],rsi / push rdi
constexpr uint8_t IsTypeEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57};
constexpr const char* IsTypePattern = "48 89 5c 24 10 48 89 6c 24 18 48 89 74 24 20 57 41 56 41 57 48 83 ec 20 48 63 f2";

using IsTypeFn = int32_t (*)(Unit* item, int32_t type) noexcept;
IsTypeFn g_isType = nullptr;
uint64_t g_guardReturn = 0;
std::atomic<uint32_t> g_weaponSkips {0};

auto IsTypeHook(Unit* item, int32_t type) noexcept -> int32_t {
	const int32_t result = g_isType(item, type);
	if (result == 0 && type == TypeBook && reinterpret_cast<uint64_t>(_ReturnAddress()) == g_guardReturn &&
	    SkipsQuantity(false, g_isType(item, TypeWeapon) != 0)) {
		g_weaponSkips.fetch_add(1);
		return 1;
	}
	return result;
}

}  // namespace

auto InstallItemPrices(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	sigscan::Entry* const own[] = {&QuantityGuard, &GambleMid};
	sigscan::Resolve(own, 2, context->exeBase, nullptr, 0);
	bool quantity = false, gamble = false;
	uint64_t isTypeRva = 0;
	if (QuantityGuard.state == sigscan::State::Ok && sigscan::ScanUnique(context->exeBase, IsTypePattern, 0, &isTypeRva) == 1) {
		// The call at guard+8 must be the hooked ItemIsType, else the return-address test would never match.
		const auto call = reinterpret_cast<const uint8_t*>(QuantityGuard.address + GuardCall);
		int32_t rel = 0;
		std::memcpy(&rel, call + 1, sizeof(rel));
		if (QuantityGuard.address + GuardCall + 5 + rel == context->exeBase + isTypeRva) {
			g_guardReturn = QuantityGuard.address + GuardCall + 5;
			quantity = sigscan::HookBySignature(context, "ItemIsType", IsTypePattern, 0, IsTypeEntry, sizeof(IsTypeEntry), IsTypeHook,
			                                    &g_isType);
		}
	}
	if (GambleMid.state == sigscan::State::Ok) {
		gamble = context->PatchBytes(GambleMid.rva + MidImul, MidImulStock, sizeof(MidImulStock), MidOne, sizeof(MidOne));
	}
	std::snprintf(report, reportSize, "weapon prices %s, gamble midpoint %s", quantity ? "in" : "FAILED", gamble ? "in" : "FAILED");
	return quantity && gamble;
}

auto ItemPriceCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "item prices: %u prices without the stack quantity", g_weaponSkips.load());
}
