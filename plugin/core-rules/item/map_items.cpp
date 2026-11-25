// Maps (cmap): never magic, stack by stats (#1415, #1326).
// Moved verbatim from item_rules.cpp (#2198).
#include "shared.h"


// Maps (cmap) are never magic (#1415, owner rule #1304): normal, rare or unique. The map TCs lean magic (Map Tier 1-3 Magic 800,
// champion and boss parents 1024) and itemtypes cannot refuse magic, so a magic map is stopped where it is made. D2R's item maker
// (0x442d60) sends quality 4 to the magic affix maker (0x442c60), and the magic fallback of a failed set, rare or unique roll
// ends there too. Refusing for a map (return 0 = "failed") makes the maker fall through to its own superior and normal makers,
// whatever rolled the quality: a forced quality, any call site, or Cartographer's roll hook being absent. Cartographer hooks
// the quality roll and the magic fallback instead (other addresses, so both hooks install); this one is the backstop.
// No PluginSDK service runs inside item creation. The maker is hooked as well, only to log each map's final quality.
MagicMakerFn g_magicMakerOriginal = nullptr;
ItemMakerFn g_itemMakerOriginal = nullptr;
std::atomic<uint32_t> g_mapMagicRefused {0};
std::atomic<uint32_t> g_mapMade[10];

auto IsMapUnit(Unit* item) noexcept -> bool {
	return item != nullptr &&
	       d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(d2r::rva::ItemIsType)(item, d2r::rva::ItemTypeMap) != 0;
}

auto MapMagicMaker(Unit** item, void* request) noexcept -> uint64_t {
	if (item != nullptr && IsMapUnit(*item)) {
		++g_mapMagicRefused;
		return 0;
	}
	return g_magicMakerOriginal(item, request);
}

auto MapItemMaker(void* game, Unit** item, void* request) noexcept -> uint64_t {
	const uint64_t made = g_itemMakerOriginal(game, item, request);
	if (made != 0 && item != nullptr && IsMapUnit(*item)) {
		const int32_t quality = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::ItemQuality)(*item);
		const uint32_t n = ++g_mapMade[quality >= 0 && quality < 10 ? quality : 0];
	}
	return made;
}

// Maps (cmap) stack only when both are normal quality and every stat but the quantity matches, layer by layer (#1326).
// Hooks D2Common's stack-equal check (no SDK service covers it): the drop-onto merge, ground pickup, vendor buys and
// the client's cursor guess all call it.
// D2Common Items.cpp #11102: (target, other) -> 1 when two items may stack; found at load by its signature
// (docs/research/maps/map-stacking.md).
StackEqualFn g_stackEqualOriginal = nullptr;
std::atomic<uint32_t> g_mapStackRefused {0};

auto MapStackEqual(Unit* a, Unit* b) noexcept -> int32_t {
	using namespace d2r::rva;
	const auto isMap = [](Unit* u) {
		return u != nullptr && d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType)(u, ItemTypeMap) != 0 &&
		       d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemQuality)(u) == QualityNormal;
	};
	if (isMap(a) && isMap(b)) {
		// Create writes -1 at item data +0x34 (the unique/set row) for a normal item but the save decoder does not, and the native
		// compare reads it: a fresh map would not stack with a loaded one (#1340). Not saved for normal items.
		const auto data = d2r::Engine<uint8_t* (*)(Unit*) noexcept>(UnitItemData);
		auto* const ra = reinterpret_cast<int32_t*>(data(a) + 0x34);
		auto* const rb = reinterpret_cast<int32_t*>(data(b) + 0x34);
		*ra = -1;
		*rb = -1;
	}
	const int32_t same = g_stackEqualOriginal(a, b);
	if (same == 0 || a == nullptr || b == nullptr ||
	    d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType)(a, ItemTypeMap) == 0) {
		return same;
	}
	const auto quality = d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemQuality);
	const char* reason = "equal";
	int32_t stat = -1;
	if (quality(a) != QualityNormal || quality(b) != QualityNormal) {
		reason = "quality";
	} else if ((stat = pd2::mapstack::FirstDifference(
	                a, b, ItemStatCostRows, StatQuantity,
	                d2r::Engine<int32_t (*)(Unit*, uint32_t, pd2::mapstack::Entry*, int32_t) noexcept>(UnitStatEntries))) >= 0) {
		reason = "stat differs";
	}
	const bool merge = reason[0] == 'e';
	if (!merge) {
		++g_mapStackRefused;
	}
	return merge ? 1 : 0;
}

// Progressive Affixes checks the magic affix maker's bytes when it loads and refuses if they changed. Our plugins load
// from the mod folder, core-rules.dll before d2rl-ruffneckk-progressive-affixes.dll (#1180), so this hook waits for
// the first table load, after every plugin's load-time hooks, whatever the load order.
// Runs after ReleaseReserve: its trampoline may take one freed block; 15+ stay for Cast Triggers' 5 relay pages (#1180)
void __cdecl HookMagicMaker(const D2RL::PluginContext* context, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	static bool tried = false;
	if (tried) {
		return;
	}
	tried = true;
	// 0x442c60 by signature (the loader patches its +0x18 load into a call, so those 6 bytes are masked); a whole-instruction entry
	static constexpr uint8_t MagicMakerEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
	const bool ok = sigscan::HookBySignature(
	    context, "MagicAffixMaker",
	    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 30 ?? ?? ?? ?? ?? ?? 45 33 FF 48 8B F2 48 8B F9", 0,
	    MagicMakerEntry, sizeof(MagicMakerEntry), MapMagicMaker, &g_magicMakerOriginal);
	if (ok) {
		plog::Info("no magic maps: magic affix maker hooked after the first table load");
	} else {
		plog::Warn("no magic maps: magic affix maker hook FAILED (signature or bytes differ)");
	}
}
