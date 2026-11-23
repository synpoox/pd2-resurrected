// Corruption markers (#2070). PD2 appends a red `*` to the property a corruption added or changed; here the whole line
// is red (the owner's call on #2070), and nothing else is.
//
// How a line is known to be the corruption's: a corruption puts its outcome's code (cubemain's `corrupt` mod value) in
// the item's `corrupted` stat (413). The cubemain rows with that code whose first input matches the item (PD2's own
// check, cube_input_match.h) name the properties the outcome put on; properties.txt names their stats. D2R's property
// lines (0x2dc4b0) are built once as they are, then twice more with each of those stats lowered and raised by its row's
// minimum on the client copy (set back right after each), and a line is red only when neither of those builds has it
// (corruption_lines.h). A merged line (the item's own +200% and the corruption's +40% Enhanced Damage) is therefore red
// as a whole, and a line the corruption did not touch never is, also when the corruption split a group line such as All
// Resistances. When a value cannot be lowered (the stat is not on the item's magic or base list) its line stays blue.
// A line with no number (Cannot Be Frozen) is set to 0 both ways when the corruption is its only source. The + sockets
// outcome's `Socketed (N)` line is not a property line: SocketLine (0x2c1760) reddens it.
// Client copy only, UI thread, as item_quantity's tab counts: the server's item and the save are untouched.
#include "corruption_marks.h"

#include "corruption_lines.h"
#include "cube_input_match.h"
#include "log.h"
#include "pd2_engine.h"
#include "property_ids.h"
#include "sig_hook.h"

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
// (d2rAI-toolkit knowledge/d2r-93847-addresses.md, "Item property lines"; 0x2dc4b0 on 93847). The 0x2da760 it calls
// is D2RLoader's. Found by signature (AGENTS.md): its first 24 bytes match once in the 93847 dump.
constexpr const char* PropertyLinesPattern = "48 89 5c 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8d ac 24 c0 fb ff ff";
constexpr uint8_t PropertyLinesEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
using LinesFn = void (*)(Unit* item, char* out, int32_t size, int32_t a4, uint32_t a5, int32_t a6, uint32_t a7, uint32_t a8,
                         void* a9, uint64_t* a10) noexcept;
LinesFn g_lines = nullptr;
// D2Client InventoryTooltip.cpp (0x2c1760 on 93847): (item, out, ...) appends the `Socketed (%i)` line (or its ethereal
// and plain ethereal forms) and a newline to the tooltip text `out` (0x400 bytes). It reads the item's socket count, not
// a stat, so the property-line builds cannot see a + sockets outcome: its line is marked here instead.
constexpr const char* SocketLinePattern = "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8d ac 24 68 fc ff ff";
constexpr uint8_t SocketLineEntry[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                                       0x48, 0x8d, 0xac, 0x24, 0x68, 0xfc, 0xff, 0xff};
constexpr size_t SocketLineSize = 0x400;
using SocketLineFn = void (*)(Unit* item, char* out, void* a3, uint64_t* a4) noexcept;
SocketLineFn g_socketLine = nullptr;

constexpr int32_t StatCorrupted = 413, StatCorruptor = 414;  // as corruption_roll.cpp
constexpr int32_t PropertyFuncs = 7;

struct Property {
	std::vector<int32_t> stats;  // every stat its functions set
	bool sockets = false;        // property function 14: sets the item's sockets
};
std::vector<Property> g_props;     // by properties.txt row number (propids)
std::vector<int32_t> g_valShift;   // by itemstatcost id
int32_t g_corrupt = -1, g_corruptNum = -1;

std::atomic<uint32_t> g_calls {0}, g_corrupted {0}, g_rows {0}, g_items {0}, g_marked {0}, g_sockets {0};

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
	// D2R numbers stats by row, as properties (the *ID column is only a comment, #1355)
	const int name = stats.Column("Stat"), shift = stats.Column("ValShift");
	std::map<std::string_view, int32_t> statIds;
	for (const auto& row : stats.rows) {
		statIds[lines::Table::Cell(row, name)] = static_cast<int32_t>(g_valShift.size());
		g_valShift.push_back(lines::Number(lines::Table::Cell(row, shift)));
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
			p.sockets = p.sockets || lines::Number(lines::Table::Cell(row, func[i])) == 14;
		}
		if (c == "corrupt") {
			g_corrupt = static_cast<int32_t>(g_props.size()) - 1;
		} else if (c == "corruptnum") {
			g_corruptNum = static_cast<int32_t>(g_props.size()) - 1;
		}
	}
	return g_corrupt >= 0 && g_corruptNum >= 0;
}

// One stat moved by `by` for the comparison builds: on the magic list (list != null) or the base list
struct Lowered {
	int32_t stat, by, was;
	void* list;
};

