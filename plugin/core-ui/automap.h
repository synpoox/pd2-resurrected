// Automap for PD2's level types 36 and up (LP2, #212). D2R's automap knows 36 level types; its tile lookup
// (rva::AutomapCel) reads past its tables for higher ones. The hook answers those types from PD2's AutoMap.txt rows,
// which the build writes to the custom table data/global/excel/d2rloader/core-ui/automap.txt (tools/pd2_levels.py),
// and leaves types 0-35 to D2R.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace automap {

inline constexpr int32_t NoCel = -1;  // the lookup's answer when nothing is drawn
inline constexpr uint32_t Any = 255;  // in Style or StartSequence: any (AutoMap.txt's -1, a 0xff byte in D2R's rows)

// One row of the custom table, in its column order (written by tools/pd2_levels.py automap_rows).
struct Row {
	uint32_t levelType, tileType, style, start, end, celCount, cel[4];
};

// D2R's match over one level type's rows: the first row with this tile, style (row 255: any) and sequence (row start
// 255: any, else start..end) gives cels[Roll(seed) % count]; a row with no cels, or no row, gives -1.
constexpr auto FindCel(const Row* rows, size_t count, int32_t levelType, int32_t tileType, uint32_t style,
                       int32_t sequence, uint32_t* seed) noexcept -> int32_t {
	for (size_t i = 0; i < count; ++i) {
		const Row& r = rows[i];
		if (static_cast<int32_t>(r.levelType) != levelType || static_cast<int32_t>(r.tileType) != tileType ||
		    (r.style != Any && r.style != style) ||
		    (r.start != Any && (sequence < static_cast<int32_t>(r.start) || sequence > static_cast<int32_t>(r.end)))) {
			continue;
		}
		if (r.celCount == 0 || r.celCount > std::size(r.cel)) {
			return NoCel;
		}
		const uint64_t s = static_cast<uint64_t>(seed[0]) * 0x6ac690c5ULL + seed[1];  // d2r::Roll, as the lookup does
		seed[0] = static_cast<uint32_t>(s);
		seed[1] = static_cast<uint32_t>(s >> 32);
		return static_cast<int32_t>(r.cel[seed[0] % r.celCount]);
	}
	return NoCel;
}

// Registers the custom table and its load listener, and hooks the lookup. Returns the report for the log.
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;
// Rows loaded and lookups answered, for the status command.
void Counters(char* line, size_t size) noexcept;

}  // namespace automap
