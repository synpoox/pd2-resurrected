// Routing a quick-moved item home, the withdraw, swap and tidy passes.
// Moved verbatim from stash_affinity.cpp (#2198).
#include "shared.h"

namespace stash_affinity {

namespace detail {

// A fixed cell's stack tops up to its cap and the rest stays on the item, in the inventory (Withdraw #1763, quick-move #1771)
auto Route(D2RL::PlayerHandle player, items::ItemInfo item) noexcept -> bool {
	const aff::Home home = aff::HomeOf(item.code);
	const bool atHome = aff::AtHome(home, item.code, g_shown, item.container == C::SharedStash, item.sharedStashPage, item.x, item.y);
	if (atHome && home.category != aff::Category::Maps) {
		return true;
	}
	items::Result moved = items::Result::Conflict;
	bool full = false;  // the shown base's Maps view has no free cell (#2095)
	if (home.category == aff::Category::Maps) {
		const bool shown = g_shown != 0 && aff::Normal(item.code) == aff::Normal(g_shown);
		// A stack of the same map below a full one takes it first (#1759), also a map quick-moved onto a free cell of its
		// own page, which AtHome already calls home (#2096); a full stack or a failed merge falls through
		const uint32_t stackPage = shown ? aff::MapsPage : home.page;
		for (uint32_t i = 0; i < g_pass.reservedCount && moved != items::Result::Success; ++i) {
			items::ItemInfo& stack = g_pass.reserved[i];
			if (stack.code != 0 && stack.runtimeId != item.runtimeId && stack.container == C::SharedStash &&
			    aff::Normal(stack.code) == aff::Normal(item.code) &&
			    MapRefusal(item, stack, stackPage, aff::InMapView(stack.x, stack.y), shown) == nullptr) {
				moved = Merge(player, item, stack);
			}
		}
		if (moved == items::Result::Success) {
			SyncReserved(item);
			++g_routed;
			return true;
		}
		if (atHome && item.container == C::SharedStash) {  // a merge that failed after its take step left it in the inventory
			return true;
		}
		const aff::Cell cell = shown ? FreeMapCell() : aff::Cell {};
		// #2095: with the shown base's view full the map stays in the inventory (below); its storage row is not on screen, so it looked deleted
		if (cell.x >= 0) {
			moved = MoveTo(player, item, aff::MapsPage, cell.x, cell.y);
		} else if (!shown) {
			moved = ToStorage(player, item);
		} else {
			full = true;
			moved = items::Result::Unsupported;  // no page problem: skips the add_shared_tabs hint below
		}
	} else {  // a fixed cell: Map Materials, Bossing
		int32_t& on = At(home.page, home.row, home.column);
		if (on == -1) {
			moved = MoveTo(player, item, home.page, home.column, home.row);
			if (moved == items::Result::Success) {
				on = -2;  // used by an item this pass did not read
			}
		} else if (on >= 0 && aff::Stacks(item.code) && aff::Normal(g_pass.reserved[on].code) == aff::Normal(item.code)) {
			moved = Merge(player, item, g_pass.reserved[on], true);
		}
	}
	if (moved == items::Result::Success) {
		SyncReserved(item);
		++g_routed;
		return true;
	}
	// The page or cell is full or missing (add_shared_tabs below aff::RequiredExtraTabs): back to the inventory
	const items::ItemDestination back {.structSize = items::ItemDestinationSize, .container = C::Inventory,
	                                   .placement = items::Placement::Automatic};
	const items::Result returned = Move(player, item.handle, back);
	(returned == items::Result::Success ? g_returned : g_failed)++;
	if (full && returned == items::Result::Success && g_failLogged.First(pd2::Key(item.runtimeId, 0xFFFFu))) {  // once per item
		plog::Info("affinity: the Maps tab is full: %08x stays in the inventory", item.code);
	}
	// a stuck item retries about once a second: one line per item and page (#1705, #2094)
	if (g_failLogged.First(pd2::Key(item.runtimeId, home.page))) {
		plog::Info("affinity: %08x could not go to page %u (result %u); back to the inventory: result %u", item.code, home.page,
		           static_cast<uint32_t>(moved), static_cast<uint32_t>(returned));
	}
	// Unsupported: a stack count that could not be read; a page the stash has is full or its cell taken, not missing (#1705)
	if (moved != items::Result::Unsupported && !stash_tabs::HasPage(home.page) && !g_hinted.exchange(true)) {
		plog::Info("affinity: the reserved pages are shared tabs %u-%u; set [d2rcore.stash] add_shared_tabs to at least %u",
		           aff::FirstReservedPage, aff::LastReservedPage, aff::RequiredExtraTabs);
	}
	return false;
}

// #2179: a stack past WithdrawMax that came into the inventory by a quick-move (Ctrl+click, Shift+right click) is not a hand
// take: WithdrawMax of it must stay in the inventory, not on the cursor. The cursor cannot be moved by the SDK, so the
// rest is made as a new stack (createItem; these currencies and boss parts carry no rolls) and sent home, and the source
// is cut to WithdrawMax. False with everything put back when a step fails: Withdraw's cursor split then runs. True also when
// a failed undo left a state that could not be restored (logged), so the cursor split never runs on top of it.
auto QuickWithdraw(D2RL::PlayerHandle player, const items::ItemInfo& item, int32_t had) noexcept -> bool {
	const items::ItemCreateSpec spec {.structSize = items::ItemCreateSpecSize,
	                                  .code = item.code,
	                                  .quality = items::Quality::Normal,
	                                  .qualityRecordId = items::RandomQualityRecord,
	                                  .itemLevel = item.itemLevel,
	                                  .seedMode = items::SeedMode::Random,
	                                  .quantity = 1,
	                                  .durability = items::DefaultValue,
	                                  .stateFlags = items::ItemStateIdentified,
	                                  .destination = {.structSize = items::ItemDestinationSize, .container = C::Inventory,
	                                                  .placement = items::Placement::Automatic}};
	D2RL::ItemHandle made = D2RL::InvalidItemHandle;
	if (g_items->createItem(g_context, player, &spec, &made) != items::Result::Success) {
		return false;
	}
	// Put back without making or losing items: the source first, the new stack only once the source reads back `had`; a
	// failed destroy leaves the new stack, so the source gives up what it holds. A state that cannot be put back is logged
	// and counts as handled (true), so the cursor split never runs on top of it.
	const auto undo = [&] {
		const bool source = SetQuantity(player, item.handle, had) == items::Result::Success && Quantity(item.handle) == had;
		if (source && g_items->destroyItem(g_context, player, made, items::SocketedItemPolicy::RejectIfNotEmpty) == items::Result::Success) {
			return false;
		}
		const int32_t kept = Quantity(made);
		if (source && kept > 0 && SetQuantity(player, item.handle, had - kept) == items::Result::Success) {
			plog::Warn("affinity: %08x x%d: the new stack could not be removed; the source holds %d, the new stack %d", item.code, had, had - kept, kept);
		} else {
			plog::Error("affinity: %08x x%d: quick take could not be undone (source %d, new stack %d)", item.code, had,
			            Quantity(item.handle), kept);
		}
		return true;
	};
	if (SetQuantity(player, made, had - aff::WithdrawMax) != items::Result::Success ||
	    SetQuantity(player, item.handle, aff::WithdrawMax) != items::Result::Success) {
		return undo();
	}
	items::ItemInfo rest {.structSize = items::ItemInfoSize};
	if (Quantity(item.handle) + Quantity(made) != had || g_items->getItemInfo(g_context, made, &rest) != items::Result::Success) {
		return undo();
	}
	ResendCursor(player, item.runtimeId);  // the client still draws the old count: send the cut stack again
	Route(player, rest);  // a rest that cannot go home stays in the inventory with a new runtimeId; the next pass cuts it into 50s here again
	return true;
}

// A stack past aff::WithdrawMax new in the inventory (#1763: Ctrl+click out of the stash, or a hand pick-up put down):
// WithdrawMax of it split onto the cursor (the SDK moves no cursor item, so it stays there), the rest back home through
// Route, topping the cell's stack up. A busy cursor sends the whole stack home; a failed split keeps it in the inventory
// (it could never be taken out otherwise) and logs the result. Only counts move between the two items, checked by reading them back.
void Withdraw(D2RL::PlayerHandle player, const Noted& noted) noexcept {
	items::ItemInfo item {.structSize = items::ItemInfoSize};
	if (g_items->getItemInfo(g_context, noted.handle, &item) != items::Result::Success) {
		return;
	}
	const int32_t had = Quantity(item.handle);
	const bool hand = noted.runtimeId == g_handTaken;
	if (hand) g_handTaken = 0;  // one hand take: the stack put back home and quick-moved later is not one
	if (!hand && had > aff::WithdrawMax && aff::HomeOf(item.code).category != aff::Category::None &&
	    QuickWithdraw(player, item, had)) {
		return;
	}
	D2RL::ItemHandle held = D2RL::InvalidItemHandle;
	items::Result split = items::Result::Conflict;
	bool busy = true, sums = true;
	if (g_inventory->getCursorItem(g_context, player, &held) == D2RL::Inventory::Result::NotFound) {  // NotFound: an empty cursor
		const items::SplitStackRequest request1 {.structSize = items::SplitStackRequestSize, .player = player,
		                                         .sourceItem = item.handle, .quantity = 1};
		items::SplitStackResult result {.structSize = items::SplitStackResultSize};
		busy = false;
		// splitStack refuses a source past its maxstack (Unavailable, Slot5): split 1 off a stack of 2, then set both counts.
		// The source's rest is past 50 and only the native write sets it: without SetUnitStat nothing is touched. Every
		// failure puts the counts back from what is read, never assumed, and keeps the source in the inventory.
		D2RL::ItemHandle piece = D2RL::InvalidItemHandle;
		if (d2r::sig::SetUnitStat.rva == 0 || had <= aff::WithdrawMax) {
			split = items::Result::Unsupported;
		} else if ((split = SetQuantity(player, item.handle, 2)) != items::Result::Success) {
			if (SetQuantity(player, item.handle, had) != items::Result::Success) {
				plog::Warn("affinity: %08x x%d: set to 2 failed and the count could not be put back", item.code, had);
			}
		} else if ((split = g_items->splitStack(g_context, &request1, &result)) != items::Result::Success) {
			if (SetQuantity(player, item.handle, had) != items::Result::Success) {
				plog::Warn("affinity: %08x x%d: split failed and the count could not be put back", item.code, had);
			}
		} else {
			piece = result.cursorItem;  // source 1, piece 1
			if ((split = SetQuantity(player, item.handle, had - aff::WithdrawMax)) != items::Result::Success) {
				if (SetQuantity(player, item.handle, had - 1) != items::Result::Success) {
					plog::Warn("affinity: %08x x%d: source write failed, piece 1, source %d", item.code, had, Quantity(item.handle));
				}
			} else if ((split = SetQuantity(player, piece, aff::WithdrawMax)) != items::Result::Success) {
				int32_t got = Quantity(piece);  // 0: unreadable
				if (got <= 0) {
					const items::ItemEdit one {.structSize = items::ItemEditSize,
					                           .fields = items::EditFieldBit(items::EditField::Quantity), .quantity = 1};
					got = g_items->editItem(g_context, player, piece, &one) == items::Result::Success ? Quantity(piece) : 0;
				}
				if (got <= 0 || SetQuantity(player, item.handle, had - got) != items::Result::Success) {
					plog::Warn("affinity: %08x x%d: piece write failed, piece %d, source %d", item.code, had, got, Quantity(item.handle));
				}
			}
		}
		// self-check, read back: the two items hold what the stack had
		const int32_t source = Quantity(item.handle), cut = piece != D2RL::InvalidItemHandle ? Quantity(piece) : 0;
		sums = source + cut == had;
		if (!sums) {
			plog::Error("affinity: %08x withdrawal does not add up: had %d, source %d + piece %d", item.code, had, source, cut);
			split = split == items::Result::Success ? items::Result::Conflict : split;
		}
	}
	// An item with no home (Uber Ancients keys, #1785) has nowhere to route to: the rest stays in the inventory
	// A busy cursor, or a second homeless stack in the same pass, stays whole and is not retried. The guard also keeps
	// Route off At(0, -1, -1), out of bounds for a homeless code.
	const bool homeless = aff::HomeOf(item.code).category == aff::Category::None;
	const bool home = (split == items::Result::Success || busy) && !homeless && Route(player, item);
	if (homeless && split == items::Result::Success) {  // publishes the rest's native count (SetQuantity); the client still draws 1
		const items::ItemDestination back {.structSize = items::ItemDestinationSize, .container = C::Inventory,
		                                   .placement = items::Placement::Automatic};
		Move(player, item.handle, back);
	}
	// #1772: the source's move home made it the client's cursor item and put it down (ResendCursor), so the piece (or a
	// busy cursor's item) was held but not drawn; send the cursor item again after that last move
	items::ItemInfo cursor {.structSize = items::ItemInfoSize};
	if (g_inventory->getCursorItem(g_context, player, &held) == D2RL::Inventory::Result::Success &&
	    g_items->getItemInfo(g_context, held, &cursor) == items::Result::Success) {
		ResendCursor(player, cursor.runtimeId);
	}
	if ((!busy && !sums) || !home) {
		plog::Warn("affinity: %08x x%d taken out of the stash: %d to the cursor (split result %u, counts %s), the rest %s", item.code,
		           had, split == items::Result::Success ? aff::WithdrawMax : 0, static_cast<uint32_t>(split),
		           busy ? "untouched" : sums ? "add up" : "DO NOT add up", home ? "back home" : "kept in the inventory");
	}
	if (!home && g_kept.count < MaxNoted) {
		g_kept.item[g_kept.count++] = noted;
	}
}

auto Listed(const NotedList& list, uint32_t runtimeId) noexcept -> bool {
	for (uint32_t i = 0; i < list.count; ++i) {
		if (list.item[i].runtimeId == runtimeId) {
			return true;
		}
	}
	return false;
}

// The other bases' maps off the Maps page, then the wanted base's maps onto it
void Swap(D2RL::PlayerHandle player, uint32_t want) noexcept {
	uint32_t off = 0, on = 0;
	for (uint32_t i = 0; i < g_pass.reservedCount; ++i) {
		items::ItemInfo& item = g_pass.reserved[i];
		if (item.sharedStashPage == aff::MapsPage && aff::InMapView(item.x, item.y) && aff::BaseOf(item.code).tier >= 0 &&
		    aff::Normal(item.code) != aff::Normal(want) && ToStorage(player, item) == items::Result::Success) {
			++off;
		}
	}
	if (const aff::Base base = aff::BaseOf(want); base.tier >= 0) {
		const uint32_t page = aff::FirstStoragePage + static_cast<uint32_t>(base.tier);
		for (uint32_t i = 0; i < g_pass.reservedCount; ++i) {
			items::ItemInfo& item = g_pass.reserved[i];
			if (item.sharedStashPage != page || aff::Normal(item.code) != aff::Normal(want)) {
				continue;
			}
			const aff::Cell cell = FreeMapCell();
			if (cell.x < 0) {
				break;  // the rest stay stored
			}
			if (MoveTo(player, item, aff::MapsPage, cell.x, cell.y) == items::Result::Success) {
				++on;
			}
		}
	}
	g_shown = want;
	++g_swapped;
}

// While a reserved tab is up: an item on the Maps or Bossing page off its place (one put straight onto the page on show,
// or left under the buttons or the cube) goes where Route sends it; an item no rule takes goes back to the inventory
void Tidy(D2RL::PlayerHandle player) noexcept {
	for (uint32_t i = 0; i < g_pass.reservedCount; ++i) {
		const items::ItemInfo& item = g_pass.reserved[i];
		if (item.code == 0 || (item.sharedStashPage != aff::MapsPage && item.sharedStashPage != aff::BossingPage) ||
		    !InCell(item.x, item.y)) {
			continue;
		}
		const aff::Home home = aff::HomeOf(item.code);
		if (aff::AtHome(home, item.code, g_shown, true, item.sharedStashPage, item.x, item.y)) {
			continue;
		}
		bool found = false;  // Route has it already, from its own copy
		for (uint32_t j = 0; j < g_pass.foundCount && !found; ++j) {
			found = g_pass.found[j].runtimeId == item.runtimeId;
		}
		if (found) {
			continue;
		}
		if (home.category != aff::Category::None) {
			Route(player, item);
			continue;
		}
		const items::ItemDestination back {.structSize = items::ItemDestinationSize, .container = C::Inventory,
		                                   .placement = items::Placement::Automatic};
		const items::Result moved = Move(player, item.handle, back);
		if (moved == items::Result::Success || !g_tidyFailed.exchange(true)) {  // a full inventory logs once, not every pass
			plog::Info("affinity: %08x on page %u at %d,%d belongs to no reserved tab: back to the inventory (result %u)",
			           item.code, item.sharedStashPage, item.x, item.y, static_cast<uint32_t>(moved));
		}
		// a stack past the inventory cap is split now, not left whole (#1785)
		if (moved == items::Result::Success && aff::Stacks(item.code) && Quantity(item.handle) > aff::WithdrawMax) {
			Withdraw(player, Noted {item.runtimeId, item.code, item.handle});
		}
	}
}

}  // namespace detail

}  // namespace stash_affinity
