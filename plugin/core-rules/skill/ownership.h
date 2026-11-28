// Ownership used by corpse guards, melee reach and Open Wounds (#2035, #2269).
#pragma once
#include "pd2_engine.h"

namespace pd2r::skills {

using d2r::At;
using d2r::Unit;

// Image 0x6ab3782c: server allocation 0x4905a0 sets this flag through 0x34e140.
// Client allocation 0x98fa0 instead puts a serial in UnitGame; it is NOT a side test.
constexpr int UnitExtendedFlags = 0x128;
constexpr uint32_t ServerUnit = 0x04000000;
inline auto IsServerUnit(const Unit* unit) noexcept -> bool {
	return unit != nullptr && (At<uint32_t>(unit, UnitExtendedFlags) & ServerUnit) != 0;
}

constexpr int MonDataAi = 0x30, AiOwnerId = 0x68, AiOwnerType = 0x6c;
inline auto MonsterAiOwner(const Unit* target) noexcept -> const uint8_t* {
	using namespace d2r::rva;
	// 0x4a53c0 is server-only and has no side check. Client init 0x1a2c90 writes
	// a word at data+0x30 and eight FF bytes at +0x32, not an AI pointer.
	const auto* data = IsServerUnit(target) && At<int32_t>(target, UnitType) == UnitMonster
	    ? At<uint8_t*>(target, UnitData) : nullptr;
	return data != nullptr ? At<uint8_t*>(data, MonDataAi) : nullptr;
}
inline auto ValidOwnerId(uint32_t id) noexcept -> bool {
	return id != 0 && id != UINT32_MAX;
}
// D2Client RosterPets' (pet id) -> player id, UINT32_MAX when absent (0x13aba0).
using ClientPetOwnerFn = uint32_t (*)(uint32_t) noexcept;
inline auto OwnedByPlayer(const Unit* target, ClientPetOwnerFn clientOwner = nullptr) noexcept -> bool {
	using namespace d2r::rva;
	if (target == nullptr) {
		return false;
	}
	if (At<int32_t>(target, UnitOwnerType) == 0 && ValidOwnerId(At<uint32_t>(target, UnitOwnerId))) {
		return true;
	}
	// Client pets need not populate unit owner fields either; their roster is synced separately.
	if (At<int32_t>(target, UnitType) == UnitMonster && !IsServerUnit(target) && clientOwner != nullptr) {
		return ValidOwnerId(clientOwner(At<uint32_t>(target, UnitIdField)));
	}
	// Keep the IDs: native 0x4a53c0 resolves a live owner, which need not exist
	// when Find/Redemption examine a corpse. No SDK service exposes these IDs.
	const auto* ai = MonsterAiOwner(target);
	return ai != nullptr && At<int32_t>(ai, AiOwnerType) == 0 && ValidOwnerId(At<uint32_t>(ai, AiOwnerId));
}

// PD2 0x103749f0(unit, 1): the monster's owner or its owner's owner is a player.
// The caller supplies the native lookup for the unit's side (client/server).
template <typename Find>
inline auto PlayerOwnedMonster(const Unit* unit, Find find, ClientPetOwnerFn clientOwner = nullptr) noexcept -> bool {
	using namespace d2r::rva;
	for (int depth = 0; depth < 2 && unit != nullptr && At<int32_t>(unit, UnitType) == UnitMonster; ++depth) {
		if (OwnedByPlayer(unit, clientOwner)) {
			return true;
		}
		const uint32_t id = At<uint32_t>(unit, UnitOwnerId);
		const Unit* owner = At<int32_t>(unit, UnitOwnerType) == UnitMonster && ValidOwnerId(id) ? find(unit, id) : nullptr;
		if (owner == nullptr) {
			const auto* ai = MonsterAiOwner(unit);
			if (ai != nullptr && At<int32_t>(ai, AiOwnerType) == UnitMonster) {
				const uint32_t aiId = At<uint32_t>(ai, AiOwnerId);
				if (ValidOwnerId(aiId)) {
					owner = find(unit, aiId);
				}
			}
		}
		unit = owner;
	}
	return false;
}

}  // namespace pd2r::skills
