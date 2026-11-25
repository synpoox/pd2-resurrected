// PD2's item rules for Ormus' Robes (docs/research/items/p12-ormus-robes.md):
//
// - properties func 25 (`sorc-skill-rand-ctc`, ProjectDiablo.dll 0x103a12a0) in D2R's free property slot 26: one
//   random skill of PD2's sorceress list gets +param, and the item casts that skill on cast (item_skilloncast) at
//   chance min and level max, both adjusted by the skill as PD2 does.
//   The Cast Triggers companion fires item_skilloncast (IR2, the stat's event in itemstatcost.txt).
// - curses (docs/research/items/ir4-curses.md): max curses (a target holds up to the caster's max_curses + 1 curses) and
//   curse resistance (curse_effectiveness on the target scales the curse's stats), around D2R's ApplyCurse; PD2's
//   gate (#264): resisted-to-0 cancels, stat 109 cuts the duration (cap 75 on a player), attract no longer refuses.
//   Dark Pact (#265) consumes a curse and explodes around the target, scaled by the curses it held.
// - skill while equipped and phasing (docs/research/items/ir7-equipped-items.md): item_skillonequip runs its skill as an
//   item aura (D2R's item_aura case and tick, fed stat 375), and Phasing SelfAura drops the unit collision bits.
// - critical and deadly strike (IR3, docs/research/items/ir3-combat-stats.md): PD2's roll and multipliers in place of
//   D2R's, in WeaponDamage's roll block (a byte patch calling WeaponStrike) and D2R's missile roll.
// - op bases, life steal cap and crushing blow (IR3): equipped_eth / missing_hp kept as base stats from the stat
//   callback, lifedrain_percentcap on D2R's add-life, PD2's blow in CrushingBlowApply's jump slot, and its roll on
//   HitEvents' event 5/6 calls, where PD2's open wounds and deep wounds (event 15) run too.
// - dual wield attack rate (#1867): a player's two weapons attack at the faster one's rate, in D2R's unit rate setter.
#include "item/shared.h"

static int g_itemTotal = 0;

