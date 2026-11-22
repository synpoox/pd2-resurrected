// Socketed gems and runes apply their gems.txt mods once (#450).
//
// Our merged misc.txt gives every gem and rune compactsave=0 (PD2's stacks need it). D2R then keeps the filler's own
// stat list as well as applying gems.txt, so the mods count twice on insertion and a third time after a reload.
// A socketed filler is always one item and never comes out, so after its mods are applied it gets the item flag
// compact (0x200000): the save and the item packet then write it without stats, as vanilla's compactsave=1 does.
// The SDK has no service for item flags or this function, hence the native hook (pd2_engine.h).
//
// Throwing weapons and quivers roll sockets (#1828). D2R's socket roll (93847: 0x441fc0) returns when the stackable
// test 0x374660 says yes (call at 0x441ffc). PD2 (ProjectDiablo.dll 0x10374be0, over 1.13c D2Game 0xec90) runs the roll
// when the item is not stackable or its own items.txt type (D2Common #11088, an exact compare) is one of six: 1.13c
// 42, 43, 44, 87, 5, 6. That one call is redirected (RedirectCall) to StackableNoSockets: no SDK service reaches the
// socket roll.
#include "socket_rules.h"

#include "item_rules.h"
#include "pd2_engine.h"
#include "log.h"

#include <D2RLPlugin/data_tables.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <iterator>

namespace {

using d2r::At;
using d2r::Unit;

std::atomic<uint32_t> g_marked {0};

using SocketModsFn = void (*)(uint8_t dataFlag, Unit* filler, Unit* parent, int32_t arg) noexcept;
SocketModsFn g_original = nullptr;

auto IsItem(Unit* unit) noexcept -> bool {
	return unit != nullptr && At<int32_t>(unit, d2r::rva::UnitType) == 4;
}

void SocketMods(uint8_t dataFlag, Unit* filler, Unit* parent, int32_t arg) noexcept {
	g_original(dataFlag, filler, parent, arg);
	// Only an item parent: a jewel (its own rolled stats) never matches the type test.
	if (IsItem(parent)) {
		MarkSocketedCompact(filler);
	}
}

// mov rcx,rdi / call socketable 0x373610 / test / jz / mov rcx,rdi / call stackable 0x374660 (+28) / test / jnz /
// mov rcx,rdi / call max sockets 0x36ead0 / test al,al / jz, after the item quality >= 2 test
constexpr const char* StackableCallPattern =
    "83 f8 02 0f 8c ?? ?? ?? ?? 48 8b cf e8 ?? ?? ?? ?? 85 c0 0f 84 ?? ?? ?? ?? 48 8b cf e8 ?? ?? ?? ?? 85 c0 0f 85 ?? ?? ?? ?? "
    "48 8b cf e8 ?? ?? ?? ?? 84 c0 0f 84";
constexpr uint32_t StackableCallAt = 28;
// itemtypes codes of PD2's six 1.13c types: tkni 42, taxe 43, jave 44, ajav 87, bowq 5, xboq 6
constexpr uint32_t SocketTypes[] = {D2RL::DataTables::MakeFourCC('t', 'k', 'n', 'i'), D2RL::DataTables::MakeFourCC('t', 'a', 'x', 'e'),
                                    D2RL::DataTables::MakeFourCC('j', 'a', 'v', 'e'), D2RL::DataTables::MakeFourCC('a', 'j', 'a', 'v'),
                                    D2RL::DataTables::MakeFourCC('b', 'o', 'w', 'q'), D2RL::DataTables::MakeFourCC('x', 'b', 'o', 'q')};
constexpr int Banks = 4;  // DataTables::Bank 1-3, indexed by the item's data flag

using StackableFn = int32_t (*)(Unit* item) noexcept;
using ItemRecordFn = const uint8_t* (*)(uint32_t dataFlag, int32_t classId) noexcept;
StackableFn g_stackable = nullptr;
const D2RL::PluginContext* g_context = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
int32_t g_typeRows[Banks][std::size(SocketTypes)] = {};  // row + 1, 0 = not looked up yet, -1 = not in the bank (game thread only)
std::atomic<uint32_t> g_stackRolls {0};

// The itemtypes row of SocketTypes[i] in the bank (-1: not there), looked up on first use; only Busy (off the game
// thread) is asked again
auto TypeRow(uint8_t bank, size_t i) noexcept -> int32_t {
	int32_t& row = g_typeRows[bank][i];
	if (row == 0) {
		D2RL::DataTables::RowView view {.structSize = D2RL::DataTables::RowViewSize};
		const auto result = g_tables->findRowByCode(g_context, static_cast<D2RL::DataTables::Bank>(bank),
		                                            D2RL::DataTables::TableId::ItemTypes, SocketTypes[i], &view);
		if (result == D2RL::DataTables::Result::Busy) {
			return -1;
		}
		row = result == D2RL::DataTables::Result::Success ? static_cast<int32_t>(view.rowIndex) + 1 : -1;
	}
	return row < 0 ? -1 : row - 1;
}

// D2R's stackable answer, but 0 (not stackable: the socket roll goes on) for an item whose own type is one of the six
auto StackableNoSockets(Unit* item) noexcept -> int32_t {
	const int32_t stackable = g_stackable(item);
	if (stackable == 0) {
		return 0;
	}
	const uint8_t bank = d2r::Engine<uint8_t (*)(Unit*) noexcept>(d2r::sig::UnitDataFlag)(item);
	const uint8_t* record = bank < Banks ? d2r::Engine<ItemRecordFn>(d2r::sig::ItemRecord)(bank, At<int32_t>(item, d2r::rva::UnitClassId))
	                                     : nullptr;
	if (record == nullptr) {
		return stackable;
	}
	const int16_t type = At<int16_t>(record, d2r::rva::ItType);
	for (size_t i = 0; i < std::size(SocketTypes); ++i) {
		if (TypeRow(bank, i) == type) {
			const uint32_t n = ++g_stackRolls;
			if (n <= 20 || n % 500 == 0) {
				plog::Info("stackable sockets: bank %u item type row %d rolls sockets, %u so far", bank, type, n);
			}
			return 0;
		}
	}
	return stackable;
}

auto InstallStackableSockets(const D2RL::PluginContext* context) noexcept -> bool {
	if (d2r::sig::UnitDataFlag.state != sigscan::State::Ok || d2r::sig::ItemRecord.state != sigscan::State::Ok ||
	    context->QueryService(&g_tables) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasDataTableServiceField(g_tables, D2RL::DataTableServiceRequiredSize)) {
		return false;
	}
	g_context = context;
	return RedirectCall(context, StackableCallPattern, StackableCallAt, reinterpret_cast<uint64_t>(&StackableNoSockets),
	                    reinterpret_cast<uint64_t*>(&g_stackable));
}

}  // namespace

