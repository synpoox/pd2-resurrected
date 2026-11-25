// Hook helpers: swap a D2RLoader thunk slot, redirect a relay call.
// Moved verbatim from item_rules.cpp (#2198).
#include "shared.h"

auto ThunkSlotRva(const sigscan::Entry& entry, uint32_t callOffset) noexcept -> uint64_t {
	if (entry.state != sigscan::State::Ok) {
		return 0;
	}
	const auto* const call = reinterpret_cast<const uint8_t*>(d2r::imageBase + entry.rva + callOffset);
	if (call[0] != 0xe8) {
		return 0;
	}
	const uint8_t* const thunk = call + 5 + *reinterpret_cast<const int32_t*>(call + 1);
	if (thunk[0] != 0xff || thunk[1] != 0x25) {  // jmp [rip+disp32]; anything else is not D2RLoader's thunk
		return 0;
	}
	const uint8_t* const slot = thunk + 6 + *reinterpret_cast<const int32_t*>(thunk + 2);
	return reinterpret_cast<uint64_t>(slot) - d2r::imageBase;
}

auto SwapThunkSlot(const D2RL::PluginContext* context, const sigscan::Entry& entry, uint32_t callOffset, uint64_t wrapper,
                   uint64_t* original) noexcept -> bool {
	const uint64_t slotRva = ThunkSlotRva(entry, callOffset);
	if (slotRva == 0) {
		return false;
	}
	const uint64_t old = *reinterpret_cast<const uint64_t*>(d2r::imageBase + slotRva);
	if (old == 0) {
		return false;
	}
	*original = old;
	if (!context->PatchWriteU64(slotRva, &old, sizeof(old), wrapper)) {
		return false;
	}
	plog::Info("%s: %s slot at image +0x%llx swapped", entry.area, entry.name, static_cast<unsigned long long>(slotRva));
	return true;
}

auto RedirectCall(const D2RL::PluginContext* context, const char* pattern, uint32_t callAt, uint64_t entry,
                  uint64_t* original) noexcept -> bool {
	uint64_t match = 0;
	if (sigscan::ScanUnique(context->exeBase, pattern, 0, &match) != 1) {
		return false;
	}
	const uint64_t site = context->exeBase + match + callAt;
	uint8_t stock[5];
	std::memcpy(stock, reinterpret_cast<const void*>(site), 5);
	if (stock[0] != 0xe8) {
		return false;
	}
	int32_t rel = 0;
	std::memcpy(&rel, stock + 1, 4);
	const D2RL::MutationService* mutations = nullptr;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize)) {
		return false;
	}
	uint8_t* relay = AllocateRelay(context->exeBase);
	if (relay == nullptr) {
		return false;
	}
	const uint8_t jump[6] = {0xff, 0x25, 0, 0, 0, 0};  // jmp [rip+0]; &entry
	std::memcpy(relay, jump, sizeof(jump));
	std::memcpy(relay + sizeof(jump), &entry, 8);
	const int64_t reach = static_cast<int64_t>(reinterpret_cast<uint64_t>(relay)) - static_cast<int64_t>(site + 5);
	const int32_t rel32 = static_cast<int32_t>(reach);
	uint8_t call[5] = {0xe8};
	std::memcpy(call + 1, &rel32, 4);
	DWORD old;
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (reach != rel32 || VirtualProtect(relay, 0x1000, PAGE_EXECUTE_READ, &old) == 0 ||
	    mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		VirtualFree(relay, 0, MEM_RELEASE);
		return false;
	}
	*original = site + 5 + static_cast<int64_t>(rel);  // before the call can land
	const D2RL::Mutations::BytePatchRequest patch{.structSize = D2RL::Mutations::BytePatchRequestSize,
	                                              .rva = site - context->exeBase,
	                                              .expected = stock,
	                                              .expectedSize = 5,
	                                              .bytes = call,
	                                              .size = 5};
	D2RL::Mutations::OperationHandle operation = 0;
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	const bool staged = mutations->stageBytePatch(context, transaction, &patch, &operation) == D2RL::Mutations::Result::Success;
	if (!staged || mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		// a failed commit that did not restore the stock call may still aim at the relay: keep the page then
		if (!staged || D2RL::Mutations::HasFlag(result.flags, D2RL::Mutations::CommitFlags::OriginalStateRestored)) {
			VirtualFree(relay, 0, MEM_RELEASE);
		}
		return false;
	}
	return true;
}
