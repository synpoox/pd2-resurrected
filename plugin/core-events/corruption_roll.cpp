// General roll helpers (#943): the CRT roll and PD2's corruption / Desecration
// roll over cubemain's corruption rows. Retained by core-rules' lane (corruption, Demonic Cube, Desecration); the
// map-drop and brick callers only borrow them.
#include "corruption_roll.h"

#include "log.h"

#include <intrin.h>

#include <cstdlib>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t StatCorrupted = 413, StatCorruptor = 414;  // PD2 360 and 361
// properties.txt corrupt, corruptnum, desecrate, desecratenum: D2R numbers them by row past the "Expansion" row, not by *Id (tools/tests/test_desecration_rolls.py pins them)
constexpr int32_t PropCorrupt = 287, PropCorruptNum = 288, PropDesecrate = 349, PropDesecrateNum = 350;
constexpr int32_t DesecrateFloor = 851;  // PD2's Desecration roll, 851-999

}  // namespace

// The CRT's rand state is per thread and starts at seed 1: seed each thread once, or every session rolls the same
auto RollRand() noexcept -> int32_t {
	static thread_local const bool seeded = (std::srand(static_cast<unsigned>(__rdtsc())), true);
	(void)seeded;
	return std::rand();
}

// PD2's 0x103649d0: a random corruption outcome from cubemain's corruption rows (op 16 or 18 on `corruptor` with a
// value), unless the item is corrupted. Only rows whose first input matches the item (PD2's own check: class, type,
// quality, socketed, ethereal) and whose second is the Worldstone Shard or nothing count. The group's "destroyed
// (rare)" row (rare usetype output, no third mod) sets the roll to value..999, so the outcome is one of the rows after
// it and never the destroyed one; the first row whose value beats the roll puts its mods on.
// With `desecrate` (PD2's flag 1, Desecration) the roll is 851-999 from the start (an amulet's rows 682-689) and the
// row's corrupt/corruptnum go on as desecrate/desecratenum; the caller checks the item.
auto CorruptItem(Game* game, Unit* item, bool desecrate, const char* label) noexcept -> bool {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	if (!desecrate && stat(item, StatCorrupted, 0) != 0) {
		return false;
	}
	const auto record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(CubeRecipe);
	const auto itemRecord = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ItemRecord);
	const auto has = [item](uint64_t fn) noexcept { return d2r::Engine<int32_t (*)(Unit*) noexcept>(fn)(item) != 0; };
	const int32_t rows = d2r::Engine<int32_t (*)(uint32_t) noexcept>(CubeRecipeCount)(dataFlag);
	int32_t floor = desecrate ? DesecrateFloor : 0;
	int32_t roll = desecrate ? DesecrateFloor + RollRand() % (1000 - DesecrateFloor) : 0;
	for (int32_t i = 0; i < rows; ++i) {
		const uint8_t* r = record(dataFlag, i);
		if (r == nullptr || r[CubeEnabled] == 0 || (r[CubeOp] != 16 && r[CubeOp] != 18) ||
		    At<int32_t>(r, CubeParam) != StatCorruptor || At<int32_t>(r, CubeValue) == 0) {
			continue;
		}
		const uint8_t* in = r + CubeInputs;
		const int32_t shard = At<int32_t>(in + CubeInputSize, 2);
		const uint8_t* shardRecord = shard > 0 ? itemRecord(dataFlag, shard) : nullptr;
		if (shardRecord != nullptr && At<uint32_t>(shardRecord, ItCode) != 0x20737377) {  // 'wss '
			continue;
		}
		const auto flags = At<uint16_t>(in, 0);
		const int32_t id = At<int32_t>(in, 2);
		if (((flags & 2) != 0 && d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType)(item, id) == 0) ||
		    ((flags & 1) != 0 && id != -1 && id != 0xffff && At<int32_t>(item, UnitClassId) != id) ||
		    (in[6] != 0 && d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemQuality)(item) != in[6]) ||
		    ((flags & 0x20) != 0 && has(ItemIsEthereal)) || ((flags & 0x10) != 0 && !has(ItemIsEthereal)) ||
		    ((flags & 4) != 0 && has(ItemHasSockets)) || ((flags & 8) != 0 && !has(ItemHasSockets))) {
			continue;
		}
		const int32_t value = At<int32_t>(r, CubeValue);
		if (!desecrate && r[CubeOutType] == 0xff && r[CubeOutQuality] == 6 &&
		    At<int32_t>(r, CubeOutMods + 2 * CubeModSize) == -1) {
			floor = value;
			roll = value < 1000 ? value + RollRand() % (1000 - value) : value;  // PD2 divides by 0 at 1000
		}
		if (floor == 0 || roll >= value) {
			continue;
		}
		for (int m = 0; m < 5; ++m) {
			const uint8_t* mod = r + CubeOutMods + m * CubeModSize;
			if (At<int32_t>(mod, 0) == -1) {
				break;
			}
			int32_t prop = At<int32_t>(mod, 0);
			if (desecrate) {
				prop = prop == PropCorrupt ? PropDesecrate : prop == PropCorruptNum ? PropDesecrateNum : prop;
			}
			int32_t property[4] = {prop, At<int16_t>(mod, 4), At<int16_t>(mod, 6), At<int16_t>(mod, 8)};
			d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(ApplyCubeMod)(item, property);
		}
		plog::Info("%s: item %d %s by cubemain row %d (roll %d of %d-999)", desecrate ? "desecration" : label,
		           At<int32_t>(item, UnitClassId), desecrate ? "desecrated" : "corrupted", i, roll, floor);
		return true;
	}
	return false;
}
