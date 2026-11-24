// PD2's vendor buy merge (#1916). 1.13c's buy (D2Game +0xbe640) sends a bought scroll into a held tome: the test
// D2Common #10744(item, 0x16) at +0xbe872, then the finder +0xd1b20 (an inventory page 0 tome, same affixes, quantity
// below its max), then it charges the gold and adds the count to that tome; the store keeps its item. PD2 rewrites both
// calls (ProjectDiablo.dll, PD2.gpr 0x10384d60 and 0x10384f30): the test passes for its list of item types (the bought
// item's primary type, #11088), and the finder looks for a held stack of the bought item's own type (a scroll still
// looks for a tome). Read from the asm: the stack must sit on inventory page 0 (#10020, item data +0x45; not the stash,
// cube, belt or a worn slot), have the same affixes (D2Game +0xd16b0) and a quantity (stat 70) strictly below its max
// (#10463: items.txt maxstack plus stat 254, at most 511). No ethereal test.
//
// D2R 93847 kept the same shape in its server buy handler (0x53d850): ItemIsType(item, 0x16) at 0x53dc0f, the tome finder
// 0x387d00(inventory, item, 0) at 0x53dc3c, then the count (1, or with the fill flag as many as the gold buys up to
// the room), the charge and the add (0x46f090, which tells the buyer's client). So both calls are redirected here as
// PD2 does, and D2R's own code charges and adds. No SDK service fits: the SDK has no buy event, and its item services
// take a PlayerHandle, which the server has none of for a remote buyer. The handler runs on the server, so a joined
// TCP/IP client's buy merges as the host's does.
#include "buy_stacks.h"

#include "item_rules.h"
#include "hot_log.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

using d2r::At;
using d2r::Unit;

// PD2's list (0x10384d60's switch) as PD2 type numbers, which skip the raw table's "Expansion" divider as D2R's do,
// resolved by name and given D2R's number: 0x16 scro (22, D2R's own tome case), 0x76 imma (124), 0x77 imrn (125),
// 0x78 imra (126), 0x79 rera (127), 0x7a scou (128), 0x7b upma (129), 0x7c upmp (130), 199 pvpm (205), 0xcf fort (213),
// 0xe3 pvpd (233). tools/tests/test_buy_stacks.py checks the rows against files/data.
constexpr int32_t ScrollType = 22;
constexpr int32_t MergeTypes[] = {124, 125, 126, 127, 128, 129, 130, 205, 213, 233};
constexpr int32_t StatQuantity = 70;
constexpr int32_t UnitItem = 4;

// From the buy handler: `cmp [rbp+0xe0],al` (gamble) / jne / lea edx,[rax+0x16] / mov rcx,rbx / call ItemIsType /
// ... / mov rdx,rbx / call the tome finder / mov r14,rax (93847 0x53dbfd)
constexpr char BuyPattern[] =
	"38 85 e0 00 00 00 0f 85 ?? ?? ?? ?? 8d 50 16 48 8b cb e8 ?? ?? ?? ?? 85 c0 0f 84 ?? ?? ?? ?? 48 8b 4c 24 58 48 8d 15 "
	"?? ?? ?? ?? 41 b8 ?? ?? 00 00 e8 ?? ?? ?? ?? 48 8b c8 45 33 c0 48 8b d3 e8 ?? ?? ?? ?? 4c 8b f0 48 85 c0";
constexpr uint32_t TypeCallAt = 18, FinderCallAt = 63;
// The buy's room count: UnitStat(stack, 70, 0) / mov rcx,r14 / mov ebx,eax / call GetMaxStack (93847 0x53dc7c, 0x3719e0)
constexpr char MaxStackPattern[] = "45 33 c0 41 8d 57 45 49 8b ce e8 ?? ?? ?? ?? 49 8b ce 8b d8 e8 ?? ?? ?? ?? 8b c8 33 d2 2b cb";
constexpr uint32_t MaxStackCallAt = 20;
// The tome finder's type test: mov rcx,rbx / call GetItemType / cmp eax,0x12 (93847 0x387e40, 0x372c90)
constexpr char ItemTypePattern[] = "48 8b cb e8 ?? ?? ?? ?? 83 f8 12 75 39 33 d2 48 8b cd e8";
constexpr uint32_t ItemTypeCallAt = 3;

