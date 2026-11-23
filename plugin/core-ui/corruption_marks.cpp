// Corruption markers (#2070). PD2 appends a red `*` to the property a corruption added or changed; here the whole line
// is red (the owner's call on #2070), and nothing else is.
//
// How a line is known to be the corruption's: a corruption puts its outcome's code (cubemain's `corrupt` mod value) in
// the item's `corrupted` stat (413). The cubemain rows with that code whose first input matches the item (PD2's own
// check, cube_input_match.h) name the properties the outcome put on; properties.txt names their stats. D2R's property
// lines (0x2dc4b0) are built once as they are, then again with each of those stats lowered by its row's minimum on the
// client copy (and set back right after), and only the lines that differ go red (corruption_lines.h). A merged line (the
// item's own +200% and the corruption's +40% Enhanced Damage) is therefore red as a whole, and a line the corruption did
// not touch never is. When a value cannot be lowered (the stat is not on the item's magic or base list) its line stays blue.
// Client copy only, UI thread, as item_quantity's tab counts: the server's item and the save are untouched.
#include "corruption_marks.h"

#include "corruption_lines.h"
#include "cube_input_match.h"
#include "log.h"
#include "pd2_engine.h"
#include "property_ids.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace corruption_marks {

namespace {

using d2r::At;
using d2r::Unit;
namespace lines = pd2::corruption_lines;

// D2Client mods.cpp: (item, out, size, ...) writes the item's blue property lines to `out`, one per LF, no colour codes
// (d2rAI-toolkit knowledge/d2r-93847-addresses.md, "Item property lines"). The entry 0x2da760 it calls is D2RLoader's.
constexpr uint64_t PropertyLines = 0x2dc4b0;
constexpr uint8_t PropertyLinesEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
using LinesFn = void (*)(Unit* item, char* out, int32_t size, int32_t a4, uint32_t a5, int32_t a6, uint32_t a7, uint32_t a8,
                         void* a9, uint64_t* a10) noexcept;
LinesFn g_lines = nullptr;

constexpr int32_t StatCorrupted = 413, StatCorruptor = 414;  // as corruption_roll.cpp
constexpr int32_t PropertyFuncs = 7;

struct Property {
	std::vector<int32_t> stats;  // every stat its functions set
};
std::vector<Property> g_props;     // by properties.txt row number (propids)
std::vector<int32_t> g_valShift;   // by itemstatcost id
int32_t g_corrupt = -1, g_corruptNum = -1;

std::atomic<uint32_t> g_items {0}, g_marked {0};

// The stats a property function sets with no stat column of its own (D2R's property functions 5, 6 and 7)
auto FixedStats(int32_t func, std::vector<int32_t>* out) -> void {
	if (func == 5) {
		out->insert(out->end(), {21, 23, 159});  // mindamage, secondary_mindamage, item_throw_mindamage
	} else if (func == 6) {
		out->insert(out->end(), {22, 24, 160});  // maxdamage, secondary_maxdamage, item_throw_maxdamage
	} else if (func == 7) {
		out->insert(out->end(), {17, 18});  // item_maxdamage_percent, item_mindamage_percent
	}
}

auto Load(const D2RL::PluginContext* context) -> bool {
	std::string propText, statText;
	if (!propids::ReadExcel(context, L"properties.txt", &propText) || !propids::ReadExcel(context, L"itemstatcost.txt", &statText)) {
		return false;
	}
	const lines::Table stats = lines::ParseTable(statText);
	const int name = stats.Column("Stat"), id = stats.Column("*ID"), shift = stats.Column("ValShift");
	std::map<std::string_view, int32_t> statIds;
	for (const auto& row : stats.rows) {
		const int32_t n = lines::Number(lines::Table::Cell(row, id));
		statIds[lines::Table::Cell(row, name)] = n;
		if (n >= 0 && n < 4096) {
			g_valShift.resize(std::max<size_t>(g_valShift.size(), n + 1));
			g_valShift[n] = lines::Number(lines::Table::Cell(row, shift));
		}
	}
	const lines::Table props = lines::ParseTable(propText);
	const int code = props.Column("code");
	int func[PropertyFuncs], stat[PropertyFuncs];
	for (int i = 0; i < PropertyFuncs; ++i) {
		func[i] = props.Column("func" + std::to_string(i + 1));
		stat[i] = props.Column("stat" + std::to_string(i + 1));
	}
	for (const auto& row : props.rows) {
		const std::string_view c = lines::Table::Cell(row, code);
		if (c == "Expansion") {
			continue;  // not a row to the game (propids::Parse)
		}
		Property& p = g_props.emplace_back();
		for (int i = 0; i < PropertyFuncs; ++i) {
			const std::string_view s = lines::Table::Cell(row, stat[i]);
			const auto it = s.empty() ? statIds.end() : statIds.find(s);
			if (it != statIds.end()) {
				p.stats.push_back(it->second);
			} else {
				FixedStats(lines::Number(lines::Table::Cell(row, func[i])), &p.stats);
			}
		}
		if (c == "corrupt") {
			g_corrupt = static_cast<int32_t>(g_props.size()) - 1;
		} else if (c == "corruptnum") {
			g_corruptNum = static_cast<int32_t>(g_props.size()) - 1;
		}
	}
	return g_corrupt >= 0 && g_corruptNum >= 0;
}

// One stat lowered for the second build: on the magic list (list != null) or the base list
struct Lowered {
	int32_t stat, by, was;
	void* list;
};

// The corruption's stats on `item`, each lowered by its row's minimum (shifted by the stat's ValShift). Empty when the
// item is not corrupted, or its outcome has no property that is on the item.
auto Collect(Unit* item, std::vector<Lowered>* out) noexcept -> void {
	using namespace d2r::rva;
	const int32_t outcome = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(item, StatCorrupted, 0);
	if (outcome == 0) {
		return;
	}
	const uint32_t dataFlag = d2r::Engine<uint8_t (*)(Unit*) noexcept>(UnitDataFlag)(item);
	const auto record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(CubeRecipe);
	const int32_t rows = d2r::Engine<int32_t (*)(uint32_t) noexcept>(CubeRecipeCount)(dataFlag);
	std::map<int32_t, int32_t> by;  // stat -> the smallest minimum among the matching rows
	for (int32_t i = 0; i < rows; ++i) {
		const uint8_t* r = record(dataFlag, i);
		if (r == nullptr || r[CubeEnabled] == 0 || (r[CubeOp] != 16 && r[CubeOp] != 18) ||
		    At<int32_t>(r, CubeParam) != StatCorruptor || At<int32_t>(r, CubeValue) == 0) {
			continue;
		}
		bool ours = false;
		for (int m = 0; m < 5 && !ours; ++m) {
			const uint8_t* mod = r + CubeOutMods + m * CubeModSize;
			ours = At<int32_t>(mod, 0) == g_corrupt && At<int16_t>(mod, 6) == outcome;
		}
		if (!ours || !pd2::CubeInputMatches(r + CubeInputs, item)) {
			continue;
		}
		for (int m = 0; m < 5; ++m) {
			const uint8_t* mod = r + CubeOutMods + m * CubeModSize;
			const int32_t prop = At<int32_t>(mod, 0);
			if (prop == -1) {
				break;
			}
			if (prop == g_corrupt || prop == g_corruptNum || prop < 0 || static_cast<size_t>(prop) >= g_props.size()) {
				continue;
			}
			const int32_t min = At<int16_t>(mod, 6), max = At<int16_t>(mod, 8);
			const int32_t step = min > 0 ? min : max;
			if (step <= 0) {
				continue;  // a negative or per-level value: its line stays blue
			}
			for (const int32_t stat : g_props[prop].stats) {
				const int32_t shift = static_cast<size_t>(stat) < g_valShift.size() ? g_valShift[stat] : 0;
				const int32_t v = step << shift;
				const auto it = by.find(stat);
				by[stat] = it == by.end() ? v : std::min(it->second, v);
			}
		}
	}
	if (by.empty()) {
		return;
	}
	void* magic = d2r::Engine<void* (*)(Unit*, int32_t, uint32_t) noexcept>(UnitFlagList)(item, 0, ListFlagMagic);
	const auto listValue = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(ListStatValue);
	const auto baseValue = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitBaseStat);
	for (const auto& [stat, step] : by) {
		const int32_t onMagic = magic != nullptr ? listValue(dataFlag, magic, stat, 0) : 0;
		if (onMagic >= step) {
			out->push_back({stat, step, onMagic, magic});
		} else if (const int32_t onBase = baseValue(item, stat, 0); onBase >= step) {
			out->push_back({stat, step, onBase, nullptr});
		}
	}
}

