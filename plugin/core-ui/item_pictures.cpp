#include "item_pictures.h"

#include "pd2_engine.h"

#include <D2RLPlugin/mutations.h>

#include <cstdio>

namespace item_pictures {

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	const D2RL::MutationService* mutations = nullptr;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize)) {
		std::snprintf(report, size, "FAILED (no mutation service)");
		return;
	}
	// Both key patches through one transaction: PatchWrite writes at most 8 bytes, and half a patch would break a key.
	// The second is `cheat spawn`'s key (#323), so tests can spawn PD2's 4-character items.
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		std::snprintf(report, size, "FAILED (no transaction)");
		return;
	}
	const D2RL::Mutations::BytePatchRequest patch{.structSize = D2RL::Mutations::BytePatchRequestSize,
	                                              .rva = d2r::rva::ItemsJsonKey,
	                                              .expected = d2r::rva::ItemsJsonKeyD2R,
	                                              .expectedSize = sizeof(d2r::rva::ItemsJsonKeyD2R),
	                                              .bytes = d2r::rva::ItemsJsonKeyFull,
	                                              .size = sizeof(d2r::rva::ItemsJsonKeyFull)};
	const D2RL::Mutations::BytePatchRequest spawn{.structSize = D2RL::Mutations::BytePatchRequestSize,
	                                              .rva = d2r::rva::SpawnCheatKey,
	                                              .expected = d2r::rva::SpawnCheatKeyD2R,
	                                              .expectedSize = sizeof(d2r::rva::SpawnCheatKeyD2R),
	                                              .bytes = d2r::rva::SpawnCheatKeyFull,
	                                              .size = sizeof(d2r::rva::SpawnCheatKeyFull)};
	D2RL::Mutations::OperationHandle operation = 0, spawnOperation = 0;
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	if (mutations->stageBytePatch(context, transaction, &patch, &operation) != D2RL::Mutations::Result::Success ||
	    mutations->stageBytePatch(context, transaction, &spawn, &spawnOperation) != D2RL::Mutations::Result::Success ||
	    mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		std::snprintf(report, size, "FAILED (bytes differ)");
		return;
	}
	std::snprintf(report, size, "items.json and cheat spawn keys keep 4 characters");
}

}  // namespace item_pictures
