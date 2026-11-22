// PD2's inventory hotkeys, configurable (#1567). Defaults, in the inventory, stash and Cube:
//   Ctrl + right click          drop the item (with any panel open)
//   Shift + right click         move it to the stash, or back to the inventory when it is in the stash or Cube
//   Ctrl + Shift + right click  move it to the Cube (Cube open or not)
//   Shift + left click          identify it with a Horadric Almanac, Tome of Identify or Scroll of Identify in the inventory; replaces D2R's Shift+Left equip
// d2rloader\config\core-ui-hotkeys.toml (written on first run) rebinds each action as "modifiers+button", e.g. drop = "ctrl+right".
//
// Left clicks come from the SDK's item interaction event. D2R sends no event for a right click, so the grid widget's right-click
// handler (93847 0x2c3310: widget, packed cell) is hooked by signature; it hands every other right click to D2R unchanged.
// The moves and identify are SDK item transactions on the game thread, which move an item whole or leave it where it was.
// Drop has no SDK route: D2R's own Ctrl + left click drops (or quick-moves with a panel open), so a drop bind calls the
// widget's left-click handler, and needs Ctrl as its only modifier. It blanks the panel-open table around that call so
// D2R sees the inventory alone. Every action plays its game sound (identify: sounds.txt 13; moves: the item's drop sound).
// The stash move sends Gems/Materials/Runes tab items to their tab through D2R's own Ctrl+click deposit (#1774), and maps,
// map currency and bossing items to their reserved shared page (stash affinity).
// shortcut: identify never spends a tome's last charge (SDK limit); other items go to the personal stash (no tab pick).
#include "hotkeys.h"

#include "inventory_lock.h"

#include "log.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"
#include "stash_affinity.h"
#include "stash_home.h"

#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item_interactions.h>
#include <D2RLPlugin/threads.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <windows.h>
#include <string>

