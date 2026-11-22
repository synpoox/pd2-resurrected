// Lock Inventory (#1578). The padlock is two ButtonWidgets at one spot in the HD and legacy inventory layouts, InventoryLock
// (unlocked art) and InventoryLockOn (locked); a frame watch shows only the one matching the lock, because this loader's
// widget service cannot set a toggle button's state (the controller layout keeps one button, for the pad focus, under a
// locked-art image the watch shows, #1675). Their click message (PanelManager:OpenPanel:core-ui/InventoryLock,
// caught and consumed like the altar's confirm) flips the lock. The lock stays across games and is saved per character
// (config/inventory-lock-<name>.locked exists while locked; inventory-lock.locked holds the last lock, which the
// padlock shows on entry until the character is read), as PD2 keeps it in its settings and sends it
// to the server (ProjectDiablo.dll 0x102d15c0, packet 0x5c), whose wrappers of the body-slot handlers refuse while it is
// on (0x10394bd0 and others) and play the "I can't" sound.
//
// Native: no SDK service sees a body-slot change. ItemInteractionService skips equipment and cursor paths, the
// inventory item policies apply to custom pages only, and InputService sees bindings, not slot clicks. So the server's
// client-message dispatch (PlrMsg, 93847 0x4f2fa0, by signature) drops the messages that change a body slot
// (ChangesEquipment) from the local player while locked; clicks, Ctrl/Shift clicks and the pad all send those. The
// client marks the item pending and its timer clears it, as for any refused move.
//
// shortcut: single player and the host only (a TCP/IP client runs no server here, so its padlock does nothing);
// upgrade with a plugin message to the host if online play needs it.
// shortcut: no "I can't" sound on a refused move. The charm rows (#1727) are locked by the 0x19 take and 0x18 put messages.
#include "inventory_lock.h"

#include "log.h"
#include "pd2_engine.h"
#include "stash_affinity.h"
#include "stash_home.h"
#include "sig_hook.h"

#include <D2RLPlugin/lifecycle.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/overlay.h>
#include <D2RLPlugin/shared_events.h>
#include <D2RLPlugin/widgets.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include <windows.h>