using IsTypeFn = int32_t (*)(Unit*, int32_t) noexcept;
using FinderFn = Unit* (*)(void*, Unit*, Unit*) noexcept;
using ItemFn = int32_t (*)(Unit*) noexcept;
IsTypeFn g_isType = nullptr;  // the stock call's target, set by the redirect
FinderFn g_finder = nullptr;
ItemFn g_itemType = nullptr;
ItemFn g_maxStack = nullptr;
std::atomic<uint32_t> g_merges {0};

auto Callee(uint64_t site) noexcept -> uint64_t {
	int32_t rel = 0;
	std::memcpy(&rel, reinterpret_cast<const void*>(site + 1), sizeof(rel));
	return site + 5 + static_cast<int64_t>(rel);
}

auto Listed(Unit* item) noexcept -> bool {
	const int32_t type = g_itemType(item);
	for (const int32_t row : MergeTypes) {
		if (type == row) {
			return true;
		}
	}
	return false;
}

// The buy's scroll test: also true for PD2's types, so the buy goes on to the finder.
auto BuyIsType(Unit* item, int32_t type) noexcept -> int32_t {
	const int32_t stock = g_isType(item, type);
	return stock != 0 || item == nullptr || !Listed(item) ? stock : 1;
}

// The buy's tome finder: a scroll still goes to D2R's; a listed type to the first held stack PD2's finder would take.
// shortcut: same item code stands in for PD2's same type and same affixes; every listed type has one code and no affixes.
auto BuyFinder(void* inventory, Unit* item, Unit* a3) noexcept -> Unit* {
	const auto isType = d2r::Engine<IsTypeFn>(d2r::sig::ItemIsType);
	if (inventory == nullptr || item == nullptr || isType(item, ScrollType) != 0 || !Listed(item)) {
		return g_finder(inventory, item, a3);
	}
	const auto next = d2r::Engine<Unit* (*)(Unit*) noexcept>(d2r::sig::InventoryNextItem);
	const auto page = d2r::Engine<uint8_t (*)(Unit*) noexcept>(d2r::sig::ItemPage);
	const auto code = d2r::Engine<uint32_t (*)(Unit*) noexcept>(d2r::sig::ItemCode);
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat);
	const uint32_t bought = code(item);
	for (Unit* held = d2r::Engine<Unit* (*)(void*) noexcept>(d2r::sig::InventoryFirstItem)(inventory); held != nullptr;
	     held = next(held)) {
		if (At<int32_t>(held, d2r::rva::UnitType) != UnitItem || page(held) != 0 || code(held) != bought) {
			continue;
		}
		const int32_t quantity = stat(held, StatQuantity, 0);
		const int32_t max = g_maxStack(held);
		if ((quantity > 0 ? quantity : 0) < max) {
			++g_merges;
			return held;
		}
	}
	return nullptr;
}

}  // namespace

auto InstallBuyStacks(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	using sigscan::State;
	const uint64_t base = context->exeBase;
	uint64_t maxSite = 0, typeSite = 0;
	const bool found = d2r::sig::ItemIsType.state == State::Ok && d2r::sig::InventoryFirstItem.state == State::Ok &&
	                   d2r::sig::InventoryNextItem.state == State::Ok && d2r::sig::ItemPage.state == State::Ok &&
	                   d2r::sig::ItemCode.state == State::Ok && d2r::sig::UnitStat.state == State::Ok &&
	                   sigscan::ScanUnique(base, MaxStackPattern, 0, &maxSite) == 1 &&
	                   sigscan::ScanUnique(base, ItemTypePattern, 0, &typeSite) == 1;
	if (found) {
		g_maxStack = reinterpret_cast<ItemFn>(Callee(base + maxSite + MaxStackCallAt));
		g_itemType = reinterpret_cast<ItemFn>(Callee(base + typeSite + ItemTypeCallAt));
	}
	// the finder first: with the test alone a listed item would reach D2R's tome finder
	const bool ok = found &&
	                RedirectCall(context, BuyPattern, FinderCallAt, reinterpret_cast<uint64_t>(&BuyFinder),
	                             reinterpret_cast<uint64_t*>(&g_finder)) &&
	                RedirectCall(context, BuyPattern, TypeCallAt, reinterpret_cast<uint64_t>(&BuyIsType),
	                             reinterpret_cast<uint64_t*>(&g_isType));
	std::snprintf(report, reportSize, "buy merges stacks%s", ok ? "" : found ? " FAILED (call sites)" : " FAILED (signature)");
	return ok;
}

auto BuyStackCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "buy stacks: %u merges", g_merges.load());
}
