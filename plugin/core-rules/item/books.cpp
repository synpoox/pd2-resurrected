// PD2's per-hand stat sum and the unlimited books' item spell entries.
// Moved verbatim from item_rules.cpp (#2198).
#include "shared.h"


// PD2's 0x10381720: each item-type layer of the stat that the wielded weapon is of; 2han counts only with the other
// hand empty or holding the same item, 1han only when the other hand holds another item or none
// shortcut: PD2 also counts the 1han layer for a dual-wield class's 2han weapon beside another item; no stat this sum
// is used for has a 1han layer (skills passiveitype), upgrade if one gets it.
auto HandStatSum(Unit* unit, uint32_t stat) noexcept -> int32_t {
	using namespace d2r::rva;
	Unit* weapon = unit != nullptr ? d2r::Engine<Unit* (*)(Unit*) noexcept>(UnitWeapon)(unit) : nullptr;
	const uint8_t* tables = weapon != nullptr ? ::DataTables(d2r::api.unitDataFlag(unit)) : nullptr;
	if (tables == nullptr) {
		return 0;
	}
	void* inventory = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(UnitInventory)(unit, __FILE__, __LINE__);
	const auto bodyItem = d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(InventoryBodyItem);
	Unit* right = inventory != nullptr ? bodyItem(inventory, 4) : nullptr;  // bodylocs 4 and 5 are the hands
	Unit* other = right == weapon && inventory != nullptr ? bodyItem(inventory, 5) : right;
	const auto isType = d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType);
	const int64_t types = At<int64_t>(tables, TablesItemTypesCount);
	int32_t sum = 0;
	for (int32_t type = 1; type < types; ++type) {
		if (isType(weapon, type) == 0) {
			continue;
		}
		const bool counted = type == ItemType2han ? other == nullptr || other == weapon
		                     : type == ItemType1han ? other != weapon : true;
		sum += counted ? StatOf(unit, static_cast<int32_t>(stat), type) : 0;
	}
	return sum;
}

// The unlimited books' item spell entries (17 and 18), written through the SDK's MutationService (no service adds an
// item spell); the patch is refused unless both entries are still empty.
auto PatchUnlimitedBooks(const D2RL::PluginContext* context) noexcept -> bool {
	using namespace d2r::rva;
	const D2RL::MutationService* mutations = nullptr;
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize) ||
	    mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		return false;
	}
	static constexpr uint8_t Empty[2 * ItemSpellEntrySize] = {};
	static constexpr int32_t Books[2 * 3] = {UnlimitedPortalCheck, UnlimitedPortalUse, 1, 0, AlmanacUseSlot, 1};
	static_assert(sizeof(Books) == sizeof(Empty));
	const D2RL::Mutations::BytePatchRequest patch{
		.structSize = D2RL::Mutations::BytePatchRequestSize,
		.rva = ItemSpellTable + static_cast<uint64_t>(UnlimitedPortalSpell) * ItemSpellEntrySize,
		.expected = Empty,
		.expectedSize = sizeof(Empty),
		.bytes = Books,
		.size = sizeof(Books)};
	D2RL::Mutations::OperationHandle operation = 0;
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	if (mutations->stageBytePatch(context, transaction, &patch, &operation) != D2RL::Mutations::Result::Success ||
	    mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		return false;
	}
	return true;
}