namespace hotkeys {

namespace {

namespace items = D2RL::Items;
using D2RL::ItemInteractions::Modifier;
using D2RL::ItemInteractions::ModifierBit;

enum class Act { Drop, Stash, Cube, Identify, Count };
struct Bind {
	uint32_t mods = 0;
	bool right = true;
	bool on = false;
};
constexpr const char* ActName[] = {"drop", "stash", "cube", "identify"};
Bind g_binds[static_cast<int>(Act::Count)];

const D2RL::PluginContext* g_context = nullptr;
const D2RL::ItemService* g_items = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;

constexpr char DefaultFile[] =
    "# PD2 inventory hotkeys (core-ui). Each action is \"modifiers+button\": modifiers shift, ctrl, alt; button left or right.\n"
    "# \"off\" turns an action off. Restart the game after a change.\n"
    "# drop uses D2R's own Ctrl + left click drop, so it must be exactly \"ctrl+right\" (or off).\n"
    "drop = \"ctrl+right\"\n"
    "stash = \"shift+right\"\n"
    "cube = \"ctrl+shift+right\"\n"
    "identify = \"shift+left\"\n";

auto Parse(const std::string& value, Bind* bind) noexcept -> bool {
	Bind b;
	size_t at = 0;
	bool button = false;
	while (at <= value.size()) {
		size_t end = value.find('+', at);
		if (end == std::string::npos) end = value.size();
		const std::string word = value.substr(at, end - at);
		if (word == "shift") b.mods |= ModifierBit(Modifier::Shift);
		else if (word == "ctrl" || word == "control") b.mods |= ModifierBit(Modifier::Control);
		else if (word == "alt") b.mods |= ModifierBit(Modifier::Alt);
		else if (word == "left" || word == "right") { b.right = word == "right"; button = true; }
		else return false;
		at = end + 1;
	}
	if (!button || b.mods == 0) return false;  // a bare click would take over every click
	b.on = true;
	*bind = b;
	return true;
}

void LoadConfig(const wchar_t* configPath) noexcept {
	const char* defaults[] = {"ctrl+right", "shift+right", "ctrl+shift+right", "shift+left"};
	for (int i = 0; i < static_cast<int>(Act::Count); ++i) Parse(defaults[i], &g_binds[i]);
	if (configPath != nullptr) {
		std::wstring path = configPath;
		const size_t slash = path.find_last_of(L"\\/");
		path = (slash == std::wstring::npos ? L"" : path.substr(0, slash + 1)) + L"core-ui-hotkeys.toml";
		FILE* file = _wfopen(path.c_str(), L"rb");
		if (file == nullptr) {
			if ((file = _wfopen(path.c_str(), L"wb")) != nullptr) {
				std::fwrite(DefaultFile, 1, sizeof DefaultFile - 1, file);
				std::fclose(file);
			}
		} else {
			char line[256];
			while (std::fgets(line, sizeof line, file) != nullptr) {
				std::string text;
				for (const char* c = line; *c != '\0' && *c != '#'; ++c) {
					if (!std::isspace(static_cast<unsigned char>(*c)) && *c != '"') text += static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
				}
				const size_t eq = text.find('=');
				if (eq == std::string::npos) continue;
				const std::string key = text.substr(0, eq);
				const std::string value = text.substr(eq + 1);
				for (int i = 0; i < static_cast<int>(Act::Count); ++i) {
					if (key != ActName[i]) continue;
					if (value == "off" || value.empty()) {
						g_binds[i].on = false;
					} else if (!Parse(value, &g_binds[i])) {
						plog::Warn("hotkeys: %s = \"%s\" is not modifiers+button; the default stays", key.c_str(), value.c_str());
					}
				}
			}
			std::fclose(file);
		}
	}
	for (int i = 0; i < static_cast<int>(Act::Count); ++i) {
		for (int j = i + 1; j < static_cast<int>(Act::Count); ++j) {
			if (g_binds[i].on && g_binds[j].on && g_binds[i].mods == g_binds[j].mods && g_binds[i].right == g_binds[j].right) {
				plog::Warn("hotkeys: %s and %s share one bind; the first wins", ActName[i], ActName[j]);
			}
		}
	}
	Bind& drop = g_binds[static_cast<int>(Act::Drop)];
	if (drop.on && (!drop.right || drop.mods != ModifierBit(Modifier::Control))) {
		plog::Warn("hotkeys: drop must be exactly ctrl+right; it is off");
		drop.on = false;
	}
	for (int i = 0; i < static_cast<int>(Act::Count); ++i) {
		plog::Info("hotkeys: %s %s mods %u %s", ActName[i], g_binds[i].on ? "on" : "off", g_binds[i].mods, g_binds[i].right ? "right" : "left");
	}
}

auto Match(uint32_t mods, bool right) noexcept -> int {
	for (int i = 0; i < static_cast<int>(Act::Count); ++i) {
		if (g_binds[i].on && g_binds[i].mods == mods && g_binds[i].right == right) return i;
	}
	return -1;
}

// ---- the game-thread job ----
struct Job {
	Act act;
	D2RL::PlayerHandle player;
	uint32_t code;
	uint32_t runtimeId;
	items::ItemContainer container;
	int sound;  // played after the action succeeds
};
Job g_job {};
std::atomic<bool> g_busy {false};

struct Find {
	uint32_t code;       // 0: any code
	uint32_t runtimeId;  // 0: any item of the code with quantity >= minQuantity
	int32_t minQuantity = 2;
	D2RL::ItemHandle found = D2RL::InvalidItemHandle;
	items::ItemInfo info {};
};

auto FindItem(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* find = static_cast<Find*>(user);
	if ((find->code == 0 || item->code == find->code) && (find->runtimeId != 0 ? item->runtimeId == find->runtimeId : item->quantity >= find->minQuantity)) {
		find->found = item->handle;
		find->info = *item;
		return D2RL::Inventory::IterationAction::Stop;
	}
	return D2RL::Inventory::IterationAction::Continue;
}

auto Look(D2RL::PlayerHandle player, uint32_t mask, Find* find) noexcept -> bool {
	const D2RL::Inventory::ItemFilter filter {.structSize = D2RL::Inventory::ItemFilterSize, .containerMask = mask};
	g_inventory->forEachInventoryItem(g_context, player, &filter, FindItem, find);
	return find->found != D2RL::InvalidItemHandle;
}

// D2R's client sound player (93847 0x1a0780: sound id, unit or null, 0, 0, 0): null unit is a UI sound, as the grid's own item
// placement (0x2c7290) plays it. 0x1c3420 (client item unit) -> the item's drop sound id. The SDK has no sound service.
using PlayFn = void (*)(int, void*, int, int, char) noexcept;
PlayFn g_play = nullptr;
using ItemSoundFn = int (*)(void*) noexcept;
ItemSoundFn g_itemSound = nullptr;
constexpr int SoundIdentify = 13;  // sounds.txt cursor_identify_item
void PlayUi(int id) noexcept {
	if (g_play != nullptr && id > 0) {
		g_play(id, nullptr, 0, 0, 0);
		plog::Info("hotkeys: sound %d", id);
	}
}
// A map whose base the Maps tab shows (shown: its code, 0 none) is sent onto the Maps page, the rest to their home page
constexpr auto ToMapsPage(pd2::affinity::Category category, uint32_t code, uint32_t shown) noexcept -> bool {
	return category == pd2::affinity::Category::Maps && shown != 0 && pd2::affinity::Normal(code) == pd2::affinity::Normal(shown);
}
static_assert(ToMapsPage(pd2::affinity::HomeOf(pd2::affinity::Code("m01")).category, pd2::affinity::Code("m01"), pd2::affinity::Code("m01")));
static_assert(!ToMapsPage(pd2::affinity::HomeOf(pd2::affinity::Code("m01")).category, pd2::affinity::Code("m01"), pd2::affinity::Code("m04")));
static_assert(!ToMapsPage(pd2::affinity::HomeOf(pd2::affinity::Code("m01")).category, pd2::affinity::Code("m01"), 0));
static_assert(!ToMapsPage(pd2::affinity::HomeOf(pd2::affinity::Code("imma")).category, pd2::affinity::Code("imma"), pd2::affinity::Code("m01")));

void Run(const Job& job) noexcept {
	Find find {job.code, job.runtimeId};
	if (!Look(job.player, items::ContainerBit(job.container), &find)) {
		plog::Info("hotkeys: %s failed: item not found", ActName[static_cast<int>(job.act)]);
		return;
	}
	items::ExistingItemOperation ops[2] {};
	uint32_t count = 1;
	bool wasHomed = false;
	D2RL::ItemHandle spentScroll = D2RL::InvalidItemHandle;
	if (job.act == Act::Identify) {
		// What identifies is in books.txt / misc.txt: Tome of Identify (ibk, pSpell 1, spends a charge), Scroll of Identify (isc,
		// pSpell 1, a stack) and the Horadric Almanac (rid, unlimited). Best source first: the Almanac costs nothing.
		struct Source { uint32_t code; bool spends; int32_t minQuantity; };
		const Source sources[] = {{items::MakeItemCode('r', 'i', 'd', ' '), false, 1}, {items::MakeItemCode('i', 'b', 'k', ' '), true, 2},
		                          {items::MakeItemCode('i', 's', 'c', ' '), true, 1}};
		Find source {};
		bool spends = false;
		for (const Source& candidate : sources) {
			// shortcut: a tome's last charge is not spendable here: the SDK debit and editItem both need a positive quantity and a destroy
			// would delete the tome (D2R keeps it at 0), so a tome with one charge is skipped; upgrade when the SDK can set a stack to 0.
			// A lone scroll is destroyed after the identify.
			source = Find {candidate.code, 0, candidate.minQuantity};
			if (Look(job.player, items::ContainerBit(items::ItemContainer::Inventory), &source)) { spends = candidate.spends; break; }
		}
		if (source.found == D2RL::InvalidItemHandle) {
			plog::Info("hotkeys: identify failed: no Almanac, Tome of Identify with a spare charge or Scroll of Identify in the inventory");
			return;
		}
		ops[0].structSize = items::ExistingItemOperationSize;
		ops[0].kind = items::ExistingItemOperationKind::Edit;
		ops[0].item = find.found;
		ops[0].edit = {.fields = items::EditFieldBit(items::EditField::Identified), .stateFlags = items::ItemStateIdentified};
		if (spends && source.info.quantity >= 2) {
			ops[1].structSize = items::ExistingItemOperationSize;
			ops[1].kind = items::ExistingItemOperationKind::Debit;
			ops[1].item = source.found;
			ops[1].debit.quantity = 1;
			count = 2;
		} else if (spends) {
			spentScroll = source.found;
		}
	} else {
		if (job.act == Act::Cube && find.info.code == items::MakeItemCode('b', 'o', 'x', ' ')) return;  // the Horadric Cube never goes into itself
		const items::ItemContainer from = job.container;
		// the padlock (#1727) also keeps charm-row items put: this move never reaches the hooked message dispatch
		if (from == items::ItemContainer::Inventory && find.info.y >= d2r::CharmFirstRow && inventory_lock::IsLocked()) {
			plog::Info("hotkeys: %s refused: inventory lock, charm row %d", ActName[static_cast<int>(job.act)], find.info.y + 1);
			return;
		}
		const items::ItemContainer to = job.act == Act::Cube ? (from == items::ItemContainer::Cube ? items::ItemContainer::Inventory : items::ItemContainer::Cube)
		                                                     : (from == items::ItemContainer::Inventory ? items::ItemContainer::PersonalStash : items::ItemContainer::Inventory);
		ops[0].structSize = items::ExistingItemOperationSize;
		ops[0].kind = items::ExistingItemOperationKind::Move;
		ops[0].item = find.found;
		ops[0].move.destination = {.structSize = items::ItemDestinationSize, .container = to, .placement = items::Placement::Automatic};
		// stash affinity (#1724): a map, map currency or bossing item goes to its home tab, as a quick-move does; Automatic on that page
		// puts it down anywhere there and the affinity watch then sets its cell
		const auto home = pd2::affinity::HomeOf(find.info.code);
		if (job.act == Act::Stash && from == items::ItemContainer::Inventory && home.category != pd2::affinity::Category::None) {
			ops[0].move.destination.container = items::ItemContainer::SharedStash;
			// #1773: a map of the base the Maps tab shows goes to the Maps page, as a quick-move from that tab does; on its storage
			// row it was home already, so the watch left it off the Maps view until a tier switch swapped it in
			ops[0].move.destination.sharedStashPage = ToMapsPage(home.category, find.info.code, stash_affinity::WantedBase()) ? pd2::affinity::MapsPage : home.page;
			wasHomed = true;
		}
	}
	const items::ExistingItemTransaction transaction {
		.structSize = items::ExistingItemTransactionSize, .player = job.player, .operationCount = count, .operations = ops};
	items::ExistingItemTransactionResult result {.structSize = items::ExistingItemTransactionResultSize};
	items::Result status = g_items->executeExistingItemTransaction(g_context, &transaction, &result);
	if (wasHomed && status != items::Result::Success) {  // its page is full or missing (add_shared_tabs): the personal stash; the watch then fails to route it there and sends it back to the inventory, as a quick-move ends
		ops[0].move.destination.container = items::ItemContainer::PersonalStash;
		ops[0].move.destination.sharedStashPage = 0;
		status = g_items->executeExistingItemTransaction(g_context, &transaction, &result);
	}
	if (status == items::Result::Success && spentScroll != D2RL::InvalidItemHandle &&
	    g_items->destroyItem(g_context, job.player, spentScroll, items::SocketedItemPolicy::RejectIfNotEmpty) != items::Result::Success) {
		plog::Warn("hotkeys: identify worked but the scroll was not spent");
	}
	plog::Info("hotkeys: %s %s (%u)", ActName[static_cast<int>(job.act)], status == items::Result::Success ? "done" : "failed",
	           static_cast<uint32_t>(status));
	// D2R's sound player belongs on the UI thread, not this authoritative game update
	if (status == items::Result::Success && job.sound > 0) {
		g_threads->runOnUiThread(g_context, [](const D2RL::PluginContext*, void* sound) noexcept { PlayUi(static_cast<int>(reinterpret_cast<intptr_t>(sound))); },
		                         reinterpret_cast<void*>(static_cast<intptr_t>(job.sound)));
	}
}

void GameThreadJob(const D2RL::PluginContext*, void*) noexcept {
	const Job job = g_job;
	g_busy = false;
	Run(job);
}

constexpr uint32_t Ours = items::ContainerBit(items::ItemContainer::Inventory) | items::ContainerBit(items::ItemContainer::Cube) |
                          items::ContainerBit(items::ItemContainer::PersonalStash) | items::ContainerBit(items::ItemContainer::SharedStash) |
                          items::ContainerBit(items::ItemContainer::CustomPage);

// UI thread. True when the action was queued or deliberately swallowed (the caller then drops D2R's own click).
auto Queue(Act act, D2RL::PlayerHandle player, const items::ItemInfo& info, int sound) noexcept -> bool {
	// Equipment, belt and the like are not ours: let D2R act
	if ((items::ContainerBit(info.container) & Ours) == 0) return false;
	if (act == Act::Identify && (info.stateFlags & items::ItemStateIdentified) != 0) return false;  // nothing to identify: D2R's own Shift + left click equips
	if (g_busy.exchange(true)) return true;
	g_job = {act, player, info.code, info.runtimeId, info.container, sound};
	if (g_threads->runOnGameThread(g_context, GameThreadJob, nullptr) != D2RL::Threads::Result::Success) {
		g_busy = false;
		return false;
	}
	return true;
}

// ---- left click ----
auto __cdecl OnActivate(const D2RL::PluginContext*, const D2RL::ItemInteractions::ItemInteractionEvent* event, void*) noexcept
	-> D2RL::ItemInteractions::Decision {
	using D2RL::ItemInteractions::Decision;
	if (event->inputSource != D2RL::ItemInteractions::InputSource::KeyboardMouse) return Decision::Continue;
	const int act = Match(event->modifiers, false);
	if (act < 0 || static_cast<Act>(act) == Act::Drop) return Decision::Continue;
	items::ItemInfo info {.structSize = items::ItemInfoSize};
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (g_items->getItemInfo(g_context, event->item, &info) != items::Result::Success ||
	    g_inventory->getLocalPlayer(g_context, &player) != D2RL::Inventory::Result::Success) {
		return Decision::Continue;
	}
	if (!Queue(static_cast<Act>(act), player, info, SoundIdentify)) return Decision::Continue;
	return Decision::Consume;
}

// ---- right click ----
using KeyFn = int (*)(int) noexcept;  // D2R's "is this virtual key down" wrapper (93847 0x120a100)
KeyFn g_key = nullptr;
using RightFn = void (*)(void*, uint64_t) noexcept;
RightFn g_right = nullptr;
using PanelFn = uint8_t (*)(int) noexcept;  // D2R's "is panel N open" (93847 0xce500)
PanelFn g_panel = nullptr;
// panel ids, read in game: inventory 1, stash 24 (0x18), Cube 25 (0x19); D2R's own Drop label needs 0x16, 0xd, 0xb shut too (vendor panels)
constexpr int PanelCount = 32;  // D2R's own loop over the table is < 0x20; the dwords after it belong to other code
constexpr int PanelInventory = 1, PanelStash = 0x18;
constexpr int AlwaysOpen[] = {0, 20, 28, 29};  // open with only the inventory up (read in game; 28 only in town): HUD panels, no NPC, stash or Cube panel
auto PanelOpen(int id) noexcept -> bool { return g_panel != nullptr && g_panel(id) != 0; }
uint8_t* g_panelTable = nullptr;  // the byte-per-panel "is open" table behind g_panel
using AtFn = void* (*)(void*, uint64_t*) noexcept;
constexpr size_t SlotItemAt = 0xc8, SlotLeftClick = 0x100;  // the grid widget's vtable (0x1cf3800): item at cell, left click
constexpr uint8_t RightEntry[] = {0x48, 0x89, 0x54, 0x24, 0x10, 0x53, 0x48, 0x83, 0xec, 0x20};
// #1774: an inventory item of the RotW stash tabs (Gems, Materials, Runes: misc.txt AdvancedStashStackable) goes to its tab
// cell, as Ctrl+click puts it. No SDK service reaches those tabs (stash_stacks.cpp), so D2R's own Ctrl+click branch runs: the
// grid's left click (93847 0x2c7540, stash up, Shift up) asks 0x15a0b0 (client item: stash panel open, RotW on, the item's
// class in the tabs' table) and then calls 0x2aaaa0 (grid widget, &cell), which sends the tab deposit (0x63, through
// 0x15f8b0) and plays the sounds. The server's deposit (0x4f92f0) is the one stash_stacks.cpp counts whole stacks in.
using ToTabsFn = uint8_t (*)(void*) noexcept;
ToTabsFn g_toTabs = nullptr;
using DepositFn = void (*)(void*, uint64_t*) noexcept;
DepositFn g_deposit = nullptr;

// cubeOnly: the click came from OnMessage, so D2R has no click of its own to fall back to, and only a Cube item is ours
void Click(void* widget, uint64_t cell, bool cubeOnly) noexcept {
	const auto native = [&] { if (!cubeOnly) g_right(widget, cell); };
	uint32_t mods = 0;
	if (g_key(0x10) != 0) mods |= ModifierBit(Modifier::Shift);
	if (g_key(0x11) != 0) mods |= ModifierBit(Modifier::Control);
	if (g_key(0x12) != 0) mods |= ModifierBit(Modifier::Alt);
	const int act = mods == 0 ? -1 : Match(mods, true);
	if (act >= 0) {
		void** vtable = *static_cast<void***>(widget);
		uint64_t at = cell;
		void* unit = reinterpret_cast<AtFn>(vtable[SlotItemAt / sizeof(void*)])(widget, &at);
		D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
		D2RL::ItemHandle cursor = D2RL::InvalidItemHandle;
		// Only with an empty cursor and an item under the mouse; any doubt is D2R's own click
		if (unit != nullptr && g_inventory->getLocalPlayer(g_context, &player) == D2RL::Inventory::Result::Success &&
		    g_inventory->getCursorItem(g_context, player, &cursor) == D2RL::Inventory::Result::NotFound) {
			// the SDK's handle is not the client unit: find the item by the unit id the two share
			Find find {0, d2r::At<uint32_t>(unit, d2r::rva::UnitIdField)};
			const Act what = static_cast<Act>(act);
			if (what == Act::Drop) {
				// the player's own inventory or Cube, whatever panel is open. D2R's Ctrl + left click only drops with no other panel up (with one
				// up it quick-moves), so the panel table reads shut for that one call; the item's own drop sound plays inside it
				if (find.runtimeId != 0 && Look(player, items::ContainerBit(items::ItemContainer::Inventory) | items::ContainerBit(items::ItemContainer::Cube), &find) &&
				    (!cubeOnly || find.info.container == items::ItemContainer::Cube)) {
					uint8_t saved[PanelCount] = {};
					for (int id = 0; id < PanelCount && g_panelTable != nullptr; ++id) {
						bool keep = id == PanelInventory;
						for (const int open : AlwaysOpen) keep = keep || id == open;
						saved[id] = g_panelTable[id];
						if (!keep) g_panelTable[id] = 0;
					}
					reinterpret_cast<RightFn>(vtable[SlotLeftClick / sizeof(void*)])(widget, cell);  // Ctrl is down: D2R's Ctrl + left click
					// the click handler runs synchronously on this thread and does not touch the table, so writing back what we read is safe
					for (int id = 0; id < PanelCount && g_panelTable != nullptr; ++id) g_panelTable[id] = saved[id];
					plog::Info("hotkeys: drop sent");
				}
				return;
			}
			// a stash move needs the stash panel open (no remote stash); a Cube move works with the Cube shut and any panel up
			if (what == Act::Stash && !PanelOpen(PanelStash)) return;
			if (find.runtimeId == 0 || !Look(player, Ours, &find)) {
				native();
				return;
			}
			if (cubeOnly && find.info.container != items::ItemContainer::Cube) return;
			if (what == Act::Stash && find.info.container == items::ItemContainer::Inventory && g_deposit != nullptr &&
			    g_toTabs != nullptr && g_toTabs(unit) != 0) {
				if (find.info.y >= d2r::CharmFirstRow && inventory_lock::IsLocked()) {
					plog::Info("hotkeys: stash refused: inventory lock, charm row %d", find.info.y + 1);
					return;
				}
				uint64_t spot = cell;
				g_deposit(widget, &spot);
				plog::Info("hotkeys: stash %08x to its Gems/Materials/Runes tab (D2R's deposit)", find.info.code);
				return;
			}
			if (Queue(what, player, find.info, what == Act::Identify ? SoundIdentify : g_itemSound != nullptr ? g_itemSound(unit) : 0)) {
				return;
			}
		}
	}
	native();
}

void OnRightClick(void* widget, uint64_t cell) noexcept { Click(widget, cell, false); }

void OnCubeClick(void* widget, uint64_t cell) noexcept { Click(widget, cell, true); }

// #1821: D2R never sends the right-click message to the Cube's grid (the other grids and the root widget get it, the grid under
// the mouse does not), so OnRightClick never runs for a Cube item. The message handler every widget shares sees the message
// instead: when the mouse is over a grid that holds a Cube item, the bind runs for that grid and cell.
using MsgFn = void (*)(void*, int64_t*) noexcept;
using ManagerFn = uint8_t* (*)() noexcept;
MsgFn g_message = nullptr;
ManagerFn g_manager = nullptr;
void* g_rightTarget = nullptr;  // InventoryRightClick's address: the grids are the widgets whose vtable slot SlotRightClick holds it
constexpr int64_t RightMessage = 0x2d5172cac7c938c;  // the message's name hash (message field +0x88)
constexpr size_t MessageName = 0x11, MessageData = 0x22, SlotCellAt = 0x98, SlotRightClick = 0x108;
size_t g_hoveredOffset = 0;  // read from the hover-check function at install, never assumed
using GateFn = uint8_t (*)() noexcept;
GateFn g_gate = nullptr;  // D2R's own "input is blocked" check: non-zero means D2R ignores the click
// The handler compares message[0] with an id it looks up by name hash (the movabs/lea/call at its +0x47/+0x55/+0x74); we repeat that lookup.
using IdFn = int64_t (*)(const void*, int64_t) noexcept;
IdFn g_idLookup = nullptr;
const void* g_idTable = nullptr;
int64_t g_idHash = 0;
// Looked up on the first message, not at install: an install-time lookup returned an id that never matched (the id table is not ready yet).
std::atomic<int64_t> g_inputMessage {0};
constexpr uint8_t MessageEntry[] = {0x40, 0x55, 0x53, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0xac, 0x24, 0xc0, 0xfd, 0xff, 0xff};
uint64_t g_lastCubeClick = 0;

void OnMessage(void* widget, int64_t* message) noexcept {
	int64_t inputMessage = g_inputMessage.load(std::memory_order_relaxed);
	if (inputMessage == 0 && g_idLookup != nullptr) g_inputMessage.store(inputMessage = g_idLookup(g_idTable, g_idHash), std::memory_order_relaxed);
	if (message != nullptr && inputMessage != 0 && message[0] == inputMessage && message[MessageName] == RightMessage && g_gate != nullptr && g_gate() == 0 && g_manager != nullptr && g_rightTarget != nullptr && g_key != nullptr) {
		void* hovered = *reinterpret_cast<void**>(g_manager() + g_hoveredOffset);
		const uint64_t now = GetTickCount64();
		// the message reaches many widgets: once per click, and never when the grid under the mouse gets it itself (its own hook runs)
		if (hovered != nullptr && hovered != widget && (*static_cast<void***>(hovered))[SlotRightClick / sizeof(void*)] == g_rightTarget && now - g_lastCubeClick > 250) {
			g_lastCubeClick = now;
			void** vtable = *static_cast<void***>(hovered);
			uint64_t scratch[16] = {};
			const auto at = reinterpret_cast<uint64_t* (*)(void*, void*, int64_t) noexcept>(vtable[SlotCellAt / sizeof(void*)]);
			const int64_t where = *reinterpret_cast<int64_t*>(reinterpret_cast<uint8_t*>(message[MessageData]) + 4);
			const uint64_t* cell = at(hovered, scratch, where);
			if (cell != nullptr) OnCubeClick(hovered, *cell);
		}
	}
	g_message(widget, message);
}

// The hook reads D2R structs by offset, so every byte it relies on is checked first; any mismatch leaves the hook off.
bool BytesAre(uint64_t address, size_t at, std::initializer_list<uint8_t> bytes) noexcept {
	const uint8_t* p = reinterpret_cast<const uint8_t*>(address) + at;
	size_t i = 0;
	for (const uint8_t b : bytes) if (p[i++] != b) return false;
	return true;
}

bool InstallCubeMessage(const D2RL::PluginContext* context) noexcept {
	constexpr const char* EntrySig = "40 55 53 57 41 56 41 57 48 8d ac 24 c0 fd ff ff 48 81 ec 40 03 00 00 48 8b 05 ?? ?? ?? ?? 48 33 c4";
	uint64_t manager = 0, gate = 0, entry = 0;
	if (sigscan::ScanUnique(context->exeBase, "48 83 ec 28 48 8b 05 ?? ?? ?? ?? 48 85 c0 75 1a 48 8d 4c 24 30 88 44 24 30 e8 ?? ?? ?? ?? 84 c0 74 01 cc 48 8b 05 ?? ?? ?? ?? 48 8b 80 d0 00 00 00 48 83 c4 28 c3", 0, &manager) != 1 ||
	    sigscan::ScanUnique(context->exeBase, "48 8b 05 ?? ?? ?? ?? 48 85 c0 74 08 0f b6 80 b8 00 00 00 c3 c3", 0, &gate) != 1 ||
	    sigscan::ScanUnique(context->exeBase, EntrySig, 0, &entry) != 1) return false;
	const uint64_t entryFn = context->exeBase + entry;
	// The hover check is the function the handler calls at +0x2b3 (several same-shaped checks exist, so it is not found by a scan of its own):
	// it calls the manager getter (e8 at +9) and compares a manager field (disp32 at +0x11) with the widget.
	if (!BytesAre(entryFn, 0x2b0, {0x48, 0x8b, 0xcf, 0xe8})) return false;
	const uint64_t hoverFn = entryFn + 0x2b8 + *reinterpret_cast<const int32_t*>(entryFn + 0x2b4);
	if (!BytesAre(hoverFn, 0, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xe8}) || !BytesAre(hoverFn, 0x0e, {0x48, 0x39, 0x98}) ||
	    !BytesAre(hoverFn, 0x15, {0x0f, 0x94, 0xc0}) || hoverFn + 14 + *reinterpret_cast<const int32_t*>(hoverFn + 10) != context->exeBase + manager) return false;
	// the handler's reads of the message and vtable slots, and the id lookup this hook repeats
	if (!BytesAre(entryFn, 0x124, {0x49, 0x8b, 0x87, 0x88, 0x00, 0x00, 0x00}) || !BytesAre(entryFn, 0x135, {0x49, 0x8b, 0xb7, 0x10, 0x01, 0x00, 0x00}) ||
	    !BytesAre(entryFn, 0x312, {0xff, 0x93, 0x98, 0x00, 0x00, 0x00}) || !BytesAre(entryFn, 0x47, {0x48, 0xba}) ||
	    !BytesAre(entryFn, 0x55, {0x48, 0x8d, 0x0d}) || !BytesAre(entryFn, 0x74, {0xe8})) return false;
	// every value OnMessage reads is set before the hook can run it
	g_idLookup = reinterpret_cast<IdFn>(entryFn + 0x79 + *reinterpret_cast<const int32_t*>(entryFn + 0x75));
	g_idTable = reinterpret_cast<const void*>(entryFn + 0x5c + *reinterpret_cast<const int32_t*>(entryFn + 0x58));
	g_idHash = *reinterpret_cast<const int64_t*>(entryFn + 0x49);
	g_hoveredOffset = *reinterpret_cast<const uint32_t*>(hoverFn + 0x11);
	g_manager = reinterpret_cast<ManagerFn>(context->exeBase + manager);
	g_gate = reinterpret_cast<GateFn>(context->exeBase + gate);
	if (!sigscan::HookBySignature(context, "InputMessage", EntrySig, 0, MessageEntry, sizeof MessageEntry, OnMessage, &g_message)) {
		g_gate = nullptr;
		g_idLookup = nullptr;
		g_manager = nullptr;
		return false;
	}
	return true;
}

}  // namespace

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	g_context = context;
	const D2RL::ItemInteractionService* interactions = nullptr;
	if (context->QueryService(&interactions) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasItemInteractionServiceField(interactions, D2RL::ItemInteractionServiceRequiredSize) ||
	    context->QueryService(&g_items) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasItemServiceField(g_items, D2RL::ItemServiceSplitStackFieldEnd) ||
	    context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize) ||
	    context->QueryService(&g_inventory) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasInventoryServiceField(g_inventory, D2RL::InventoryServiceRequiredSize)) {
		std::snprintf(report, size, "FAILED (an item, item interaction, inventory or thread service is missing)");
		return;
	}
	LoadConfig(context->pluginConfigPath);
	uint32_t mask = D2RL::ItemInteractions::DefaultContainerMask;
	if (D2RL::HasItemInteractionServiceField(interactions, D2RL::ItemInteractionServiceSupportedContainerMaskFieldEnd)) {
		mask |= interactions->supportedContainerMask & items::ContainerBit(items::ItemContainer::SharedStash);
	}
	const D2RL::ItemInteractions::ItemInteractionListener listener {
		.structSize = D2RL::ItemInteractions::ItemInteractionListenerSize, .priority = 50, .callback = OnActivate, .containerMask = mask};
	D2RL::ItemInteractions::ListenerHandle handle = D2RL::ItemInteractions::InvalidHandle;
	if (interactions->registerListener(context, &listener, &handle) != D2RL::ItemInteractions::Result::Success) {
		std::snprintf(report, size, "FAILED (no item interaction listener)");
		return;
	}
	uint64_t key = 0, rightRva = 0;
	constexpr const char* RightSignature = "48 89 54 24 10 53 48 83 ec 20 48 8b d9 e8 ?? ?? ?? ?? 84 c0 74 5c 48 8b 03 48 8d 54 24 38 48 8b cb";
	if (sigscan::ScanUnique(context->exeBase, RightSignature, 0, &rightRva) == 1) g_rightTarget = reinterpret_cast<void*>(context->exeBase + rightRva);
	const bool right = sigscan::ScanUnique(context->exeBase, "48 83 ec 28 ff 15 ?? ?? ?? ?? c1 e8 0f", 0, &key) == 1 &&
	                   sigscan::HookBySignature(context, "InventoryRightClick", RightSignature, 0,
	                                            RightEntry, sizeof RightEntry, OnRightClick, &g_right);
	if (!InstallCubeMessage(context)) plog::Warn("hotkeys: the Cube grid binds are off (message handler signature not found or changed)");
	uint64_t panel = 0;
	if (sigscan::ScanUnique(context->exeBase, "48 63 c1 48 8d 0d ?? ?? ?? ?? 0f b6 04 08 c3 cc 48 8b c4 48 89 50 10 4c 89 40 18", 0, &panel) == 1) g_panel = reinterpret_cast<PanelFn>(context->exeBase + panel);
	uint64_t at = 0;
	if (sigscan::ScanUnique(context->exeBase, "40 55 56 57 41 56 41 57 48 8d 6c 24 d1 48 81 ec 00 01 00 00 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 45 0f 83 3d", 0, &at) == 1) g_play = reinterpret_cast<PlayFn>(context->exeBase + at);
	if (sigscan::ScanUnique(context->exeBase, "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48 8b f9 33 db e8 ?? ?? ?? ?? 41 b8 9b 02 00 00", 0, &at) == 1) g_itemSound = reinterpret_cast<ItemSoundFn>(context->exeBase + at);
	if (g_panel != nullptr) {  // lea rcx, [rip + disp32] at +3 of the panel function: the table it indexes
		const uint8_t* lea = reinterpret_cast<const uint8_t*>(g_panel) + 3;
		g_panelTable = const_cast<uint8_t*>(lea + 7 + *reinterpret_cast<const int32_t*>(lea + 3));
	}
	if (sigscan::ScanUnique(context->exeBase, "40 53 48 83 ec 20 48 8b d9 b9 18 00 00 00 e8 ?? ?? ?? ?? 84 c0 74 ?? e8 ?? ?? ?? ?? 0f b6 c8 ba 02 00 00 00 e8 ?? ?? ?? ?? 84 c0 74 ?? 41 b8 f8 04 00 00", 0, &at) == 1) g_toTabs = reinterpret_cast<ToTabsFn>(context->exeBase + at);
	if (sigscan::ScanUnique(context->exeBase, "40 53 48 83 ec 40 48 8b 01 ff 90 c8 00 00 00 48 8b d8 48 85 c0 74 ?? 48 8b c8 e8 ?? ?? ?? ?? 85 c0 75 ?? 48 89 7c 24 50 e8 ?? ?? ?? ?? 8b c8 e8 ?? ?? ?? ?? 48 8b c8 e8", 0, &at) == 1) g_deposit = reinterpret_cast<DepositFn>(context->exeBase + at);
	if (g_toTabs == nullptr || g_deposit == nullptr) plog::Warn("hotkeys: the stash bind sends Gems/Materials/Runes items to the personal stash (tab deposit signature not found)");
	if (g_panelTable == nullptr) plog::Warn("hotkeys: the panel table was not found; Ctrl + right click drops only with no other panel open");
	if (g_play == nullptr || g_itemSound == nullptr) plog::Warn("hotkeys: item sounds are off (signature not found)");
	if (right) {
		g_key = reinterpret_cast<KeyFn>(context->exeBase + key);
	} else {
		plog::Warn("hotkeys: right-click binds are off (signature not found); the left-click binds still work");
	}
	std::snprintf(report, size, "inventory hotkeys: Ctrl+right drop, Shift+right stash, Ctrl+Shift+right Cube, Shift+left identify (core-ui-hotkeys.toml)%s",
	              right ? "" : "; right-click binds off");
}

}  // namespace hotkeys
