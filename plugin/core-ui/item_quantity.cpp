// The quantity display, by a native hook (AGENTS.md: SDK first). Why a hook: D2RLoader 1.3.1's WidgetService has no
// getWidgetScreenRect (64 bytes; core-ui logged `widget 1 (size 64, needs 208)`) and its OverlayService draws nothing,
// so the SDK cannot place or draw the number (#359). The D2RLoader dev says the screen-rect call is coming; then this
// file can go back to the SDK version (WidgetService + InventoryService + OverlayService, commit e8da7c5 of PR #402).
// Everything native sits in this file.
//
// The hook replaces D2R's item draw 0x2a72d0 (widget, item), which every item widget calls once per item: it does
// what D2R does (picture rect from the grid's vtable, then the picture), then for a stackable item draws its quantity
// stat (70) with D2R's text draw 0x902e20, in the picture rect aligned right and bottom. Drawn inside the grid's own
// draw, so tooltips and the cursor item cover it, as they cover the picture.
//
// PD2 (its D2GL renderer, config 6, which `HOTKEY_ItemQuantity` toggles) draws the quantity of every grid item that has
// one, in white over a 60%-black box, at the bottom left. Here: bottom right (#359 asks for it), white with D2R's drop
// shadow instead of the box (docs/deviations.md).
#include "item_quantity.h"
#include "text_font.h"

#include "pd2_engine.h"
#include "log.h"
#include "sig_hook.h"
#include "stash_stack.h"
#include "stash_toml.h"

#include <D2RLPlugin/item.h>
#include <D2RLPlugin/localization.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdio>

namespace item_quantity {

namespace {

using d2r::Unit;

static_assert(TextRect({100, 200, 98, 196}, 6).width == 92 && TextRect({100, 200, 98, 196}, 6).height == 190);
static_assert(TextRect({100, 200, 98, 196}, 6).x == 100 && TextRect({100, 200, 98, 196}, 6).y == 200);

// Grid item draw (widget, item); its first 15 bytes: test rdx,rdx / jz +0xd7 / push rsi / push rdi / sub rsp,0x68.
// The hook never calls the original, so the relative jz in them needs no trampoline.
constexpr uint64_t GridItemDraw = 0x2a72d0;
constexpr uint8_t GridItemDrawEntry[] = {0x48, 0x85, 0xd2, 0x0f, 0x84, 0xd7, 0x00, 0x00, 0x00, 0x56, 0x57, 0x48, 0x83, 0xec, 0x68};
// What 0x2a72d0 calls, in its order
constexpr uint64_t WidgetScale = 0x1e6750;  // (parent widget) -> UI scale; null parent: the float at 0x1cba904
constexpr uint64_t BaseScale = 0x1cba904;
constexpr uint64_t ItemCells = 0x371850;    // (item, u8* width, u8* height, file, line): items record +0x11e/+0x11f
constexpr uint32_t ItemInfoSlot = 0xc0 / 8;   // grid vtable: (widget, out, item) -> first qword: the item's cell
constexpr uint32_t PlacementSlot = 0xf8 / 8;  // (widget, int32 rect[4] out, height << 32 | width, cell) -> rect
constexpr uint64_t ItemPicture = 0x15bb80;  // (item, y << 32 | x, scale, out)
// D2R's text draw (TextBoxWidget::Draw 0x86d410 calls it): (text, int32 rect[4] screen px, D2WinFont*, scale)
constexpr uint64_t DrawText = 0x902e20;
// Numbers only on InventoryGridWidget (inventory, cube, stash pages, trade and vendor grids, HD and legacy layouts):
// the stash's Gems/Materials/Runes tabs (AdvancedStashSlotWidget, vtable 0x1cf4678) draw D2R's own count, and the
// equipment slots, belt and others are not grids.
constexpr uint64_t InventoryGridVtable = 0x1cf3800;
constexpr int32_t StatQuantity = 70;
constexpr uint32_t RecordStackable = 0x142;  // items record: the save writer 0x37d140 writes quantity only when set
constexpr uint32_t RecordCode = 0x80;        // items record: the item code (pd2_engine.h ItemRecord)

using textfont::Font;
constexpr int32_t PointSize = 30;  // HD cell is 98 layout units; the HUD's small text is 22
constexpr Font QuantityFont = textfont::Make(PointSize, 2, 2);  // right, bottom
static_assert(QuantityFont.v[1] == PointSize && QuantityFont.v[0xa] == 2 && QuantityFont.v[0x28] == 0xe);

using ScaleFn = float (*)(void* parent) noexcept;
using CellsFn = void (*)(Unit* item, uint8_t* width, uint8_t* height, const char* file, int32_t line) noexcept;
using InfoFn = uint64_t* (*)(void* widget, void* out, Unit* item) noexcept;
using PlaceFn = int32_t* (*)(void* widget, int32_t* rect, uint64_t size, uint64_t cell) noexcept;
using PictureFn = void (*)(Unit* item, uint64_t position, float scale, void* out) noexcept;
using DrawTextFn = void (*)(const char* text, const int32_t* rect, const Font* font, float scale) noexcept;

std::atomic<uint32_t> g_drawn {0}, g_numbers {0}, g_faults {0};
std::atomic<bool> g_on {true};  // off after a fault in the number draw: pictures still draw

template <class T>
auto Slot(void* object, uint32_t slot) noexcept -> T {
	return reinterpret_cast<T>((*reinterpret_cast<void***>(object))[slot]);
}

// The number of a stack (0: none) and its text rect
auto Quantity(Unit* item) noexcept -> int32_t {
	using namespace d2r::rva;
	if (d2r::At<int32_t>(item, UnitType) != 4) {
		return 0;
	}
	const int32_t row = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitClassOf)(item, __FILE__, __LINE__);
	const uint8_t flag = d2r::Engine<uint8_t (*)(Unit*) noexcept>(UnitDataFlag)(item);
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ItemRecord)(flag, row);
	if (record == nullptr || record[RecordStackable] == 0) {
		return 0;
	}
	// the owner's call on #359: tomes (PD2's only type `book` rows) keep their charges to the tooltip
	const uint32_t code = *reinterpret_cast<const uint32_t*>(record + RecordCode);
	if (code == D2RL::Items::MakeItemCode("tbk") || code == D2RL::Items::MakeItemCode("ibk")) {
		return 0;
	}
	return d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(item, StatQuantity, 0);
}