void Set(Unit* item, uint32_t dataFlag, const Lowered& l, int32_t value) noexcept {
	using namespace d2r::rva;
	if (l.list != nullptr) {
		d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat)(dataFlag, l.list, l.stat, value, 0);
	} else {
		d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(item, l.stat, value, 0);
	}
}

void Lines(Unit* item, char* out, int32_t size, int32_t a4, uint32_t a5, int32_t a6, uint32_t a7, uint32_t a8, void* a9,
           uint64_t* a10) noexcept {
	std::vector<Lowered> lowered;
	if (item != nullptr && out != nullptr && size > 0 && At<int32_t>(item, d2r::rva::UnitType) == 4) {
		Collect(item, &lowered);
	}
	if (lowered.empty()) {
		g_lines(item, out, size, a4, a5, a6, a7, a8, a9, a10);
		return;
	}
	g_items.fetch_add(1, std::memory_order_relaxed);
	// the second build gets what the first one got: the buffer's text so far and its own copy of a10, which the build clears
	std::string alt(out, strnlen(out, size));
	alt.resize(size, '\0');
	uint64_t a10Copy[2] = {};
	if (a10 != nullptr) {
		std::memcpy(a10Copy, a10, sizeof(a10Copy));
	}
	g_lines(item, out, size, a4, a5, a6, a7, a8, a9, a10);
	const uint32_t dataFlag = d2r::Engine<uint8_t (*)(Unit*) noexcept>(d2r::rva::UnitDataFlag)(item);
	for (const Lowered& l : lowered) {
		Set(item, dataFlag, l, l.was - l.by);
	}
	g_lines(item, alt.data(), size, a4, a5, a6, a7, a8, a9, a10 != nullptr ? a10Copy : nullptr);
	for (const Lowered& l : lowered) {
		Set(item, dataFlag, l, l.was);
	}
	if (lines::Mark(out, static_cast<size_t>(size), std::string_view(alt.c_str()))) {
		g_marked.fetch_add(1, std::memory_order_relaxed);
	}
}

}  // namespace

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	if (!Load(context)) {
		std::snprintf(report, size, "FAILED (properties.txt or itemstatcost.txt not read, or no corrupt property)");
		return;
	}
	const bool hook = context->InstallInlineHook(PropertyLines, PropertyLinesEntry, sizeof(PropertyLinesEntry), Lines, &g_lines);
	std::snprintf(report, size, hook ? "corrupted items show the lines their corruption changed in red (%zu properties read)"
	                                 : "FAILED (property lines entry bytes differ)", g_props.size());
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "corruption: %u corrupted tooltips built, %u with red lines", g_items.load(), g_marked.load());
}

}  // namespace corruption_marks