// The corruption's stats on `item`, each with its row's minimum (shifted by the stat's ValShift). Empty when the
// item is not corrupted, or its outcome has no property that is on the item. *sockets: the outcome added sockets.
auto Collect(Unit* item, std::vector<Lowered>* out, bool* sockets) noexcept -> void {
	using namespace d2r::rva;
	const int32_t outcome = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(item, StatCorrupted, 0);
	if (outcome == 0) {
		return;
	}
	g_corrupted.fetch_add(1, std::memory_order_relaxed);
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
		// sockets not tested: the + sockets outcome gives the item the sockets its row's `nos` refuses
		if (!ours || !pd2::CorruptionShardOk(dataFlag, r) || !pd2::CubeInputMatches(r + CubeInputs, item, false)) {
			continue;
		}
		g_rows.fetch_add(1, std::memory_order_relaxed);
		for (int m = 0; m < 5; ++m) {
			const uint8_t* mod = r + CubeOutMods + m * CubeModSize;
			const int32_t prop = At<int32_t>(mod, 0);
			if (prop == -1) {
				break;
			}
			if (prop == g_corrupt || prop == g_corruptNum || prop < 0 || static_cast<size_t>(prop) >= g_props.size()) {
				continue;
			}
			if (g_props[prop].sockets) {
				*sockets = true;  // its line is SocketLine's
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
	g_calls.fetch_add(1, std::memory_order_relaxed);
	std::vector<Lowered> lowered;
	bool sockets = false;
	if (item != nullptr && out != nullptr && size > 0 && At<int32_t>(item, d2r::rva::UnitType) == 4) {
		Collect(item, &lowered, &sockets);
	}
	if (lowered.empty()) {
		g_lines(item, out, size, a4, a5, a6, a7, a8, a9, a10);
		return;
	}
	g_items.fetch_add(1, std::memory_order_relaxed);
	// the comparison builds get what the first one got: the buffer's text so far and their own copy of a10, which a build clears
	const std::string start(out, strnlen(out, size));
	uint64_t a10Copy[2] = {};
	if (a10 != nullptr) {
		std::memcpy(a10Copy, a10, sizeof(a10Copy));
	}
	g_lines(item, out, size, a4, a5, a6, a7, a8, a9, a10);
	const uint32_t dataFlag = d2r::Engine<uint8_t (*)(Unit*) noexcept>(d2r::rva::UnitDataFlag)(item);
	const auto build = [&](int32_t sign) noexcept {
		std::string text = start;
		text.resize(size, '\0');
		uint64_t copy[2] = {a10Copy[0], a10Copy[1]};
		// the corruption as the stat's only source: 0 both ways, or a line with no number (Cannot Be Frozen) would read
		// the same raised and stay blue
		for (const Lowered& l : lowered) {
			Set(item, dataFlag, l, l.was == l.by ? 0 : l.was + sign * l.by);
		}
		g_lines(item, text.data(), size, a4, a5, a6, a7, a8, a9, a10 != nullptr ? copy : nullptr);
		for (const Lowered& l : lowered) {
			Set(item, dataFlag, l, l.was);
		}
		text.resize(strnlen(text.c_str(), size));
		return text;
	};
	const std::string lower = build(-1), higher = build(1);
	if (lines::Mark(out, static_cast<size_t>(size), lower, higher)) {
		g_marked.fetch_add(1, std::memory_order_relaxed);
	}
}

void SocketLine(Unit* item, char* out, void* a3, uint64_t* a4) noexcept {
	const size_t before = out != nullptr ? strnlen(out, SocketLineSize) : 0;
	g_socketLine(item, out, a3, a4);
	if (item == nullptr || out == nullptr || At<int32_t>(item, d2r::rva::UnitType) != 4) {
		return;
	}
	std::vector<Lowered> unused;
	bool sockets = false;
	Collect(item, &unused, &sockets);
	if (sockets && lines::MarkTail(out, SocketLineSize, before)) {
		g_sockets.fetch_add(1, std::memory_order_relaxed);
	}
}

}  // namespace

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	if (!Load(context)) {
		std::snprintf(report, size, "FAILED (properties.txt or itemstatcost.txt not read, or no corrupt property)");
		return;
	}
	const bool hook = sigscan::HookBySignature(context, "ItemPropertyLines", PropertyLinesPattern, 0, PropertyLinesEntry,
	                                           sizeof(PropertyLinesEntry), Lines, &g_lines) &&
	                  sigscan::HookBySignature(context, "ItemSocketLine", SocketLinePattern, 0, SocketLineEntry,
	                                           sizeof(SocketLineEntry), SocketLine, &g_socketLine);
	std::snprintf(report, size, hook ? "corrupted items show the lines their corruption changed in red (%zu properties read)"
	                                 : "FAILED (a tooltip signature not found once, or its entry bytes differ)", g_props.size());
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "corruption: %u property builds, %u of a corrupted item, %u outcome rows matched, %u with stats lowered, %u with red lines, %u red Socketed lines",
	              g_calls.load(), g_corrupted.load(), g_rows.load(), g_items.load(), g_marked.load(), g_sockets.load());
}

}  // namespace corruption_marks
