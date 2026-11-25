// Stash cells, moves and quantities for the stash affinity watch.
// Moved verbatim from stash_affinity.cpp (#2198).
#include "shared.h"

namespace stash_affinity {

namespace detail {

auto InCell(int32_t x, int32_t y) noexcept -> bool {
	return x >= 0 && x < aff::PageWidth && y >= 0 && y < aff::PageWidth;
}

auto Reserved(const items::ItemInfo& item) noexcept -> bool {
	return item.container == C::SharedStash && item.sharedStashPage != UINT32_MAX &&
	       item.sharedStashPage >= aff::FirstReservedPage && item.sharedStashPage <= aff::LastReservedPage;
}

// A fixed-cell page's (Maps, Bossing) cell, in the pass's map of items
auto At(uint32_t page, int32_t row, int32_t column) noexcept -> int32_t& {
	return g_pass.at[page == aff::BossingPage ? 1 : 0][row][column];
}

auto Used(uint32_t page, int32_t x, int32_t y) noexcept -> bool {
	return (g_pass.used[page - aff::FirstReservedPage][y] & (1U << x)) != 0;
}

void Use(uint32_t page, int32_t x, int32_t y, bool used) noexcept {
	uint32_t& row = g_pass.used[page - aff::FirstReservedPage][y];
	row = used ? row | (1U << x) : row & ~(1U << x);
}

auto NoteInventory(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* list = static_cast<NotedList*>(user);
	if (aff::HomeOf(item->code).category != aff::Category::None && list->count < MaxNoted) {
		list->item[list->count++] = {item->runtimeId, item->code, item->handle};
	}
	return D2RL::Inventory::IterationAction::Continue;
}

auto ReadStash(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* pass = static_cast<Pass*>(user);
	if (Reserved(*item) && InCell(item->x, item->y) && pass->reservedCount < MaxReserved) {
		Use(item->sharedStashPage, item->x, item->y, true);
		if ((item->sharedStashPage == aff::MapsPage || item->sharedStashPage == aff::BossingPage) && InCell(item->x, item->y)) {
			At(item->sharedStashPage, item->y, item->x) = static_cast<int32_t>(pass->reservedCount);
		}
		pass->reserved[pass->reservedCount++] = *item;
	}
	for (uint32_t i = 0; i < pass->gone.count; ++i) {
		if (pass->gone.item[i].runtimeId == item->runtimeId && pass->gone.item[i].code == item->code &&
		    pass->foundCount < MaxNoted) {
			pass->found[pass->foundCount++] = *item;
			break;
		}
	}
	return D2RL::Inventory::IterationAction::Continue;
}

auto Move(D2RL::PlayerHandle player, D2RL::ItemHandle item, const items::ItemDestination& to) noexcept -> items::Result {
	items::ExistingItemOperation op {};
	op.structSize = items::ExistingItemOperationSize;
	op.kind = items::ExistingItemOperationKind::Move;
	op.item = item;
	op.move.destination = to;
	const items::ExistingItemTransaction transaction {
		.structSize = items::ExistingItemTransactionSize, .player = player, .operationCount = 1, .operations = &op};
	items::ExistingItemTransactionResult result {.structSize = items::ExistingItemTransactionResultSize};
	return g_items->executeExistingItemTransaction(g_context, &transaction, &result);
}

// To a shared page: a cell (x, y), or anywhere with x -1. The pass's cells and the item's place follow the move.
auto MoveTo(D2RL::PlayerHandle player, items::ItemInfo& item, uint32_t page, int32_t x, int32_t y) noexcept -> items::Result {
	items::ItemDestination to {.structSize = items::ItemDestinationSize, .container = C::SharedStash,
	                           .placement = items::Placement::Automatic, .sharedStashPage = page};
	if (x >= 0) {
		to.placement = items::Placement::Exact;
		to.x = static_cast<uint32_t>(x);
		to.y = static_cast<uint32_t>(y);
	}
	const items::Result moved = Move(player, item.handle, to);
	if (moved != items::Result::Success) {
		return moved;
	}
	if (Reserved(item) && InCell(item.x, item.y)) {
		Use(item.sharedStashPage, item.x, item.y, false);
		if (item.sharedStashPage == aff::MapsPage || item.sharedStashPage == aff::BossingPage) {
			At(item.sharedStashPage, item.y, item.x) = -1;  // free for an item later in this pass
		}
	}
	if (x >= 0) {
		Use(page, x, y, true);
	}
	item.container = C::SharedStash;
	item.sharedStashPage = page;
	item.x = x;
	item.y = y;  // -1, -1 after a move anywhere: its cell is not known, so it is never freed this pass
	return moved;
}

// The first free map cell of the Maps page; {-1, -1} when they are all used
auto FreeMapCell() noexcept -> aff::Cell {
	for (int32_t i = 0; i < aff::MapViewCells; ++i) {
		if (const aff::Cell c = aff::MapView(i); !Used(aff::MapsPage, c.x, c.y)) {
			return c;
		}
	}
	return {};
}

// A map to its base's row on its tier's storage page, or anywhere on that page when the row is full
auto ToStorage(D2RL::PlayerHandle player, items::ItemInfo& item) noexcept -> items::Result {
	const aff::Home home = aff::HomeOf(item.code);
	const int32_t column = aff::FreeColumn(g_pass.used[home.page - aff::FirstReservedPage][home.row]);
	return MoveTo(player, item, home.page, column, column >= 0 ? home.row : -1);
}

// An item's count: stat 70, read off the native item. Loader 1.3.1's ItemInfo.quantity reads 0 for PD2's map currency
// and boss parts (#1628: a stack the game draws as 2 read q0), and no SDK call merges two stacks (item.h: Debit, Edit,
// Move, splitStack), so Merge sets the sum with editItem. 0 when the read fails.
void ReadQuantity(const D2RL::PluginContext*, void* native, void* user) noexcept {
	*static_cast<int32_t*>(user) = d2r::Engine<int32_t (*)(d2r::Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(
	    static_cast<d2r::Unit*>(native), d2r::rva::StatQuantity, 0);
}

auto Quantity(D2RL::ItemHandle item) noexcept -> int32_t {
	int32_t count = 0;
	return g_items->editNativeItem(g_context, item, ReadQuantity, &count) == items::Result::Success ? count : 0;
}

void WriteQuantity(const D2RL::PluginContext*, void* native, void* user) noexcept {
	d2r::Engine<void (*)(d2r::Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::sig::SetUnitStat)(
	    static_cast<d2r::Unit*>(native), d2r::rva::StatQuantity, *static_cast<int32_t*>(user), 0);
}

// Sets a stack's count, verified by the stat. editItem refuses a count past the item's maxstack (Slot5, #1763: result 2
// for 50 + 50), and no SDK call sets more, so a stash stack past 50 gets stat 70 natively (SetUnitStat, by signature)
// through editNativeItem; the caller's next move of the item publishes it to the client.
auto SetQuantity(D2RL::PlayerHandle player, D2RL::ItemHandle item, int32_t count) noexcept -> items::Result {
	const items::ItemEdit edit {.structSize = items::ItemEditSize, .fields = items::EditFieldBit(items::EditField::Quantity),
	                            .quantity = static_cast<uint32_t>(count)};
	if (g_items->editItem(g_context, player, item, &edit) == items::Result::Success && Quantity(item) == count) {
		return items::Result::Success;
	}
	if (d2r::sig::SetUnitStat.rva == 0 || g_items->editNativeItem(g_context, item, WriteQuantity, &count) != items::Result::Success) {
		return items::Result::Unsupported;
	}
	return Quantity(item) == count ? items::Result::Success : items::Result::Unsupported;
}

}  // namespace detail

}  // namespace stash_affinity
