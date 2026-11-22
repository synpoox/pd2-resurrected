// PD2's vendor and gamble prices (#1539, #1625, docs/research/items/item-prices.md). D2R's price function (93847: 0x36f0c0,
// Items.cpp) differs from PD2's in four ways. Native: no PluginSDK service reaches any of these computations. Every rule
// fails safe: a signature that does not match exactly once leaves that rule out.
//
// 1. The quantity step starts at `ItemIsType(item, 0x12)` (0x37069f; the type number is built in r12 + 0x12): non-zero
//    skips it. ItemIsType (0x373890) is hooked, and its answer is changed only for the call whose return address is that
//    site, so every other caller sees D2R's own answer. A book still skips, as before; PD2 skips every weapon too, so a
//    stack of throwing knives prices as one knife, buying and selling.
// 2. The gamble price (0x376b10) uses the stack midpoint (min + max) / 2 of the base item as a cost factor; PD2 uses 1.
//    The midpoint is `imul ebp,[rbx+0xe8]` after `max(mid, 1)` (0x376c4d); `mov ebp,[rbx+0xe8]` + nop is mid = 1.
// 3. Superior items skip the stat term: the price switch on quality sends 3 (superior) and 9 (tempered) to one label that
//    calls the bonus-stat pricer (0x37c390, a D2RLoader thunk, so the call site is the place). The switch table (0x3709dc,
//    RVAs) entry for 3 is moved past that call (+27); 9 keeps it. The code after the call reads the three price slots in
//    place, so skipping it adds nothing, as PD2's `if (quality != 3)`.
// 4. A repair quote (type 3) is capped at 250000, as PD2's final `min(price, 250000)`: the price function is hooked and its
//    result clamped for type 3.
// 5. Runes can't be sold (#1777). ItemIsSellable (0x374370, D2Common Items.cpp: not item flag 0x1000, not a quest item,
//    not type 0x27) is hooked to answer 0 for an item of the itemtypes `rune` type (its row found by code through the
//    SDK's DataTableService). Every caller is a sell path: the vendor panel, its tooltip and controller prompts (client),
//    and the SUnitNpc.cpp sell handlers (server). So the client refuses as it does for a quest item and the rune stays
//    where it was, and a host refuses a sell message too. Native: no PluginSDK service decides what a vendor takes.
#include "item_prices.h"

#include "item_price.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"

#include <D2RLPlugin/data_tables.h>

#include <windows.h>

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

// mov eax,[rbp-0x2d] / dec eax / cmp eax,8 / ja / lea rdx,[image base] / cdqe / mov ecx,[rdx+rax*4+TABLE] / add rcx,rdx / jmp rcx:
// the table's RVA is the disp32 at +26
constexpr const char* QualityDispatch =
    "8b 45 d3 ff c8 83 f8 08 0f 87 ?? ?? ?? ?? 48 8d 15 ?? ?? ?? ?? 48 98 8b 8c 82 ?? ?? ?? ?? 48 03 ca ff e1";
constexpr uint64_t TableDisp = 26;
// the label for superior and tempered: mov eax,[rbp-0x25] / lea r9,.. / lea r8,.. / mov [rsp+20h],eax / lea rdx,.. / mov rcx,rdi /
// call (+22) ; the code after the call starts `mov eax,[rbp-0x35]` (+27)
constexpr uint8_t StatLabel[] = {0x8b, 0x45, 0xdb, 0x4c, 0x8d, 0x4d, 0xd7, 0x4c, 0x8d, 0x45, 0xcb, 0x89, 0x44, 0x24, 0x20,
                                 0x48, 0x8d, 0x55, 0xcf, 0x48, 0x8b, 0xcf, 0xe8};
constexpr uint64_t StatSkip = 27;
constexpr uint8_t AfterCall[] = {0x8b, 0x45, 0xcb};