auto InstallItemRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	g_context = context;
	int installed = 0;
	int total = 0;  // every rule below: ItemRuleCount() reports it, so there is no literal to keep
	size_t used = 0;
	const auto rule = [&](bool ok, const char* name, const char* fail = " FAILED (bytes differ)") {
		installed += ok ? 1 : 0;
		++total;
		if (used < reportSize) {
			const int n = std::snprintf(report + used, reportSize - used, "%s%s%s", used ? "; " : "", name, ok ? "" : fail);
			used += n > 0 ? static_cast<size_t>(n) : 0;
		}
	};
	if (reportSize > 0) {
		report[0] = 0;
	}
	if (context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize)) {
		g_threads = nullptr;  // the server stats still work; only the client's tooltips miss them
	}
	if (context->QueryService(&g_tables) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasDataTableServiceField(g_tables, D2RL::DataTableServiceRequiredSize)) {
		g_tables = nullptr;  // InBank then refuses: no op bases at all rather than a save that fails
		plog::Warn("op bases: no data table service, equipped_eth and missing_hp are off");
	}
	static constexpr uint8_t Empty[8] = {};
	using PropertyFn = int32_t (*)(uint8_t, uint32_t, uint64_t, Unit*, const void*, uint8_t, uint16_t, uint16_t,
	                               int32_t, uint32_t, uint32_t, uint64_t) noexcept;
	const bool property = PropertySlot < PropertyFuncSlots &&
	                      context->PatchWriteU64(PropertyFuncTable + static_cast<uint64_t>(PropertySlot) * 8, Empty,
	                                             sizeof(Empty),
	                                             reinterpret_cast<uint64_t>(static_cast<PropertyFn>(PropSorcSkillRandCtc)));
	char ormus[40];
	std::snprintf(ormus, sizeof(ormus), "property func %d ormus", PropertySlot);
	rule(property, ormus, " FAILED (slot not empty)");
	const bool weaponStrike = PatchWeaponCrit(context);
	rule(weaponStrike, "weapon strike");
	const bool missileStrike = context->InstallInlineHook(MissileCritRoll, MissileCritRollEntry,
	                                                      sizeof(MissileCritRollEntry), MissileStrike, &g_missileRollOriginal);
	rule(missileStrike, "missile strike");
	const bool blowRoll = PatchHitEvents(context);
	rule(blowRoll, "crushing blow roll", " FAILED (call sites)");
	plog::Info("slow: skip prime evils %s", blowRoll ? "on" : "FAILED");  // rides on the HitEvent relay
	const bool lifeCap = context->InstallInlineHook(AddLife, AddLifeEntry, sizeof(AddLifeEntry),
	                                                CappedAddLife, &g_addLifeOriginal);
	rule(lifeCap, "life steal cap");
	// The slot holds whatever the image or D2RCore put there; PD2's blow replaces it whole, so it is not kept
	const uint64_t blowSlot = *reinterpret_cast<const uint64_t*>(d2r::imageBase + CrushingBlowApplySlot);
	using BlowFn = int32_t (*)(Game*, Unit*, Unit*, d2r::CrushingBlowArgs*) noexcept;
	const bool blow = blowSlot != 0 && context->PatchWriteU64(CrushingBlowApplySlot, &blowSlot, sizeof(blowSlot),
	                                                          reinterpret_cast<uint64_t>(static_cast<BlowFn>(CrushingBlow)));
	rule(blow, "crushing blow", " FAILED (slot write)");
	const bool curses = context->InstallInlineHook(ApplyCurse, ApplyCurseEntry, sizeof(ApplyCurseEntry), CurseApply,
	                                               &g_applyCurseOriginal);
	rule(curses, "curses");
	// Only with the curse hook: without it the boss curse would skip the rules all the same
	uint64_t bossCurseAt = 0;
	const bool bossCurse = curses && sigscan::ScanUnique(context->exeBase, BossCursePattern, 0, &bossCurseAt) == 1 && [&] {
		const uint64_t call = context->exeBase + bossCurseAt + BossCurseHostileCall;
		g_bossCurseHostile = reinterpret_cast<int32_t (*)(Game*, Unit*, Unit*) noexcept>(
			call + 5 + *reinterpret_cast<const int32_t*>(call + 1));
		plog::Info("found BossCurse at 0x%llx", static_cast<unsigned long long>(bossCurseAt));
		return context->InstallInlineHook(bossCurseAt, BossCurseEntry, sizeof(BossCurseEntry), BossCurse,
		                                  &g_bossCurseOriginal);
	}();
	rule(bossCurse, "boss curse", " FAILED (curse hook, signature or bytes differ)");
	const bool zeroFound = FindZeroChargeSkills(context);  // before the init hook hands out a callback
	// Both or neither: a callback without the tick would start item_skillonequip's aura and stop it at its first tick
	const bool tick = context->InstallInlineHook(ItemAuraTick, ItemAuraTickEntry, sizeof(ItemAuraTickEntry),
	                                             EquipAuraTick, &g_itemAuraTickOriginal);
	const bool equip = tick && context->InstallInlineHook(StatCallbackInit, StatCallbackInitEntry,
	                                                      sizeof(StatCallbackInitEntry), EquipStatCallbackInit,
	                                                      &g_statCallbackInitOriginal);
	rule(equip, "skill on equip and op bases");
	const bool zeroCharges = equip && zeroFound;
	plog::Info("charges: keep 0-charge skills %s", zeroCharges ? "on" : "FAILED (signature, or the stat callback hook)");
	rule(zeroCharges, "zero-charge skills", " FAILED (signature, or the stat callback hook)");
	// The scheduler first: the handler alone would only see events that durability or quantity items schedule
	const bool replenish = context->InstallInlineHook(ScheduleReplenish, ScheduleReplenishEntry,
	                                                  sizeof(ScheduleReplenishEntry), ReplenishSchedule,
	                                                  &g_scheduleReplenishOriginal) &&
	                       context->InstallInlineHook(ReplenishEvent, ReplenishEventEntry, sizeof(ReplenishEventEntry),
	                                                  ChargesReplenishEvent, &g_replenishEventOriginal);
	rule(replenish, "replenish charges");
	// Unlimited books (IR5): D2R's item spell table is data; its free entry 17 gets the Town Portal handlers and 18
	// none (core-events' hooks identify all), both with keep 1, so the book stays after its use
	const bool unlimited = PatchUnlimitedBooks(context);
	rule(unlimited, "unlimited books", " FAILED (entries not empty, or no mutation service)");
	const bool charms = InstallCharmRows(context);
	rule(charms, "charm rows", " FAILED (signature or bytes differ)");
	const bool telekinesis = PatchTelekinesisArrows(context);
	rule(telekinesis, "telekinesis no arrows", " FAILED (signature or bytes differ)");
	const bool shrinePotions = PatchShrinePotions(context);
	rule(shrinePotions, "shrine potions", " FAILED (signature or bytes differ)");
	const bool curseLength = PatchCurseLength(context);
	rule(curseLength, "curse length", " FAILED (signature, bytes or states.txt)");
	const bool halfFreeze = PatchHalfFreeze(context);
	rule(halfFreeze, "half freeze", " FAILED (signature or bytes differ)");
	const bool chillRates = PatchChillRates(context);
	rule(chillRates, "chill rates", " FAILED (signature, call or relay)");
	const bool stunLength = PatchStunLength(context);
	rule(stunLength, "stun length", " FAILED (signature, call or relay)");
	// One rule, ok only with both halves: a client roll capped apart from the server's draws the missile stopping early.
	// Both sites are scanned before either hook goes in, so a build that moved one leaves both rolls stock.
	// shortcut: if the loader refuses the second hook after the first went in, the first stays; undo it if that ever happens.
	uint64_t pierceServerAt = 0;
	uint64_t pierceClientAt = 0;
	const bool pierceCap =
	    sigscan::ScanUnique(context->exeBase, PierceRollEntry, 0, &pierceServerAt) == 1 &&
	    sigscan::ScanUnique(context->exeBase, ClientPierceRollEntry, 0, &pierceClientAt) == 1 &&
	    sigscan::HookBySignature(context, "PierceRoll", PierceRollEntry, 0, PierceRollStock, sizeof(PierceRollStock),
	                             PierceRoll, &g_pierceRollOriginal) &&
	    sigscan::HookBySignature(context, "ClientPierceRoll", ClientPierceRollEntry, 0, ClientPierceRollStock,
	                             sizeof(ClientPierceRollStock), ClientPierceRoll, &g_clientPierceRollOriginal);
	if (pierceCap) {
		plog::Info("pierce cap: server and client missile pierce rolls hooked, PD2's cap 10 (6 for Lightning Fury)");
	}
	rule(pierceCap, "pierce cap", " FAILED (signature or bytes differ)");
	const bool dualWield = PatchDualWieldRate(context);
	rule(dualWield, "dual wield rate", " FAILED (signature, bytes or relay)");
	const char* const dye = InstallDyeTint(context);
	rule(dye[0] == 0, "dye tint", dye);
	const bool mapStack = sigscan::HookBySignature(
	    context, "ItemsCanMerge", "48 89 5C 24 08 48 89 74 24 10 55 57 41 56 48 8B EC 48 83 EC 20", 0,
	    d2r::rva::ItemsCanMergeEntry, sizeof(d2r::rva::ItemsCanMergeEntry), MapStackEqual, &g_stackEqualOriginal);
	rule(mapStack, "map stacking");
	// 0x442d60 by signature (calls are masked); the entry is whole instructions. 0x442c60 waits for the tables (HookMagicMaker).
	static constexpr uint8_t ItemMakerEntry[] = {0x40, 0x53, 0x55, 0x41, 0x56, 0x48, 0x83, 0xec, 0x50, 0x4c, 0x8b, 0xf1, 0x49, 0x8b, 0xe8, 0x48, 0x8b, 0x0a};
	const D2RL::LifecycleService* lifecycle = nullptr;
	const D2RL::Lifecycle::DataTablesLoadedListener magicListener {.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
	                                                               .callback = HookMagicMaker};
	D2RL::Lifecycle::ListenerHandle magicHandle = D2RL::Lifecycle::InvalidHandle;
	const bool noMagicMaps = context->QueryService(&lifecycle) == D2RL::ServiceQueryResult::Success &&
	                         D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize) &&
	                         lifecycle->registerDataTablesLoadedListener(context, &magicListener, &magicHandle) ==
	                             D2RL::Lifecycle::Result::Success;
	const bool mapMakerLog = sigscan::HookBySignature(
	    context, "ItemMaker", "40 53 55 41 56 48 83 EC 50 4C 8B F1 49 8B E8 48 8B 0A 48 8B DA 48 85 C9 74 0A E8 ?? ?? ?? ?? 83 F8 04 74 14", 0,
	    ItemMakerEntry, sizeof(ItemMakerEntry), MapItemMaker, &g_itemMakerOriginal);
	rule(noMagicMaps, noMagicMaps ? "no magic maps (hooked when tables load)" : "no magic maps", " FAILED (no table listener)");
	rule(mapMakerLog, "map maker log", " FAILED (signature or bytes differ)");
	const bool goldPickup = InstallGoldPickup(context);
	rule(goldPickup, "gold pickup range", " FAILED (signature or bytes differ)");
	g_itemTotal = total;
	return installed;
}

auto ItemRuleCount() noexcept -> int {
	return g_itemTotal;  // set by InstallItemRules, which every caller runs first
}

auto ItemRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size,
	              "ormus rolls %u, strike rolls %u, crits %u, "
	              "deadly strikes %u, equipped_eth changes %u, life steal cap checks %u (blocked %u), crushing blow rolls %u, "
	              "crushing blows %u, curses %u, resisted %u, gated %u, skill on equip changes %u, ticks %u, phasing %u, "
	              "charges replenished %u, map stacks refused %u, magic maps refused %u, gold picked up near %u",
	              g_rolls.load(), g_strikeRolls.load(), g_crits.load(), g_deadlyStrikes.load(), g_ethReads.load(),
	              g_capChecks.load(), g_capBlocks.load(), g_blowRolls.load(), g_blows.load(),
	              g_curses.load(), g_curseResisted.load(), g_curseGate.load(), g_equipCallbacks.load(), g_equipTicks.load(), g_phasing.load(),
	              g_chargesReplenished.load(), g_mapStackRefused.load(), g_mapMagicRefused.load(), GoldPickupCount());
}