namespace inventory_lock {

namespace {

using d2r::Game;
using d2r::Unit;

constexpr uint8_t Equip[] = {0x1a, 0, 0, 0, 0, 4, 0, 0, 0};
constexpr uint8_t ShiftUnequip[] = {0x58, 1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t MercSwap[] = {0x51, 1, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t MercToGrid[] = {0x5b, 1, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t DropWorn[] = {0x5c, 1, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 1, 0, 0, 0};
constexpr uint8_t DropFromGrid[] = {0x5c, 1, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 7, 0, 0, 0};
constexpr uint8_t GridList[] = {0x22, 0xff, 0xff, 0xff, 0xff, 7, 0, 0, 0, 0};
constexpr uint8_t BodyList[] = {0x22, 7, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0};
constexpr auto ListOf(uint8_t mode) noexcept {
	struct { uint8_t b[0x20] {}; } p;
	p.b[0] = 0x60;
	p.b[0x15] = mode;
	return p;
}
constexpr uint8_t UseItem[] = {0x26, 0, 0, 0, 0};
constexpr uint8_t Insert[] = {0x18, 0, 0, 0, 0};
static_assert(ChangesEquipment(Equip, sizeof Equip));
static_assert(ChangesEquipment(ShiftUnequip, sizeof ShiftUnequip));
static_assert(ChangesEquipment(MercSwap, sizeof MercSwap) && ChangesEquipment(MercToGrid, sizeof MercToGrid));
static_assert(ChangesEquipment(DropWorn, sizeof DropWorn));
static_assert(!ChangesEquipment(DropFromGrid, sizeof DropFromGrid));  // a drop from a page is not a body slot
static_assert(!ChangesEquipment(DropWorn, 0x0c));  // too short to hold the page
static_assert(!ChangesEquipment(GridList, sizeof GridList));  // a grid's list move: not a body slot
static_assert(ChangesEquipment(BodyList, sizeof BodyList));
static_assert(ChangesEquipment(ListOf(1).b, 0x20) && ChangesEquipment(ListOf(2).b, 0x20));
static_assert(!ChangesEquipment(ListOf(0).b, 0x20) && !ChangesEquipment(ListOf(3).b, 0x20));
static_assert(!ChangesEquipment(ListOf(1).b, 0x15));  // too short to hold the mode
static_assert(!ChangesEquipment(UseItem, sizeof UseItem) && !ChangesEquipment(Insert, sizeof Insert));
constexpr uint8_t TakeIt[] = {0x19, 7, 0, 0, 0};
constexpr uint8_t PutLow[] = {0x18, 7, 0, 0, 0, 3, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t PutHigh[] = {0x18, 7, 0, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t PutStash[] = {0x18, 7, 0, 0, 0, 3, 0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0};
constexpr auto RowIs3 = [](uint32_t) { return 3; };
constexpr auto RowIs4 = [](uint32_t) { return 4; };
constexpr auto NoRow = [](uint32_t) { return -1; };
constexpr uint8_t SwapIn[] = {0x1f, 9, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0};
constexpr uint8_t CtrlDrop[] = {0x5c, 7, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 9, 0, 4, 0};
constexpr auto OnlyId7Row4 = [](uint32_t id) { return id == 7 ? 4 : -1; };
static_assert(TouchesCharmRows(SwapIn, sizeof SwapIn, OnlyId7Row4) && !TouchesCharmRows(SwapIn, sizeof SwapIn, NoRow));
constexpr auto GridMove = [] { auto p = ListOf(0); p.b[1] = 7; for (int i = 5; i < 9; ++i) p.b[i] = 0xff; p.b[0x0d] = 7; return p; }();
constexpr auto PlayerIdOnly = [] { auto p = ListOf(0); p.b[1] = 7; return p; }();
static_assert(TouchesCharmRows(GridMove.b, 0x20, OnlyId7Row4));  // 0x60 mode 0: the item at +0x0d
static_assert(!TouchesCharmRows(PlayerIdOnly.b, 0x20, OnlyId7Row4));  // 0x60 +1 is a player id, never asked
static_assert(TouchesCharmRows(CtrlDrop, sizeof CtrlDrop, OnlyId7Row4) && !TouchesCharmRows(CtrlDrop, 4, OnlyId7Row4));
static_assert(TouchesCharmRows(TakeIt, sizeof TakeIt, RowIs4) && !TouchesCharmRows(TakeIt, sizeof TakeIt, RowIs3));
static_assert(!TouchesCharmRows(TakeIt, sizeof TakeIt, NoRow) && !TouchesCharmRows(TakeIt, 4, RowIs4));
static_assert(TouchesCharmRows(PutLow, sizeof PutLow, NoRow) && !TouchesCharmRows(PutHigh, sizeof PutHigh, NoRow));
static_assert(!TouchesCharmRows(PutStash, sizeof PutStash, NoRow) && !TouchesCharmRows(PutLow, 16, NoRow));
namespace aff = pd2::affinity;
// the Maps and Bossing tabs refuse a drop that is not the item's own cell (#1726)
constexpr auto Own = [](uint32_t code, uint32_t shown, uint32_t page, int32_t x, int32_t y) {
	return aff::AtHome(aff::HomeOf(code), code, shown, true, page, x, y);
};
constexpr auto Nop = [](uint32_t, uint32_t, int32_t, int32_t) { return true; };
constexpr auto Never = [](uint32_t, uint32_t, int32_t, int32_t) { return false; };
constexpr uint8_t SwapStash[] = {0x1f, 9, 0, 0, 0, 7, 0, 0, 0, 4, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0};
constexpr uint8_t PutGem[] = {0x18, 7, 0, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0};
constexpr uint8_t PutBoss[] = {0x18, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0};
constexpr uint8_t PutInv[] = {0x18, 7, 0, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0};
constexpr auto GemPut = [](uint32_t, int32_t x, int32_t y) { return Own(aff::Code("gcv"), 0, aff::MapsPage, x, y); };
constexpr auto MapPut = [](uint32_t, int32_t x, int32_t y) { return Own(aff::Code("m04"), aff::Code("m04"), aff::MapsPage, x, y); };
constexpr auto BossPut = [](uint32_t, int32_t x, int32_t y) { return Own(aff::Code("dcma"), 0, aff::BossingPage, x, y); };
constexpr auto WrongBossPut = [](uint32_t, int32_t x, int32_t y) { return Own(aff::Code("dcbl"), 0, aff::BossingPage, x, y); };
static_assert(MisplacedOnTab(PutGem, sizeof PutGem, aff::MapsPage, GemPut, Nop));   // a 1x1 gem into a map cell: refused
static_assert(!MisplacedOnTab(PutGem, sizeof PutGem, aff::MapsPage, MapPut, Nop));  // a map of the shown base into its cell
static_assert(!MisplacedOnTab(PutBoss, sizeof PutBoss, aff::BossingPage, BossPut, Nop));  // a boss item into its own cell
static_assert(MisplacedOnTab(PutBoss, sizeof PutBoss, aff::BossingPage, WrongBossPut, Nop));  // dcbl into dcma's cell: refused
static_assert(!MisplacedOnTab(PutGem, sizeof PutGem, 0, GemPut, Nop));  // no tab open
static_assert(!MisplacedOnTab(PutInv, sizeof PutInv, aff::MapsPage, GemPut, Nop));  // the inventory grid is not the stash
static_assert(MisplacedOnTab(SwapStash, sizeof SwapStash, aff::MapsPage, GemPut, Never) && !MisplacedOnTab(SwapStash, sizeof SwapStash, aff::MapsPage, GemPut, Nop));
static_assert(!MisplacedOnTab(SwapIn, sizeof SwapIn, aff::MapsPage, GemPut, Never));  // a swap on the inventory (page 0) is never filtered
static_assert(!MisplacedOnTab(SwapStash, 0x14, aff::MapsPage, GemPut, Never));  // too short
static_assert(!ChangesEquipment(nullptr, 9) && !ChangesEquipment(Equip, 0));

constexpr char Command[] = "core-ui/InventoryLock";
constexpr char Unlocked[] = "InventoryLock", Locked[] = "InventoryLockOn", LockedArt[] = "InventoryLockOnArt";
constexpr const char* Panels[] = {"PlayerInventoryExpansionLayout", "PlayerInventoryOriginalLayout"};
constexpr uint32_t WatchFrames = 10;

const D2RL::PluginContext* g_context = nullptr;
const D2RL::WidgetService* g_widgets = nullptr;
std::atomic<bool> g_locked {false};
std::atomic<uint32_t> g_localId {0xffffffffU};
std::atomic<uint32_t> g_refused {0}, g_toggles {0};
uint32_t g_frames = 0;
std::atomic<bool> g_named {false};  // this game's character name is read (server thread, first local message)
std::mutex g_nameLock;  // guards g_name and g_dirty: the UI thread toggles, the server thread loads
std::string g_name;  // this game's character, for its saved lock; empty until read, or if unreadable
bool g_dirty = false;  // toggled this game before the name was read: save then instead of loading

// D2R's PlayerData starts with the character name (16 bytes); the server's player unit holds it at UnitData
auto NameOf(void* player) noexcept -> std::string {
	const auto* data = d2r::At<const char*>(player, d2r::rva::UnitData);
	if (data == nullptr) {
		return {};
	}
	std::string name;
	for (size_t i = 0; i < 16 && data[i] != 0; ++i) {
		const char c = data[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
			return {};  // not a character name: keep the lock in memory only
		}
		name += c;
	}
	return name.size() >= 2 ? name : std::string {};
}

// <mod>/d2rloader/config/inventory-lock<suffix>.locked, beside our d2rloader/plugins folder: suffix -<name> is a
// character's lock, an empty one the last lock in use (what the padlock shows on entry, before the name is read)
auto SavePath(const std::string& suffix) noexcept -> std::string {
	HMODULE self = nullptr;
	char path[MAX_PATH];
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        reinterpret_cast<LPCSTR>(&SavePath), &self)) {
		return {};
	}
	const DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) {
		return {};
	}
	std::string file(path, n);
	for (int i = 0; i < 2; ++i) {  // drop core-ui.dll, then plugins
		const size_t slash = file.find_last_of("\\/");
		if (slash == std::string::npos) {
			return {};
		}
		file.resize(slash);
	}
	file += "\\config";
	CreateDirectoryA(file.c_str(), nullptr);
	return file + "\\inventory-lock" + suffix + ".locked";
}

void Mark(const std::string& file, bool locked) noexcept {
	if (file.empty()) {
		return;
	}
	if (!locked) {
		DeleteFileA(file.c_str());
		return;
	}
	const HANDLE h = CreateFileA(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
	if (h != INVALID_HANDLE_VALUE) {
		CloseHandle(h);
	}
}

auto Marked(const std::string& file) noexcept -> bool {
	return !file.empty() && GetFileAttributesA(file.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// under g_nameLock; an unreadable name keeps only the last lock
void Save(const std::string& name, bool locked) noexcept {
	if (!name.empty()) {
		Mark(SavePath("-" + name), locked);
	}
	Mark(SavePath(""), locked);
}

// server thread, first message from the local player in a game: take the character's saved lock
void Load(void* player) noexcept {
	const std::lock_guard guard(g_nameLock);
	if (g_named.load()) {
		return;
	}
	g_named.store(true);
	g_name = NameOf(player);
	if (!g_dirty && !g_name.empty()) {
		g_locked.store(Marked(SavePath("-" + g_name)));
	}
	Save(g_name, g_locked.load());
	g_dirty = false;
	plog::Info("inventory lock: %s is %s", g_name.empty() ? "(unnamed)" : g_name.c_str(), g_locked.load() ? "locked" : "unlocked");
}

sigscan::Entry* const Sigs[] = {&d2r::sig::FindUnit, &d2r::sig::ItemPage, &d2r::sig::UnitPackedXY, &d2r::sig::UnitClassOf,
                                &d2r::sig::ItemRecord};

// 93847 0x4f2fa0, int (game, player, message, size): the server's client-message dispatch (PlrMsg handler table by id)
using DispatchFn = int (*)(void*, void*, const uint8_t*, int) noexcept;
DispatchFn g_dispatch = nullptr;
constexpr uint8_t DispatchEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20};

// the top row of a stored inventory-grid item of the local player, -1 for anything else (a cursor, stash or cube item, none)
auto RowOf(void* game, uint32_t id) noexcept -> int {
	Unit* item = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(d2r::sig::FindUnit)(static_cast<Game*>(game), 4, id);
	if (item == nullptr || d2r::At<int32_t>(item, d2r::rva::UnitMode) != 0 ||
	    d2r::Engine<uint8_t (*)(Unit*) noexcept>(d2r::sig::ItemPage)(item) != 0) {
		return -1;
	}
	return static_cast<int>(d2r::Engine<uint32_t (*)(Unit*) noexcept>(d2r::sig::UnitPackedXY)(item) >> 16);
}

auto UnitOf(void* game, uint32_t id) noexcept -> Unit* {
	return d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(d2r::sig::FindUnit)(static_cast<Game*>(game), 4, id);
}

// the item's code (the items record's +0x80), 0 when it is not found
auto CodeOf(void* game, Unit* item) noexcept -> uint32_t {
	if (item == nullptr) {
		return 0;
	}
	const int32_t row = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(d2r::sig::UnitClassOf)(item, __FILE__, __LINE__);
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::sig::ItemRecord)(
		d2r::DataFlag(static_cast<Game*>(game)), row);
	return record == nullptr ? 0 : d2r::At<uint32_t>(record, 0x80);
}

// 0x18: whether the item may stand at cell (x, y) of the open tab's page
auto PutOk(void* game, uint32_t id, uint32_t page, int32_t x, int32_t y) noexcept -> bool {
	const uint32_t code = CodeOf(game, UnitOf(game, id));
	return code != 0 && Own(code, stash_affinity::WantedBase(), page, x, y);
}

// 0x1f: the cursor item (mode 4) goes to the message's cell (x, y)
auto SwapOk(void* game, uint32_t a, uint32_t b, int32_t x, int32_t y, uint32_t page) noexcept -> bool {
	Unit* first = UnitOf(game, a);
	Unit* second = UnitOf(game, b);
	if (first == nullptr || second == nullptr) {
		return true;
	}
	const bool firstHeld = d2r::At<int32_t>(first, d2r::rva::UnitMode) == 4;
	Unit* held = firstHeld ? first : second;
	Unit* stored = firstHeld ? second : first;
	if (d2r::At<int32_t>(held, d2r::rva::UnitMode) != 4 || d2r::At<int32_t>(stored, d2r::rva::UnitMode) != 0) {
		return true;
	}
	const uint32_t code = CodeOf(game, held);
	return code != 0 && Own(code, stash_affinity::WantedBase(), page, x, y);
}

auto OnMessage(void* game, void* player, const uint8_t* message, int size) noexcept -> int {
	const bool local = player != nullptr && d2r::At<uint32_t>(player, d2r::rva::UnitIdField) == g_localId.load();
	if (local && !g_named.load()) {
		Load(player);
	}
	if (local && g_locked.load(std::memory_order_relaxed) && size > 0 && ChangesEquipment(message, static_cast<size_t>(size))) {
		++g_refused;
		plog::Info("inventory lock: refused message 0x%02x (%d bytes)", message[0], size);
		return 0;
	}
	if (local && g_locked.load(std::memory_order_relaxed) && size > 0 &&
	    TouchesCharmRows(message, static_cast<size_t>(size), [game](uint32_t id) noexcept { return RowOf(game, id); })) {
		++g_refused;
		plog::Info("inventory lock: refused charm-row message 0x%02x (%d bytes)", message[0], size);
		return 0;
	}
	if (const uint32_t page = stash_affinity::ViewPage(); local && page != 0 && size > 0 &&
	    MisplacedOnTab(
	        message, static_cast<size_t>(size), page,
	        [=](uint32_t id, int32_t x, int32_t y) noexcept { return PutOk(game, id, page, x, y); },
	        [=](uint32_t a, uint32_t b, int32_t x, int32_t y) noexcept { return SwapOk(game, a, b, x, y, page); })) {
		++g_refused;
		plog::Info("stash tabs: refused message 0x%02x, not the item's own cell (page %u)", message[0], page);
		return 0;
	}
	return g_dispatch(game, player, message, size);
}

auto OnUiMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept
	-> D2RL::SharedEvents::UiMessageAction {
	if (event == nullptr || event->target == nullptr || event->command == nullptr || event->text == nullptr ||
	    std::strcmp(event->target, "PanelManager") != 0 || std::strcmp(event->command, "OpenPanel") != 0 ||
	    std::strcmp(event->text, Command) != 0) {
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}
	const bool locked = !g_locked.load();
	g_locked.store(locked);
	{
		const std::lock_guard guard(g_nameLock);
		if (g_named.load()) {
			Save(g_name, locked);
		} else {
			g_dirty = true;
		}
	}
	++g_toggles;
	plog::Info("inventory lock: %s (player %u)", locked ? "locked" : "unlocked", g_localId.load());
	return D2RL::SharedEvents::UiMessageAction::Consume;
}

// UI thread: only the button that matches the lock is shown
void OnFrame(const D2RL::PluginContext*, const D2RL::Overlay::Frame*, void*) noexcept {
	if (++g_frames % WatchFrames != 0) {
		return;
	}
	const bool locked = g_locked.load();
	for (const char* name : Panels) {
		D2RL::Widgets::WidgetHandle panel {}, off {}, on {};
		if (g_widgets->findPanel(g_context, name, &panel) != D2RL::Widgets::Result::Success) {
			continue;
		}
		// controller layout (#1675): one button keeps the pad focus; only the locked art over it is shown or hidden
		if (g_widgets->findWidget(g_context, panel, LockedArt, &on) == D2RL::Widgets::Result::Success) {
			g_widgets->setWidgetVisible(g_context, on, locked);
		} else if (g_widgets->findWidget(g_context, panel, Unlocked, &off) == D2RL::Widgets::Result::Success &&
		           g_widgets->findWidget(g_context, panel, Locked, &on) == D2RL::Widgets::Result::Success) {
			g_widgets->setWidgetVisible(g_context, off, !locked);
			g_widgets->setWidgetVisible(g_context, on, locked);
			// a hidden button still takes the hover and hides the other's tooltip: disable it too
			g_widgets->setWidgetEnabled(g_context, off, !locked);
			g_widgets->setWidgetEnabled(g_context, on, locked);
		}
	}
}

void OnPlayerReady(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
	if (event != nullptr) {
		g_localId.store(event->playerId);
	}
	const std::lock_guard guard(g_nameLock);
	g_named.store(false);  // the next local message reads this game's character
	g_name.clear();
	g_dirty = false;
}

template <class Service>
auto Query(const Service*& service, bool (*has)(const Service*, uint32_t), uint32_t required) noexcept -> bool {
	if (g_context->QueryService(&service) != D2RL::ServiceQueryResult::Success || !has(service, required)) {
		service = nullptr;
	}
	return service != nullptr;
}

}  // namespace

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	g_context = context;
	sigscan::Resolve(Sigs, std::size(Sigs), context->exeBase, nullptr, 0);
	g_locked.store(Marked(SavePath("")));  // the padlock shows the last lock until this game's character is read
	const D2RL::LifecycleService* lifecycle = nullptr;
	const D2RL::SharedEventService* events = nullptr;
	const D2RL::OverlayService* overlay = nullptr;
	const bool widgets = Query(g_widgets, D2RL::HasWidgetServiceField, D2RL::WidgetServiceRequiredSize);
	const bool life = Query(lifecycle, D2RL::HasLifecycleServiceField, D2RL::LifecycleServiceRequiredSize);
	const bool shared = Query(events, D2RL::HasSharedEventServiceField, D2RL::SharedEventServiceRequiredSize);
	const bool frames = Query(overlay, D2RL::HasOverlayServiceField, D2RL::OverlayServiceRequiredSize);
	if (!(widgets && life && shared && frames)) {
		std::snprintf(report, size, "FAILED (service missing: widget %d, lifecycle %d, event %d, overlay %d)", widgets, life,
		              shared, frames);
		return;
	}
	for (const sigscan::Entry* e : Sigs) {
		if (e->state != sigscan::State::Ok) {
			std::snprintf(report, size, "FAILED (signature %s %s)", e->name, sigscan::StateText(e->state));
			return;
		}
	}
	if (!sigscan::HookBySignature(context, "PlrMsgDispatch",
	                              "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 83 ec 20 "
	                              "41 0f b6 30 45 8b e1 4d 8b f8 48 8b fa 4c 8b e9 bb 01 00 00 00 40 80 fe 67",
	                              0, DispatchEntry, sizeof DispatchEntry, OnMessage, &g_dispatch)) {
		std::snprintf(report, size, "FAILED (the server message dispatch: signature or bytes differ)");
		return;
	}
	const D2RL::Lifecycle::GameplayEventListener ready {.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
	                                                    .kind = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady,
	                                                    .callback = OnPlayerReady};
	const D2RL::SharedEvents::UiMessageListener listener {.structSize = D2RL::SharedEvents::UiMessageListenerSize,
	                                                      .callback = OnUiMessage};
	const D2RL::Overlay::CallbackRegistration frame {.structSize = D2RL::Overlay::CallbackRegistrationSize,
	                                                 .phase = D2RL::Overlay::Phase::AfterGameUi,
	                                                 .callback = OnFrame};
	D2RL::Lifecycle::ListenerHandle readyHandle = D2RL::Lifecycle::InvalidHandle;
	D2RL::SharedEvents::ListenerHandle listenerHandle = D2RL::SharedEvents::InvalidHandle;
	D2RL::Overlay::CallbackHandle frameHandle = D2RL::Overlay::InvalidCallbackHandle;
	if (lifecycle->registerGameplayEventListener(context, &ready, &readyHandle) != D2RL::Lifecycle::Result::Success ||
	    events->registerUiMessageListener(context, &listener, &listenerHandle) != D2RL::SharedEvents::Result::Success ||
	    overlay->registerFrameCallback(context, &frame, &frameHandle) != D2RL::Overlay::Result::Success) {
		std::snprintf(report, size, "FAILED (a listener was refused; the hook passes everything while unlocked)");
		return;
	}
	std::snprintf(report, size, "the inventory padlock locks the player's and the mercenary's body slots and the charm rows 5-8");
}

auto IsLocked() noexcept -> bool {
	return g_locked.load();
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "inventory lock: %s, %u toggles, %u moves refused", g_locked.load() ? "locked" : "unlocked",
	              g_toggles.load(), g_refused.load());
}

}  // namespace inventory_lock
