// Skill while equipped, phasing and zero-charge skills (docs/research/items/ir7-equipped-items.md, #1992).
// Moved verbatim from item_rules.cpp (#2198).
#include "shared.h"

namespace itemrules {

// --- Skill while equipped and phasing (docs/research/items/ir7-equipped-items.md) -------------------------------------
// PD2 copies D2's item aura for item_skillonequip (0x1036f640, 0x10345cc0): the skill runs as an item aura at the
// stat's level. D2R's stat callback has no case for stat 375, so core-rules hands it item_aura's case, and the aura's
// tick reads item_skillonequip where it reads item_aura.
constexpr int32_t StatItemAura = 151;
constexpr int32_t StatSkillOnEquip = 375;  // item_skillonequip, merged id (PD2 191)
constexpr int32_t SkillPhasing = 674;      // Phasing SelfAura, merged id (PD2 605): Spectral Plate
constexpr int32_t SkillCharge = 107, SkillMonCharge = 486, SkillArmageddonSelfAura = 662;  // merged (PD2 107, 415, 593)
// Path collision bits PD2's phasing (0x10380c50) clears and sets back: 0x1c09 <-> 0xc09 on a player, 0x3c01 <-> 0xc01
// on a monster (a mercenary)
constexpr uint32_t PhasingBitsPlayer = 0x1000, PhasingBitsMonster = 0x3000;
constexpr int32_t UnitPlayer = 0;

std::atomic<uint32_t> g_equipCallbacks {0};
std::atomic<uint32_t> g_equipTicks {0};
std::atomic<uint32_t> g_phasing {0};

// PD2's 0x10380c50: while Phasing SelfAura is equipped, the unit's path leaves out the unit collision bits
auto SetPhasing(Unit* unit, bool on) noexcept -> void {
	using namespace d2r::rva;
	uint8_t* path = At<uint8_t*>(unit, UnitPath);
	const int32_t type = At<int32_t>(unit, UnitType);
	if (path == nullptr || (type != UnitPlayer && type != UnitMonster)) {
		return;
	}
	const uint32_t bits = type == UnitPlayer ? PhasingBitsPlayer : PhasingBitsMonster;
	const uint32_t mask = At<uint32_t>(path, PathCollisionMask);
	const uint32_t want = on ? mask & ~bits : mask | bits;
	if (want == mask) {
		return;
	}
	d2r::Engine<void (*)(uint8_t*, uint32_t) noexcept>(PathSetCollisionMask)(path, want);
	++g_phasing;
}

// The game the last durability callback came from; a session change discards the queued recounts with it
Game* g_ethGame = nullptr;

void RecountEthereal(const D2RL::PluginContext*, void* user) noexcept {
	const auto key = reinterpret_cast<uintptr_t>(user);
	Unit* unit = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(d2r::rva::FindUnit)(
		g_ethGame, static_cast<int32_t>(key >> 32), static_cast<uint32_t>(key));
	if (unit != nullptr) {
		RefreshOpBase(unit, StatEquippedEth);
	}
}

// --- Zero-charge skills stay on the skill list (#1992) -------------------------------------------------------------
// 1.13c's item stat handler (D2Game +0xd9470, stat 204 item_charged_skill) removes a charged skill when its charges
// reach 0 or the item is not the unit's (D2Common #10029, last argument 1), else adds it with its charges (last
// argument 0). PD2 NOPs the 0-charge test's jle (+0xd9a41; D2Client's twin at +0x18d51), so 0 charges take the add path
// (through ProjectDiablo.dll 0x1036f940, which only forces that argument to 0): the skill keeps its place and hotkey.
// D2R has the same case 204 in the server's stat callback (93847: 0x46fdd0, `test al,al; jz` at 0x470850) and in the
// client's (0xfd1c0, at 0xfd424), each before the owner check and the add/remove call (0x33a000), but D2RCore
// replaces both entries (`jmp [rip+x]`), so their bodies never run. No SDK service covers the skill list: the
// callbacks are wrapped where StatCallbackInit hands them out (EquipStatCallbackInit), and a 0-charge stat 204 of the
// unit's own item takes the add path with 0 charges there, as in PD2. With the add path D2R never flags the skill 0x10,
// so its after-cast removal (0x43b1d8) skips it too. A cast at 0 charges is refused by the charge use (0x4368a0:
// charges < 1 returns 0), as in PD2.
// The server callback's add/remove call (0x4708f8, to 0x33a000): (unit, item id, skill, level, charges, remove)
constexpr const char* ItemSkillCall = "44 89 74 24 28 48 8b cf 44 89 7c 24 20 e8 ?? ?? ?? ?? e9";
constexpr uint32_t ItemSkillCallAt = 13;
// The server callback's 0-charge test and owner check (0x470845): the second call (0x2f8120) is (unit, 0) -> the unit
// whose stats hold this unit's (null once an item is taken off)
constexpr const char* StatOwnerCall =
    "e8 ?? ?? ?? ?? 44 0f b6 f8 84 c0 74 0f 33 d2 48 8b ce e8 ?? ?? ?? ?? 48 3b c7 74";
constexpr uint32_t StatOwnerCallAt = 18;
// Where the client's stat callback (0xfd1c0) is registered (0xfede6): `lea r8,[rip+disp]` hands out its address. Scanned there,
// not at its entry, which another plugin's inline hook would change.
constexpr const char* ClientStatCallbackRegistration = "45 33 c9 4c 8d 05 ?? ?? ?? ?? 33 d2 48 8b ce e8";
constexpr uint32_t ClientStatCallbackDispAt = 6;  // the lea's disp32; the next instruction starts at +10
using ItemSkillFn = void* (*)(Unit*, uint32_t, int32_t, int32_t, int32_t, int32_t) noexcept;
ItemSkillFn g_itemSkill = nullptr;
Unit* (*g_statOwner)(Unit*, void*) noexcept = nullptr;
uint64_t g_clientStatCallback = 0;  // its address, 0 until FindZeroChargeSkills finds all three

// True when it kept the skill: 0 charges on the unit's own item, which D2R would remove
auto KeepZeroChargeSkill(Unit* unit, Unit* item, uint32_t layer) noexcept -> bool {
	using namespace d2r::rva;
	if (g_itemSkill == nullptr || unit == nullptr || item == nullptr ||
	    (StatOf(item, StatChargedSkill, static_cast<int32_t>(layer)) & 0xff) != 0 ||
	    g_statOwner(item, nullptr) != unit) {
		return false;
	}
	const uint32_t dataFlag = d2r::Engine<uint8_t (*)(Unit*) noexcept>(UnitDataFlag)(unit);
	if (DataTables(dataFlag) == nullptr) {
		return false;
	}
	const int32_t skill = static_cast<int32_t>(layer >> LayerShift(dataFlag));
	const int32_t level = static_cast<int32_t>(layer & LayerMask(dataFlag));
	g_itemSkill(unit, At<uint32_t>(item, UnitIdField), skill, level, 0, 0);
	return true;
}

auto ClientStatCallback(void* a1, Unit* unit, Unit* item, uint64_t statLayer, int32_t oldValue, int32_t newValue) noexcept
	-> uint64_t {
	if (statLayer >> 32 == StatChargedSkill && KeepZeroChargeSkill(unit, item, static_cast<uint32_t>(statLayer))) {
		return 0;
	}
	using Fn = uint64_t (*)(void*, Unit*, Unit*, uint64_t, int32_t, int32_t) noexcept;
	return reinterpret_cast<Fn>(g_clientStatCallback)(a1, unit, item, statLayer, oldValue, newValue);
}

using StatCallbackFn = uint64_t (*)(Game* game, Unit* unit, Unit* item, uint64_t statLayer, int32_t oldValue,
                                    int32_t newValue) noexcept;

// Every unit's stat callback (see StatCallbackInit): item_skillonequip goes to D2R's as item_aura, same layer. The
// stat id is r9's high dword, the layer its low one (seen in game: 375/624 arrives as 0x17700000270)
auto EquipStatCallback(Game* game, Unit* unit, Unit* item, uint64_t statLayer, int32_t oldValue, int32_t newValue) noexcept
	-> uint64_t {
	const auto original = d2r::Engine<StatCallbackFn>(d2r::rva::StatCallback);
	const uint64_t stat = statLayer >> 32;
	if (unit != nullptr && stat == StatDurability) {  // fCallback only for the op bases: D2R has no case
		// Counted at the next game update: while the callback runs, an item being taken off still sits in its slot
		g_ethGame = game;
		const uintptr_t key = static_cast<uintptr_t>(At<uint32_t>(unit, d2r::rva::UnitType)) << 32 |
		                      At<uint32_t>(unit, d2r::rva::UnitIdField);
		if (g_threads == nullptr ||
		    g_threads->runOnGameThread(g_context, RecountEthereal, reinterpret_cast<void*>(key)) !=
		        D2RL::Threads::Result::Success) {
			RefreshOpBase(unit, StatEquippedEth);
		}
		return 0;
	}
	if (unit != nullptr && stat == StatLife) {
		RefreshOpBase(unit, StatMissingHp);
		return 0;
	}
	if (stat == StatChargedSkill && KeepZeroChargeSkill(unit, item, static_cast<uint32_t>(statLayer))) {
		return 0;
	}
	if (stat != static_cast<uint64_t>(StatSkillOnEquip)) {
		const uint64_t result = original(game, unit, item, statLayer, oldValue, newValue);
		if (unit != nullptr && stat == StatMaxLife) {
			RefreshOpBase(unit, StatMissingHp);
		}
		return result;
	}
	const uint32_t layer = static_cast<uint32_t>(statLayer);
	const uint64_t result = original(game, unit, item, (static_cast<uint64_t>(StatItemAura) << 32) | layer, oldValue,
	                                 newValue);
	if (unit != nullptr) {
		if (layer == static_cast<uint32_t>(SkillPhasing)) {
			SetPhasing(unit, newValue > 0);  // item_aura's case starts the aura at newValue (0x4705f6)
		}
		const uint32_t n = ++g_equipCallbacks;
	}
	return result;
}

// Both or neither: one side alone would keep the skill on one list and drop it from the other
auto FindZeroChargeSkills(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t call = 0;
	uint64_t owner = 0;
	uint64_t registration = 0;
	if (sigscan::ScanUnique(context->exeBase, ItemSkillCall, ItemSkillCallAt, &call) != 1 ||
	    sigscan::ScanUnique(context->exeBase, StatOwnerCall, StatOwnerCallAt, &owner) != 1 ||
	    sigscan::ScanUnique(context->exeBase, ClientStatCallbackRegistration, 0, &registration) != 1) {
		return false;
	}
	const uint64_t client =
		registration + 10 + *reinterpret_cast<const int32_t*>(context->exeBase + registration + ClientStatCallbackDispAt);
	const auto target = [&](uint64_t rva) -> uint64_t {
		const uint64_t site = context->exeBase + rva;
		return *reinterpret_cast<const uint8_t*>(site) == 0xe8 ? site + 5 + *reinterpret_cast<const int32_t*>(site + 1) : 0;
	};
	const uint64_t skillFn = target(call);
	const uint64_t ownerFn = target(owner);
	if (skillFn == 0 || ownerFn == 0) {
		return false;
	}
	g_itemSkill = reinterpret_cast<ItemSkillFn>(skillFn);
	g_statOwner = reinterpret_cast<Unit* (*)(Unit*, void*) noexcept>(ownerFn);
	g_clientStatCallback = context->exeBase + client;
	return true;
}

StatCallbackInitFn g_statCallbackInitOriginal = nullptr;

auto EquipStatCallbackInit(Unit* unit, uint32_t flags, void* callback, void* arg) noexcept -> uint64_t {
	if (reinterpret_cast<uint64_t>(callback) == d2r::imageBase + d2r::rva::StatCallback) {
		callback = reinterpret_cast<void*>(static_cast<StatCallbackFn>(EquipStatCallback));
	} else if (g_clientStatCallback != 0 && reinterpret_cast<uint64_t>(callback) == g_clientStatCallback) {
		callback = reinterpret_cast<void*>(&ClientStatCallback);
	}
	return g_statCallbackInitOriginal(unit, flags, callback, arg);
}

ItemAuraTickFn g_itemAuraTickOriginal = nullptr;

// D2R's item aura tick (0x437230) at item_skillonequip's level, as PD2's 0x10345cc0 runs it; item_aura's own ticks
// and an aura whose stat is gone (the event is removed) stay D2R's
auto EquipAuraTick(Game* game, Unit* unit, int32_t key, int32_t skill, int32_t stateKey) noexcept -> void {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr || skill < 0 || StatOf(unit, StatItemAura, skill) > 0) {
		g_itemAuraTickOriginal(game, unit, key, skill, stateKey);
		return;
	}
	const int32_t level = StatOf(unit, StatSkillOnEquip, skill);
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* record = d2r::api.getSkillRecord(dataFlag, skill);
	const uint8_t* tables = DataTables(dataFlag);
	const int16_t state = record != nullptr ? At<int16_t>(record, SkAuraState) : -1;
	if (level <= 0 || state < 0 || tables == nullptr || static_cast<uint64_t>(state) >= At<uint64_t>(tables, TablesStateCount) ||
	    d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(unit) != 0) {
		g_itemAuraTickOriginal(game, unit, key, skill, stateKey);  // finds no item_aura level: removes the event
		if (skill == SkillPhasing) {
			SetPhasing(unit, level > 0);
		}
		return;
	}
	const auto stateKeyOf = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey);
	const auto setStateKey = d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetUnitStateKey);
	const auto setFlag = d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(SetUnitFlag);
	const int32_t savedKey = stateKeyOf(unit);
	setStateKey(unit, stateKey);
	// PD2's 0x10345cc0 casts nothing while the unit charges, and Armageddon SelfAura only while it stands, walks or
	// runs (modes 1, 2, 3 and town 5, 6)
	const void* currentSkill = d2r::Engine<void* (*)(Unit*) noexcept>(CurrentSkill)(unit);
	const int32_t current = currentSkill != nullptr ? d2r::Engine<int32_t (*)(const void*, const char*, int32_t) noexcept>(
	                                                UnitSkillId)(currentSkill, __FILE__, __LINE__)
	                                          : -1;
	const int32_t mode = At<int32_t>(unit, UnitMode);
	const bool moving = mode == 1 || mode == 2 || mode == 3 || mode == 5 || mode == 6;
	const bool cast = current != SkillCharge && current != SkillMonCharge && (skill != SkillArmageddonSelfAura || moving);
	if (cast) {
		setFlag(unit, UnitFlagItemAuraCast, 1);
		d2r::Engine<uint64_t (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(
			SrvSkillDispatch)(game, unit, skill, level, 1, 1, 0);
		setFlag(unit, UnitFlagItemAuraCast, 0);
	}
	const uint32_t flags = At<uint8_t>(record, SkFlags24);
	int32_t next = -1;
	if ((flags & At<uint32_t>(reinterpret_cast<const void*>(d2r::imageBase), static_cast<int>(SkillTickMaskB))) != 0 ||
	    (flags & At<uint32_t>(reinterpret_cast<const void*>(d2r::imageBase), static_cast<int>(SkillTickMaskA))) != 0) {
		const int32_t calc = d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(record, SkPerDelayCalc), skill, level);
		const int32_t delay = calc > 5 ? calc : 5;
		const int32_t frame = At<int32_t>(game, GameFrame) - 1 + delay;
		next = frame - frame % delay + 1;
		d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(RemoveUnitEvent)(game, unit, UnitEventItemAura, key);
		d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(AddUnitEvent)(
			game, unit, UnitEventItemAura, next, key, skill, stateKey);
	}
	setStateKey(unit, savedKey);
	if (skill == SkillPhasing) {
		SetPhasing(unit, true);
	}
	const uint32_t n = ++g_equipTicks;
}

}  // namespace itemrules