// 0x36f0c0 (player, item, difficulty, quest flags, vendor, type): type 0 buy, 1 sell, 2 gamble, 3 repair
constexpr uint8_t PriceEntry[] = {0x4c, 0x89, 0x4c, 0x24, 0x20, 0x44, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4c, 0x24, 0x08};
constexpr const char* PricePattern =
    "4c 89 4c 24 20 44 89 44 24 18 48 89 54 24 10 48 89 4c 24 08 55 57 41 55 48 8d 6c 24 c9 48 81 ec b0 00 00 00 48 8b fa 4c 8b e9";
using PriceFn = uint64_t (*)(void* player, Unit* item, int32_t difficulty, uint64_t quest, uint32_t vendor, int32_t type) noexcept;
PriceFn g_price = nullptr;
std::atomic<uint32_t> g_repairCaps {0};
std::atomic<int32_t> g_repairMax {0};  // the highest repair quote seen before the cap, for the console counters

auto PriceHook(void* player, Unit* item, int32_t difficulty, uint64_t quest, uint32_t vendor, int32_t type) noexcept -> uint64_t {
	uint64_t result = g_price(player, item, difficulty, quest, vendor, type);
	if (type == 3) {
		const int32_t quote = static_cast<int32_t>(result);
		int32_t seen = g_repairMax.load();
		while (quote > seen && !g_repairMax.compare_exchange_weak(seen, quote)) {
		}
	}
	if (type == 3 && static_cast<int32_t>(result) != CapRepair(static_cast<int32_t>(result))) {
		g_repairCaps.fetch_add(1);
		result = RepairCap;
	}
	return result;
}

// Moves the switch table's entry for superior (3) past the stat call. Fails safe: one match, the two entries equal, and the
// label's bytes exactly as read.
auto InstallSuperiorStats(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t rva = 0;
	if (sigscan::ScanUnique(context->exeBase, QualityDispatch, 0, &rva) != 1) {
		plog::Warn("skipped superior stat term: dispatch not found exactly once");
		return false;
	}
	int32_t table = 0;
	std::memcpy(&table, reinterpret_cast<const uint8_t*>(context->exeBase + rva + TableDisp), sizeof(table));
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(context->exeBase + reinterpret_cast<const IMAGE_DOS_HEADER*>(context->exeBase)->e_lfanew);
	const uint64_t imageSize = nt->OptionalHeader.SizeOfImage;
	if (table < 0 || static_cast<uint64_t>(table) + sizeof(int32_t) * 9 > imageSize) {
		plog::Warn("skipped superior stat term: switch table outside the image");
		return false;
	}
	int32_t entries[9];
	std::memcpy(entries, reinterpret_cast<const uint8_t*>(context->exeBase + table), sizeof(entries));
	if (entries[2] < 0 || static_cast<uint64_t>(entries[2]) + StatSkip + sizeof(AfterCall) > imageSize) {
		plog::Warn("skipped superior stat term: label outside the image");
		return false;
	}
	const auto label = reinterpret_cast<const uint8_t*>(context->exeBase + entries[2]);
	if (entries[2] != entries[8] || std::memcmp(label, StatLabel, sizeof(StatLabel)) != 0 ||
	    std::memcmp(label + StatSkip, AfterCall, sizeof(AfterCall)) != 0) {
		plog::Warn("skipped superior stat term: switch table or label differs");
		return false;
	}
	const int32_t skip = entries[2] + static_cast<int32_t>(StatSkip);
	return context->PatchBytes(static_cast<uint64_t>(table) + 2 * sizeof(int32_t), &entries[2], sizeof(int32_t), &skip, sizeof(skip));
}

// push rdi / sub rsp,20h / mov rdi,rcx / test rcx,rcx (the SDK checks them); the pattern runs to `and eax,0x1000`
constexpr uint8_t SellableEntry[] = {0x40, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf9, 0x48, 0x85, 0xc9};
constexpr const char* SellablePattern =
    "40 57 48 83 ec 20 48 8b f9 48 85 c9 74 0a e8 ?? ?? ?? ?? 83 f8 04 74 19 48 8d 4c 24 30 c6 44 24 30 00 e8 ?? ?? ?? ?? "
    "84 c0 74 01 cc 48 85 ff 74 39 48 8b cf e8 ?? ?? ?? ?? 83 f8 04 75 2c 48 8b cf e8 ?? ?? ?? ?? 48 85 c0 75 15 48 8d 4c "
    "24 30 88 44 24 30 e8 ?? ?? ?? ?? 84 c0 74 0d cc eb 0a 8b 40 18 25 00 10 00 00";
