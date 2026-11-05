#include "automap.h"

#include "d2r.h"
#include "log.h"

#include <atomic>
#include <cstdio>
#include <new>

namespace automap {
namespace {

// FindCel checks, against D2R's lookup as decompiled for https://github.com/synpoox/pd2-resurrected/issues/212 (rva::AutomapCel)
constexpr Row CheckRows[] = {
	{36, 1, 2, 255, 255, 1, {100, 0, 0, 0}},     // left wall, style 2, any sequence
	{36, 1, 2, 255, 255, 1, {999, 0, 0, 0}},     // shadowed: the first match wins
	{36, 0, 255, 5, 7, 2, {200, 201, 0, 0}},     // floor, any style, sequences 5-7, two cels
	{37, 0, 0, 0, 255, 0, {0, 0, 0, 0}},         // PD2-only art: no cel
};
constexpr auto Find(int32_t type, int32_t tile, uint32_t style, int32_t sequence, uint32_t lo = 0, uint32_t hi = 0)
	-> int32_t {
	uint32_t seed[2] = {lo, hi};
	return FindCel(CheckRows, std::size(CheckRows), type, tile, style, sequence, seed);
}
static_assert(Find(36, 1, 2, 9) == 100);
static_assert(Find(36, 1, 3, 9) == NoCel);  // style must match
static_assert(Find(36, 0, 9, 5) == 200 && Find(36, 0, 9, 7) == 200);  // seed {0, 0} rolls 0: the first cel
static_assert(Find(36, 0, 9, 6, 1) == 201);  // 0x6ac690c5 is odd: the second cel
static_assert(Find(36, 0, 9, 4) == NoCel && Find(36, 0, 9, 8) == NoCel);  // outside start..end
static_assert(Find(37, 0, 0, 3) == NoCel);  // a matching row without cels
static_assert(Find(38, 1, 2, 9) == NoCel);  // another type

struct Rows {
	uint32_t count;
	Row* rows;
};

using LookupFn = uint64_t (*)(int32_t levelType, int32_t tileType, uint32_t style, int32_t sequence) noexcept;
LookupFn g_original = nullptr;
std::atomic<const Rows*> g_rows{nullptr};
std::atomic<uint32_t> g_answered{0};
std::atomic<uint32_t> g_found{0};
std::atomic<uint32_t> g_notReady{0};
std::atomic<uint32_t> g_missed{0};
const D2RL::CustomTableService* g_tables = nullptr;
D2RL::CustomTables::TableHandle g_table = D2RL::CustomTables::InvalidHandle;

// Header only (columns as in Columns and tools/pd2_levels.py automap_rows): the table still loads when a mod ships no automap.txt (types 36+ then draw nothing).
constexpr char EmptyTable[] = "LevelType\tTileType\tStyle\tStartSequence\tEndSequence\tCelCount\tCel1\tCel2\tCel3\tCel4\n";
constexpr const char* TablePath = "data/global/excel/d2rloader/core-ui/automap.txt";

#define AUTOMAP_COLUMN(n, f) \
	D2RL::CustomTables::ColumnDefinition { .structSize = D2RL::CustomTables::ColumnDefinitionSize, .name = n, \
	                                       .type = D2RL::CustomTables::ColumnType::Dword, \
	                                       .offset = static_cast<uint32_t>(offsetof(Row, f)) }
const D2RL::CustomTables::ColumnDefinition Columns[] = {
	AUTOMAP_COLUMN("LevelType", levelType), AUTOMAP_COLUMN("TileType", tileType), AUTOMAP_COLUMN("Style", style),
	AUTOMAP_COLUMN("StartSequence", start), AUTOMAP_COLUMN("EndSequence", end), AUTOMAP_COLUMN("CelCount", celCount),
	AUTOMAP_COLUMN("Cel1", cel[0]), AUTOMAP_COLUMN("Cel2", cel[1]), AUTOMAP_COLUMN("Cel3", cel[2]),
	AUTOMAP_COLUMN("Cel4", cel[3]),
};
#undef AUTOMAP_COLUMN

auto Lookup(int32_t levelType, int32_t tileType, uint32_t style, int32_t sequence) noexcept -> uint64_t {
	if (levelType < d2r::rva::AutomapLevelTypes) {
		return g_original(levelType, tileType, style, sequence);
	}
	g_answered.fetch_add(1, std::memory_order_relaxed);
	const Rows* rows = g_rows.load(std::memory_order_acquire);
	if (rows == nullptr) {
		return static_cast<uint32_t>(NoCel);
	}
	auto* seed = *reinterpret_cast<uint32_t**>(d2r::imageBase + d2r::rva::AutomapSeedPtr);
	const int32_t cel = FindCel(rows->rows, rows->count, levelType, tileType, style, sequence, seed);
	if (cel != NoCel) {
		g_found.fetch_add(1, std::memory_order_relaxed);
	} else if (const uint32_t n = g_missed.fetch_add(1, std::memory_order_relaxed) + 1; n <= 20 || n % 10000 == 0) {
		// not plog::Sample: its 1st/100th/10,000th lines would hide which rows a new level lacks, the first 20 show it
		plog::Info("automap: no cel for type %d tile %d style %u sequence %d (%u so far)", levelType, tileType, style,
		           sequence, n);
	}
	return static_cast<uint32_t>(cel);  // zero-extended, as D2R returns it
}

void __cdecl OnTablesLoaded(const D2RL::PluginContext* context, const D2RL::Lifecycle::DataTablesLoadedEvent* event,
                            void* userData) noexcept {
	(void)event;
	(void)userData;
	using namespace D2RL::CustomTables;
	TableInfo info{.structSize = TableInfoSize};
	if (g_tables->getTableInfo(context, g_table, TableBank::Rotw, &info) != Result::Success ||
	    info.state != TableState::Ready || info.rowSize != sizeof(Row) ||
	    info.byteCount != static_cast<uint64_t>(info.rowCount) * sizeof(Row)) {
		if (plog::Sample(g_notReady)) {
			plog::Warn("automap: table not ready (state %u, row size %u): types 36+ draw nothing",
			           static_cast<uint32_t>(info.state), info.rowSize);
		}
		return;
	}
	auto* rows = new (std::nothrow) Rows{info.rowCount, new (std::nothrow) Row[info.rowCount > 0 ? info.rowCount : 1]};
	if (rows == nullptr || rows->rows == nullptr) {
		delete rows;
		return;
	}
	if (info.rowCount > 0 &&
	    g_tables->copyRows(context, g_table, TableBank::Rotw, info.revision, rows->rows, info.byteCount) != Result::Success) {
		delete[] rows->rows;
		delete rows;
		plog::Warn("automap: copying the table failed");
		return;
	}
	// shortcut: the previous snapshot leaks (a few dozen KB per table reload), since a lookup may still read it
	g_rows.store(rows, std::memory_order_release);
	plog::Info("automap: %u rows for level types 36+", rows->count);
}

}  // namespace

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	const D2RL::ResourceService* resources = nullptr;
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&g_tables) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasCustomTableServiceField(g_tables, D2RL::CustomTableServiceRequiredSize) ||
	    context->QueryService(&resources) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasResourceServiceField(resources, D2RL::ResourceServiceRequiredSize) ||
	    context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		std::snprintf(report, size, "FAILED (no custom table, resource or lifecycle service)");
		return;
	}
	const D2RL::Resources::ResourceRegistration empty{.structSize = D2RL::Resources::ResourceRegistrationSize,
	                                                  .path = TablePath,
	                                                  .bytes = EmptyTable,
	                                                  .byteCount = sizeof(EmptyTable) - 1};
	D2RL::Resources::RegistrationHandle resource = D2RL::Resources::InvalidHandle;
	const D2RL::CustomTables::TableRegistration table{.structSize = D2RL::CustomTables::TableRegistrationSize,
	                                                  .name = "automap",
	                                                  .banks = D2RL::CustomTables::TableBank::Rotw,
	                                                  .rowSize = sizeof(Row),
	                                                  .columns = Columns,
	                                                  .columnCount = static_cast<uint32_t>(std::size(Columns)),
	                                                  .columnStride = D2RL::CustomTables::ColumnDefinitionSize};
	const D2RL::Lifecycle::DataTablesLoadedListener listener{.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
	                                                         .callback = OnTablesLoaded};
	D2RL::Lifecycle::ListenerHandle listening = D2RL::Lifecycle::InvalidHandle;
	if (resources->registerResource(context, &empty, &resource) != D2RL::Resources::Result::Success ||
	    g_tables->registerTable(context, &table, &g_table) != D2RL::CustomTables::Result::Success ||
	    lifecycle->registerDataTablesLoadedListener(context, &listener, &listening) != D2RL::Lifecycle::Result::Success) {
		std::snprintf(report, size, "FAILED (custom table %s not registered)", TablePath);
		return;
	}
	const bool hook = context->InstallInlineHook(d2r::rva::AutomapCel, d2r::rva::AutomapCelEntry,
	                                             sizeof(d2r::rva::AutomapCelEntry), Lookup, &g_original);
	std::snprintf(report, size, "level types 36+ from %s%s", TablePath, hook ? "" : "; hook FAILED (bytes differ)");
}

void Counters(char* line, size_t size) noexcept {
	const Rows* rows = g_rows.load(std::memory_order_acquire);
	std::snprintf(line, size, "automap: %u rows, %u lookups for types 36+, %u drawn", rows != nullptr ? rows->count : 0,
	              g_answered.load(), g_found.load());
}

}  // namespace automap
