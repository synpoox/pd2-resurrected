// The other Rathma clone (#475, #645): core-events' shared life and core-rules' open wounds both ask for it.
// PD2's test: the victim is a clone, both boss slots (game +0x2600 / +0x2604, kept in D2R's per-game counters under
// rathma_ai.cpp's 'UBR0' / 'UBR1') are set, and the other slot's clone is not dying or dead. PD2 takes the slot whatever
// it holds; the port also wants the other clone's class there, so an id left by Rathma or Mendeln never stands in for it.
#pragma once

#include "pd2_engine.h"

#include <cstdint>

namespace pd2::rathma {

inline constexpr int32_t BoneClone = 952, PoisonClone = 953;  // merged monstats ids (PD2 935, 936)
inline constexpr uint32_t BossKey = 0x55425230;                // 'UBR0' / 'UBR1'

inline auto Counter(d2r::Game* game, uint32_t key) noexcept -> int32_t {
	return *d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

// The live monster with that id: the game's monster lists are keyed by id & 0x7f
inline auto MonsterById(d2r::Game* game, uint32_t id) noexcept -> d2r::Unit* {
	using namespace d2r::rva;
	for (auto* u = d2r::At<d2r::Unit*>(game, GameMonsters + static_cast<int>(id & 0x7f) * 8); u != nullptr;
	     u = d2r::At<d2r::Unit*>(u, UnitListNext)) {
		if (d2r::At<uint32_t>(u, UnitIdField) == id) {
			return u;
		}
	}
	return nullptr;
}

// The other clone in the boss slots, whatever its mode: PD2's event 15 never reads it, so the half holds after the first death
inline auto Other(d2r::Game* game, d2r::Unit* victim) noexcept -> d2r::Unit* {
	using namespace d2r::rva;
	if (game == nullptr || victim == nullptr || d2r::At<int32_t>(victim, 0) != 1) {
		return nullptr;
	}
	const int32_t classId = d2r::At<int32_t>(victim, UnitClassId);
	if (classId != BoneClone && classId != PoisonClone) {
		return nullptr;
	}
	const int32_t bone = Counter(game, BossKey), poison = Counter(game, BossKey + 1);
	if (bone == 0 || poison == 0) {
		return nullptr;
	}
	const bool isBone = classId == BoneClone;
	d2r::Unit* other = MonsterById(game, static_cast<uint32_t>(isBone ? poison : bone));
	return other != nullptr && d2r::At<int32_t>(other, UnitClassId) == (isBone ? PoisonClone : BoneClone) ? other : nullptr;
}

// Other, and not dying or dead
inline auto Partner(d2r::Game* game, d2r::Unit* victim) noexcept -> d2r::Unit* {
	d2r::Unit* other = Other(game, victim);
	if (other == nullptr) {
		return nullptr;
	}
	const int32_t mode = d2r::At<int32_t>(other, d2r::rva::UnitMode);
	return mode != 0 && mode != 12 ? other : nullptr;
}

}  // namespace pd2::rathma
