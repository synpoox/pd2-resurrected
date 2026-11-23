// PD2's own check of a cubemain row's first input against an item (0x103649d0): class, type, quality, socketed,
// ethereal. Shared by the corruption roll (core-events) and the corruption markers (core-ui, #2070).
#pragma once

#include "pd2_engine.h"

#include <cstdint>

namespace pd2 {

// `in`: the row's first input (record + CubeInputs). `sockets` false skips the socketed test: a corruption can add
// sockets, so an item it corrupted no longer passes its row's `nos`.
inline auto CubeInputMatches(const uint8_t* in, d2r::Unit* item, bool sockets = true) noexcept -> bool {
	using namespace d2r::rva;
	using d2r::At;
	const auto has = [item](uint64_t fn) noexcept { return d2r::Engine<int32_t (*)(d2r::Unit*) noexcept>(fn)(item) != 0; };
	const auto flags = At<uint16_t>(in, 0);
	const int32_t id = At<int32_t>(in, 2);
	return !(((flags & 2) != 0 && d2r::Engine<int32_t (*)(d2r::Unit*, int32_t) noexcept>(ItemIsType)(item, id) == 0) ||
	         ((flags & 1) != 0 && id != -1 && id != 0xffff && At<int32_t>(item, UnitClassId) != id) ||
	         (in[6] != 0 && d2r::Engine<int32_t (*)(d2r::Unit*) noexcept>(ItemQuality)(item) != in[6]) ||
	         ((flags & 0x20) != 0 && has(ItemIsEthereal)) || ((flags & 0x10) != 0 && !has(ItemIsEthereal)) ||
	         (sockets && (((flags & 4) != 0 && has(ItemHasSockets)) || ((flags & 8) != 0 && !has(ItemHasSockets)))));
}

// A corruption row's second input is the Worldstone Shard or nothing (PD2's check; the Annihilus rows take `cwss`).
inline auto CorruptionShardOk(uint32_t dataFlag, const uint8_t* row) noexcept -> bool {
	using namespace d2r::rva;
	const int32_t shard = d2r::At<int32_t>(row + CubeInputs + CubeInputSize, 2);
	const uint8_t* rec = shard > 0 ? d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ItemRecord)(dataFlag, shard) : nullptr;
	return rec == nullptr || d2r::At<uint32_t>(rec, ItCode) == 0x20737377;  // 'wss '
}

}  // namespace pd2