void DrawNumber(Unit* item, const int32_t* picture, uint8_t height, float scale) noexcept {
	__try {
		const int32_t quantity = Quantity(item);
		if (quantity <= 0) {
			return;
		}
		char text[12];
		std::snprintf(text, sizeof(text), "%d", quantity);
		const int32_t pad = height != 0 ? picture[3] / height * 6 / 100 : 0;
		const Rect r = TextRect({picture[0], picture[1], picture[2], picture[3]}, pad);
		const int32_t rect[4] = {r.x, r.y, r.width, r.height};
		d2r::Engine<DrawTextFn>(DrawText)(text, rect, &QuantityFont, scale);
		g_numbers.fetch_add(1, std::memory_order_relaxed);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_on = false;
		g_faults.fetch_add(1);
	}
}

// 0x2a72d0, as D2R does it, then the number
void Draw(void* widget, Unit* item) noexcept {
	if (item == nullptr) {
		return;
	}
	auto* parent = d2r::At<void*>(widget, 0x30);
	float scale = parent != nullptr ? d2r::Engine<ScaleFn>(WidgetScale)(parent) : d2r::At<float>(reinterpret_cast<void*>(d2r::imageBase), static_cast<int>(BaseScale));
	scale *= d2r::At<float>(widget, 0x80);
	uint8_t width = 0, height = 0;
	d2r::Engine<CellsFn>(ItemCells)(item, &width, &height, __FILE__, __LINE__);
	uint64_t info[4] {};
	const uint64_t cell = *Slot<InfoFn>(widget, ItemInfoSlot)(widget, info, item);
	int32_t picture[4] {};
	Slot<PlaceFn>(widget, PlacementSlot)(widget, picture, static_cast<uint64_t>(height) << 32 | width, cell);
	uint8_t out[8] {};
	d2r::Engine<PictureFn>(ItemPicture)(item, static_cast<uint64_t>(static_cast<uint32_t>(picture[1])) << 32 | static_cast<uint32_t>(picture[0]), scale, out);
	g_drawn.fetch_add(1, std::memory_order_relaxed);
	if (g_on.load(std::memory_order_relaxed) && d2r::At<uint64_t>(widget, 0) == d2r::imageBase + InventoryGridVtable) {
		DrawNumber(item, picture, height, scale);
	}
}

