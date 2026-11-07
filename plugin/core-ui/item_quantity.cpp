// The quantity display, all through the PluginSDK (no D2R hook): WidgetService gives each open grid's screen rect (HD
// and legacy layouts both name it `grid`), InventoryService the player's items and their cells, DataTableService the
// items table (stackable flag and size in cells), and OverlayService draws the numbers after the game UI.
//
// PD2 (its D2GL renderer, config 6, which `HOTKEY_ItemQuantity` toggles) draws the item's quantity stat whenever it is
// not 0, so on every stackable item (stat 70 exists only on those), in white over a 60%-black box, at the bottom left of
// the picture. Here the number goes at the bottom right (#359 asks for it) and the rest is PD2's.
//
// Blocked on D2RLoader 1.3.1-beta (slot 2, 2026-11-06): its WidgetService is 64 bytes (no getWidgetState, getText or
// getWidgetScreenRect), so the part reports FAILED and stays off; and its overlay draws nothing on screen (the
// callback runs and the calls return Success). It switches on by itself on a loader that has both.
//
// shortcut: vendor grids (NPC items: InventoryService lists the player's own) and D2RLoader custom pages (their panel
// names are the page owner's) show no number; upgrade with a native read of the drawn grid if players miss them.
// shortcut: the overlay draws over the game UI, so a tooltip or the cursor item over a stack still shows its number;
// upgrade with a native draw inside the grid if that bothers players.
#include "item_quantity.h"

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <D2RLPlugin/data_tables.h>
#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/overlay.h>
#include <D2RLPlugin/threads.h>
#include <D2RLPlugin/widgets.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace item_quantity {