auto MarkSocketedCompact(Unit* filler) noexcept -> bool {
	using namespace d2r::rva;
	if (!IsItem(filler)) {
		return false;
	}
	const auto isType = d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType);
	if (isType(filler, ItemTypeGem) == 0 && isType(filler, ItemTypeRune) == 0) {
		return false;
	}
	auto* data = d2r::Engine<uint8_t* (*)(Unit*) noexcept>(UnitItemData)(filler);
	if (data == nullptr) {
		return false;
	}
	auto& flags = *reinterpret_cast<uint32_t*>(data + ItemDataFlags);
	if ((flags & ItemFlagCompactSave) != 0) {
		return false;
	}
	flags |= ItemFlagCompactSave;
	plog::Sample(g_marked);
	return true;
}

auto InstallSocketRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	const bool hook = context->InstallInlineHook(SocketFillerMods, SocketFillerModsEntry, sizeof(SocketFillerModsEntry),
	                                             SocketMods, &g_original);
	const bool stackable = InstallStackableSockets(context);
	std::snprintf(report, reportSize, "socket mods once%s; stackable sockets%s", hook ? "" : " FAILED (bytes differ)",
	              stackable ? "" : " FAILED (signature, service or relay)");
	return (hook ? 1 : 0) + (stackable ? 1 : 0);
}

auto SocketRuleCount() noexcept -> int {
	return 2;
}

auto SocketRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "socketed fillers marked compact %u, stackables let roll sockets %u", g_marked.load(),
	              g_stackRolls.load());
}