// The Gems/Materials/Runes tab tooltip (#1827). A tab slot is one item with a count byte (item data +0x9c, the cell's
// number); core-rules' deposit (stash_stacks.cpp) raises that count but the slot item keeps the first stack's quantity
// stat, so D2R's "Quantity: %d of %d" line (ItemStats1i, 0x2c1e50: stat 70 of the item's maxstack) read e.g. 20 of 50 on a
// cell of 32. For a slot item the line is written from the count and set_materials_limit instead. Display only: the
// stat, the save, and the deposit and withdraw are untouched. Why a hook: the SDK's tooltip listener only adds lines.
constexpr uint8_t Stats1iEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x56, 0x48, 0x81, 0xec, 0x40, 0x01, 0x00, 0x00};
constexpr const char* Stats1iPattern =
    "48 89 5c 24 20 56 48 81 ec 40 01 00 00 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 84 24 30 01 00 00 45 33 c0 48 8b f2 48 8b d9 "
    "41 8d 50 46 e8 ?? ?? ?? ?? 85 c0 7f ?? 48 8b cb e8";
constexpr int32_t TabCountByte = 0x9c;  // item data (unit +0x10), as D2R's own count read (0x46d9a0)
constexpr uint32_t LineSize = 0x400;    // the tooltip buffer the line is appended to (strcat_s size in 0x2c1e50)
using Stats1iFn = void (*)(Unit* item, char* out) noexcept;
Stats1iFn g_stats1i = nullptr;
const D2RL::PluginContext* g_context = nullptr;
const D2RL::LocalizationService* g_strings = nullptr;
int32_t g_limit = pd2::stashstack::DefaultLimit;
std::atomic<uint32_t> g_tabLines {0};

auto TabCount(Unit* item) noexcept -> uint8_t {
	if (item == nullptr || d2r::At<int32_t>(item, d2r::rva::UnitType) != 4) {
		return 0;
	}
	void* data = d2r::At<void*>(item, 0x10);
	return data != nullptr ? d2r::At<uint8_t>(data, TabCountByte) : 0;
}

void Stats1i(Unit* item, char* out) noexcept {
	const uint8_t count = TabCount(item);
	char format[128] {}, newline[16] {};
	uint32_t need = 0;
	if (count == 0 || g_strings->getStringByKey(g_context, "ItemStats1i", format, sizeof(format), &need) != D2RL::Localization::Result::Success ||
	    !pd2::stashstack::TwoInts(format) ||
	    g_strings->getStringByKey(g_context, "newline", newline, sizeof(newline), &need) != D2RL::Localization::Result::Success) {
		g_stats1i(item, out);
		return;
	}
	char line[192];
	std::snprintf(line, sizeof(line), format, static_cast<int32_t>(count), g_limit);
	strcat_s(out, LineSize, line);
	strcat_s(out, LineSize, newline);
	g_tabLines.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

void InstallTabTooltip(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	g_context = context;
	int32_t raw = -1;
	pd2::stash_toml::Resolve(context->pluginConfigPath, "set_materials_limit", &raw);
	g_limit = pd2::stashstack::LimitOf(raw);
	const bool ok = context->QueryService(&g_strings) == D2RL::ServiceQueryResult::Success &&
	                D2RL::HasLocalizationServiceField(g_strings, D2RL::LocalizationServiceRequiredSize) &&
	                sigscan::HookBySignature(context, "ItemStats1i", Stats1iPattern, 0, Stats1iEntry, sizeof(Stats1iEntry), Stats1i, &g_stats1i);
	std::snprintf(report, size, ok ? "tab tooltips show the cell count of %d" : "FAILED (no localization service or ItemStats1i signature)", g_limit);
}

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	const bool hook = context->InstallInlineHook(GridItemDraw, GridItemDrawEntry, sizeof(GridItemDrawEntry), Draw);
	std::snprintf(report, size, hook ? "stacks in item grids show their quantity" : "FAILED (grid item draw bytes differ)");
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "quantity: %u items drawn, %u numbers, %u faults%s, %u tab tooltip lines", g_drawn.load(), g_numbers.load(),
	              g_faults.load(), g_on.load() ? "" : " (numbers off)", g_tabLines.load());
}

}  // namespace item_quantity