constexpr uint32_t RuneCode = 'r' | 'u' << 8 | 'n' << 16 | 'e' << 24;  // itemtypes code `rune`, as D2R keys it
using SellableFn = int32_t (*)(Unit* item) noexcept;
SellableFn g_sellable = nullptr;
IsTypeFn g_typeTest = nullptr;  // ItemIsType's entry (through the hook above when it is in; it keeps these answers)
const D2RL::PluginContext* g_context = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
std::atomic<int32_t> g_runeType {-1};
std::atomic<uint32_t> g_runeRefusals {0};

// The `rune` row, looked up on the first sell check (the SDK answers only on the game thread, where the checks run)
auto RuneType() noexcept -> int32_t {
	int32_t type = g_runeType.load(std::memory_order_relaxed);
	if (type < 0) {
		D2RL::DataTables::RowView view {.structSize = D2RL::DataTables::RowViewSize};
		if (g_tables->findRowByCode(g_context, D2RL::DataTables::Bank::Rotw, D2RL::DataTables::TableId::ItemTypes, RuneCode, &view) ==
		    D2RL::DataTables::Result::Success) {
			type = static_cast<int32_t>(view.rowIndex);
			g_runeType.store(type);
			plog::Info("rune sales: itemtypes rune is row %d", type);
		}
	}
	return type;
}

auto SellableHook(Unit* item) noexcept -> int32_t {
	const int32_t result = g_sellable(item);
	if (result != 0) {
		const int32_t rune = RuneType();
		if (rune >= 0 && g_typeTest(item, rune) != 0) {
			g_runeRefusals.fetch_add(1);
			return 0;
		}
	}
	return result;
}

auto InstallRuneSales(const D2RL::PluginContext* context, uint64_t isTypeRva) noexcept -> bool {
	if (isTypeRva == 0 || context->QueryService(&g_tables) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasDataTableServiceField(g_tables, D2RL::DataTableServiceRequiredSize)) {
		plog::Warn("skipped rune sales: no ItemIsType or no data table service");
		return false;
	}
	g_context = context;
	g_typeTest = reinterpret_cast<IsTypeFn>(context->exeBase + isTypeRva);
	return sigscan::HookBySignature(context, "ItemIsSellable", SellablePattern, 0, SellableEntry, sizeof(SellableEntry), SellableHook,
	                                &g_sellable);
}

}  // namespace

auto InstallItemPrices(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	sigscan::Entry* const own[] = {&QuantityGuard, &GambleMid};
	sigscan::Resolve(own, 2, context->exeBase, nullptr, 0);
	bool quantity = false, gamble = false;
	uint64_t isTypeRva = 0;
	if (sigscan::ScanUnique(context->exeBase, IsTypePattern, 0, &isTypeRva) != 1) {
		isTypeRva = 0;
	}
	if (QuantityGuard.state == sigscan::State::Ok && isTypeRva != 0) {
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
	const bool superior = InstallSuperiorStats(context);
	const bool repair = sigscan::HookBySignature(context, "ItemPrice", PricePattern, 0, PriceEntry, sizeof(PriceEntry), PriceHook, &g_price);
	const bool runes = InstallRuneSales(context, isTypeRva);
	std::snprintf(report, reportSize, "weapon prices %s, gamble midpoint %s, superior stat skip %s, repair cap %s, rune sales block %s",
	              quantity ? "in" : "FAILED", gamble ? "in" : "FAILED", superior ? "in" : "FAILED", repair ? "in" : "FAILED",
	              runes ? "in" : "FAILED");
	return quantity && gamble && superior && repair && runes;
}

auto ItemPriceCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "item prices: %u prices without the stack quantity, %u repair quotes capped, highest repair quote %d, %u rune sales refused (rune type %d)",
	              g_weaponSkips.load(), g_repairCaps.load(), g_repairMax.load(), g_runeRefusals.load(), g_runeType.load());
}