namespace {

namespace items = D2RL::Items;
namespace widgets = D2RL::Widgets;
using C = items::ItemContainer;

static_assert(SpotOf(100, 200, 50, 0, 0, 1, 1, 4).right == 146 && SpotOf(100, 200, 50, 0, 0, 1, 1, 4).bottom == 246);
static_assert(SpotOf(0, 0, 10, 2, 1, 1, 3, 0).right == 30 && SpotOf(0, 0, 10, 2, 1, 1, 3, 0).bottom == 40);
static_assert(PageOf("Shared 2") == 1 && PageOf("3 / 7") == 2 && PageOf("Shared") == UINT32_MAX && PageOf(nullptr) == UINT32_MAX);

// The compiled items record (build 93847): +0x80 code, +0x11e inventory width and height in cells (D2Common Items.cpp's
// socket cap at 0x36e880), +0x142 stackable (the save writer 0x37d140 writes 9 quantity bits only when it is set).
constexpr uint32_t RecordCode = 0x80, RecordWidth = 0x11e, RecordHeight = 0x11f, RecordStackable = 0x142;

struct Shape {
	uint8_t stackable, width, height;
};
constexpr uint32_t MaxClasses = 4096;
Shape g_shapes[MaxClasses] {};
std::atomic<uint32_t> g_classes {0};  // set once g_shapes holds that many rows (game thread writes, UI thread reads)
std::atomic<bool> g_loading {false};

// One stack's number on screen
struct Label {
	float right, bottom, size;
	char text[8];
	uint32_t length;
};
constexpr uint32_t MaxLabels = 512;
Label g_ui[MaxLabels];  // UI thread builds here, then copies under the lock
uint32_t g_uiCount = 0;
Label g_drawn[MaxLabels];
uint32_t g_drawnCount = 0;
SRWLOCK g_lock = SRWLOCK_INIT;

// One open grid, as the UI tick found it
struct Grid {
	const char* panel;
	C container;
	int32_t columns;
	bool stash;
};
constexpr Grid Grids[] = {
	{"PlayerInventoryExpansionLayout", C::Inventory, 10, false},
	{"PlayerInventoryOriginalLayout", C::Inventory, 10, false},
	{"HoradricCubeLayout", C::Cube, 3, false},
	{"BankExpansionLayout", C::PersonalStash, 10, true},
	{"BankOriginalLayout", C::PersonalStash, 6, true},
};
struct Open {
	C container;
	uint32_t page;  // shared stash page, else 0
	float left, top, cell;
};
Open g_open[8];
uint32_t g_openCount = 0;

const D2RL::PluginContext* g_context = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
const D2RL::WidgetService* g_widgets = nullptr;
const D2RL::OverlayService* g_overlay = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
bool g_watching = false;
uint32_t g_ticks = 0, g_frames = 0, g_lastGrids = 0, g_lastLabels = 0;  // the status line reads them racily
char g_lastRect[160] = "none yet";

template <class T>
auto At(const uint8_t* row, uint32_t offset) noexcept -> T {
	T value;
	std::memcpy(&value, row + offset, sizeof(T));
	return value;
}

// Game thread: the stackable flag and size of every items row, once.
void LoadShapes(const D2RL::PluginContext*, void*) noexcept {
	D2RL::DataTables::TableView view {.structSize = D2RL::DataTables::TableViewSize};
	const auto result = g_tables->getTable(g_context, D2RL::DataTables::Bank::Rotw, D2RL::DataTables::TableId::Items, &view);
	if (result != D2RL::DataTables::Result::Success || view.rows == nullptr || view.rowSize <= RecordStackable) {
		g_loading = false;
		plog::Info("quantity: no items table (result %u, row size %u)", static_cast<uint32_t>(result), view.rowSize);
		return;
	}
	const uint32_t count = view.rowCount < MaxClasses ? view.rowCount : MaxClasses;
	uint32_t stackables = 0;
	for (uint32_t i = 0; i < count; ++i) {
		const auto* row = static_cast<const uint8_t*>(view.rows) + static_cast<size_t>(i) * view.rowSize;
		g_shapes[i] = {row[RecordStackable], row[RecordWidth], row[RecordHeight]};
		stackables += row[RecordStackable] != 0 ? 1 : 0;
		const uint32_t code = At<uint32_t>(row, RecordCode);
		if (code == items::MakeItemCode("r01") || code == items::MakeItemCode("jav") || code == items::MakeItemCode("rin") ||
		    code == items::MakeItemCode("tbk")) {
			plog::Info("quantity: row %u code %.4s stackable %u size %ux%u", i, reinterpret_cast<const char*>(row + RecordCode),
			           row[RecordStackable], row[RecordWidth], row[RecordHeight]);
		}
	}
	g_classes = count;
	plog::Info("quantity: %u items rows (row size %u), %u stackable", count, view.rowSize, stackables);
}

auto Visible(widgets::WidgetHandle widget) noexcept -> bool {
	widgets::State state {};
	return g_widgets->getWidgetState(g_context, widget, &state) == widgets::Result::Success && state.visible;
}

// UI thread: which grids are open, and where.
void FindGrids() noexcept {
	g_openCount = 0;
	for (const Grid& grid : Grids) {
		widgets::WidgetHandle panel = widgets::InvalidHandle, cells = widgets::InvalidHandle;
		if (g_widgets->findPanel(g_context, grid.panel, &panel) != widgets::Result::Success || !Visible(panel) ||
		    g_widgets->findWidget(g_context, panel, "grid", &cells) != widgets::Result::Success || !Visible(cells)) {
			continue;
		}
		widgets::ScreenRect rect {};
		if (g_widgets->getWidgetScreenRect(g_context, cells, &rect) != widgets::Result::Success || rect.width <= 0) {
			continue;
		}
		Open open {grid.container, 0, rect.x, rect.y, rect.width / static_cast<float>(grid.columns)};
		if (grid.stash) {
			widgets::WidgetHandle shared = widgets::InvalidHandle, label = widgets::InvalidHandle;
			if (g_widgets->findWidget(g_context, panel, "SharedStashTabContainer", &shared) == widgets::Result::Success &&
			    Visible(shared)) {
				char text[64] = "";
				uint32_t needed = 0;
				open.container = C::SharedStash;
				open.page = g_widgets->findWidget(g_context, shared, "CurrentSharedStashText", &label) == widgets::Result::Success &&
				                    g_widgets->getText(g_context, label, text, sizeof(text), &needed) == widgets::Result::Success
				                ? PageOf(text)
				                : 0;
			}
		}
		std::snprintf(g_lastRect, sizeof(g_lastRect), "%s %.0f,%.0f %.0fx%.0f cell %.1f container %u page %u", grid.panel,
		              rect.x, rect.y, rect.width, rect.height, open.cell, static_cast<uint32_t>(open.container), open.page);
		if (g_openCount < std::size(g_open)) {
			g_open[g_openCount++] = open;
		}
	}
}

auto AddLabel(const D2RL::PluginContext*, const items::ItemInfo* item, void*) noexcept -> D2RL::Inventory::IterationAction {
	if (item->classId >= g_classes.load(std::memory_order_acquire) || g_shapes[item->classId].stackable == 0 ||
	    g_uiCount >= MaxLabels) {
		return D2RL::Inventory::IterationAction::Continue;
	}
	const Shape shape = g_shapes[item->classId];
	for (uint32_t i = 0; i < g_openCount; ++i) {
		const Open& open = g_open[i];
		if (open.container != item->container || (open.container == C::SharedStash && open.page != item->sharedStashPage)) {
			continue;
		}
		const Spot spot = SpotOf(open.left, open.top, open.cell, item->x, item->y, shape.width, shape.height, open.cell * 0.06f);
		Label& label = g_ui[g_uiCount++];
		label.right = spot.right;
		label.bottom = spot.bottom;
		label.size = open.cell * 0.34f;
		const int written = std::snprintf(label.text, sizeof(label.text), "%d", item->quantity);
		label.length = written > 0 ? static_cast<uint32_t>(written) : 0;
		break;
	}
	return D2RL::Inventory::IterationAction::Continue;
}

void Watch(const D2RL::PluginContext*, void*) noexcept {
	g_watching = g_threads->runOnUiThread(g_context, Watch, nullptr) == D2RL::Threads::Result::Success;  // again next update
	++g_ticks;
	g_uiCount = 0;
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (g_classes.load(std::memory_order_acquire) != 0 &&
	    g_inventory->getLocalPlayer(g_context, &player) == D2RL::Inventory::Result::Success) {
		FindGrids();
		if (g_openCount != 0) {
			uint32_t mask = 0;
			for (uint32_t i = 0; i < g_openCount; ++i) {
				mask |= items::ContainerBit(g_open[i].container);
			}
			const D2RL::Inventory::ItemFilter filter {.structSize = D2RL::Inventory::ItemFilterSize, .containerMask = mask};
			g_inventory->forEachInventoryItem(g_context, player, &filter, AddLabel, nullptr);
		}
	}
	if (g_openCount != g_lastGrids || g_uiCount != g_lastLabels) {
		plog::Info("quantity: %u grids open, %u numbers; last grid %s", g_openCount, g_uiCount, g_lastRect);
		g_lastGrids = g_openCount;
		g_lastLabels = g_uiCount;
	}
	AcquireSRWLockExclusive(&g_lock);
	std::memcpy(g_drawn, g_ui, sizeof(Label) * g_uiCount);
	g_drawnCount = g_uiCount;
	ReleaseSRWLockExclusive(&g_lock);
}

// Overlay thread: draws what the last UI tick found.
void __cdecl Draw(const D2RL::PluginContext* context, const D2RL::Overlay::Frame* frame, void*) noexcept {
	++g_frames;
	AcquireSRWLockShared(&g_lock);
	for (uint32_t i = 0; i < g_drawnCount; ++i) {
		const Label& label = g_drawn[i];
		const D2RL::Overlay::MeasureTextRequest measure {.structSize = D2RL::Overlay::MeasureTextRequestSize,
		                                                 .canvas = frame->canvas,
		                                                 .textSize = label.size,
		                                                 .text = label.text,
		                                                 .textLength = label.length};
		D2RL::Overlay::TextMetrics metrics {.structSize = D2RL::Overlay::TextMetricsSize};
		if (g_overlay->measureText(context, &measure, &metrics) != D2RL::Overlay::Result::Success) {
			continue;
		}
		const float left = label.right - metrics.width, top = label.bottom - metrics.height;
		const D2RL::Overlay::FilledRectangleRequest box {.structSize = D2RL::Overlay::FilledRectangleRequestSize,
		                                                 .canvas = frame->canvas,
		                                                 .rect = {left - 2, top, label.right + 2, label.bottom},
		                                                 .color = {0, 0, 0, 0.6f}};  // PD2's 0x99 alpha black
		g_overlay->drawFilledRectangle(context, &box);
		const D2RL::Overlay::TextRequest text {.structSize = D2RL::Overlay::TextRequestSize,
		                                       .canvas = frame->canvas,
		                                       .position = {left, top},
		                                       .color = {1, 1, 1, 1},  // PD2's font color 0, white
		                                       .textSize = label.size,
		                                       .text = label.text,
		                                       .textLength = label.length};
		g_overlay->drawText(context, &text);
	}
	ReleaseSRWLockShared(&g_lock);
}

// UI thread. The SDK refuses UI work queued during plugin load, so the watch starts with the first game.
void __cdecl Start(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	if (g_classes.load() == 0 && !g_loading.exchange(true) &&
	    g_threads->runOnGameThread(context, LoadShapes, nullptr) != D2RL::Threads::Result::Success) {
		g_loading = false;
		plog::Info("quantity: could not queue the items table read");
	}
	if (!g_watching) {
		Watch(context, nullptr);
	}
}

template <class Service>
auto Query(const Service*& service, bool (*has)(const Service*, uint32_t), uint32_t required) noexcept -> bool {
	if (g_context->QueryService(&service) != D2RL::ServiceQueryResult::Success || !has(service, required)) {
		service = nullptr;
	}
	return service != nullptr;
}

auto HasTables(const D2RL::DataTableService* service, uint32_t size) noexcept -> bool {
	return service != nullptr && service->serviceVersion == D2RL::DataTableService::AbiVersion && service->serviceSize >= size;
}

}  // namespace

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	g_context = context;
	const D2RL::LifecycleService* lifecycle = nullptr;
	// `&`, not `&&`: every service is queried, so the report never blames a missing one by mistake
	if (!(Query(g_inventory, D2RL::HasInventoryServiceField, D2RL::InventoryServiceRequiredSize) &
	      Query(g_threads, D2RL::HasThreadServiceField, D2RL::ThreadServiceRequiredSize) &
	      Query(g_widgets, D2RL::HasWidgetServiceField, D2RL::WidgetServiceGetWidgetScreenRectFieldEnd) &
	      Query(g_overlay, D2RL::HasOverlayServiceField, D2RL::OverlayServiceRequiredSize) &
	      Query(g_tables, HasTables, static_cast<uint32_t>(sizeof(D2RL::DataTableService))) &
	      Query(lifecycle, D2RL::HasLifecycleServiceField, D2RL::LifecycleServiceRequiredSize))) {
		const D2RL::WidgetService* widgetService = nullptr;
		(void)context->QueryService(&widgetService);
		std::snprintf(report, size, "FAILED (missing: inventory %d, thread %d, widget %d (size %u, needs %u), overlay %d, data table %d, lifecycle %d)",
		              g_inventory == nullptr, g_threads == nullptr, g_widgets == nullptr,
		              widgetService != nullptr ? widgetService->serviceSize : 0, D2RL::WidgetServiceGetWidgetScreenRectFieldEnd,
		              g_overlay == nullptr, g_tables == nullptr, lifecycle == nullptr);
		return;
	}
	const D2RL::Overlay::CallbackRegistration draw {.structSize = D2RL::Overlay::CallbackRegistrationSize,
	                                                .phase = D2RL::Overlay::Phase::AfterGameUi,
	                                                .callback = Draw};
	D2RL::Overlay::CallbackHandle drawing = D2RL::Overlay::InvalidCallbackHandle;
	if (g_overlay->registerFrameCallback(context, &draw, &drawing) != D2RL::Overlay::Result::Success) {
		std::snprintf(report, size, "FAILED (no overlay callback)");
		return;
	}
	const D2RL::Lifecycle::GameplayEventListener listener {.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
	                                                       .kind = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady,
	                                                       .callback = Start};
	D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
	if (lifecycle->registerGameplayEventListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
		std::snprintf(report, size, "FAILED (no player-ready listener)");
		return;
	}
	std::snprintf(report, size, "stacks in the inventory, cube and stash show their quantity");
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "quantity: %u items rows, watch updates %u, frames %u, %u grids open, %u numbers; last grid %s",
	              g_classes.load(), g_ticks, g_frames, g_lastGrids, g_lastLabels, g_lastRect);
}

}  // namespace item_quantity
