// Crushing blow, open wounds and deep wounds (IR3): PD2's hit events in place of D2R's.
// Moved verbatim from item_rules.cpp (#2198).
#include "shared.h"

namespace itemrules {

// --- Crushing blow (IR3): PD2's blow (event 16, 0x103469a0) in place of D2R's CrushingBlowApply ---
constexpr uint32_t StatDamageResist = 36;
constexpr uint32_t StatPlayerCount = 100;   // monster_playercount
constexpr uint32_t StatCbEfficiency = 394;  // item_crushingblow_efficiency (PD2's 268)
constexpr int32_t OverlayCrushingBlow = 147;  // PD2's 0x93

std::atomic<uint32_t> g_blows {0};

// PD2's divisor: 10 on a player or hireling; on a prime evil (monstats flag) 70 + 10 per missing life percent (1.13c's
// D2Common #11027 is the life percent); 8 on others; a monster then adds its player count's life bonus / 50.
// shortcut: PD2's divisor 30 for its own boss lists (0x10371290, built at run time) is not ported; those bosses
// are PD2 content not in the merged data. Upgrade with them.
auto BlowDivisor(Game* game, Unit* target) noexcept -> double {
	using namespace d2r::rva;
	const int32_t type = At<int32_t>(target, UnitType);
	if (type == 0) {
		return 10.0;
	}
	if (type != 1) {
		return 8.0;
	}
	const int32_t classId = At<int32_t>(target, UnitClassId);
	for (int32_t hireling : HirelingClasses) {
		if (classId == hireling) {
			return 10.0;
		}
	}
	double divisor = 8.0;
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(d2r::DataFlag(game), classId);
	if (record != nullptr && (At<uint32_t>(record, MonFlags) & MonPrimeEvil) != 0) {
		const int32_t maxLife = Stat(target, StatMaxLife) >> 8;
		const int32_t lifePercent = maxLife != 0 ? (Stat(target, StatLife) >> 8) * 100 / maxLife : 0;
		divisor = (100.0 - lifePercent) * 10.0 + 70.0;
	}
	const int32_t players = std::max(Stat(target, StatPlayerCount), 1);
	const int32_t bonus = players < 9 ? (players > 1 ? (players - 1) * 70 : 0) : (players - 2) * 50;  // D2Game 0xaf5f0 as PD2 patches it (#2025)
	return divisor + bonus / 50.0;
}


// PD2's effectiveness: 1, plus Smite's calc4 / 100 while the unit's current skill is Smite, at the unit skill's hard
// points (1.13c's skill +0x28)
auto BlowEffectiveness(Game* game, Unit* unit) noexcept -> double {
	using namespace d2r::rva;
	void* current = d2r::Engine<void* (*)(Unit*) noexcept>(CurrentSkill)(unit);
	if (current == nullptr ||
	    d2r::Engine<int32_t (*)(const void*, const char*, int32_t) noexcept>(UnitSkillId)(current, __FILE__, __LINE__) !=
	        SkillSmite) {
		return 1.0;
	}
	const uint8_t* record = d2r::api.getSkillRecord(d2r::DataFlag(game), SkillSmite);
	const int32_t level = d2r::Engine<int32_t (*)(Unit*, void*, int32_t) noexcept>(UnitSkillLevel)(unit, current, 0);
	return record != nullptr ? 1.0 + d2r::EvalCalc(game, unit, record, SkCalc4, SkillSmite, level) / 100.0 : 1.0;
}

// PD2's blow: life / (divisor / (efficiency / 100 + effectiveness)) (divisor x1.5 for a missile), efficiency =
// item_crushingblow_efficiency + its per-hand sum (Two Hand Mastery's 2han layer), less the target's damageresist
// percent, nothing at 100 or more; then the kill flag and the overlay. D2R's event 16 and Smite's roll
// (skill_functions.cpp) both call it.
auto CrushingBlow(Game* game, Unit* unit, Unit* target, d2r::CrushingBlowArgs* args) noexcept -> int32_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr || target == nullptr || args == nullptr) {
		return 0;
	}
	const int32_t resist = Stat(target, StatDamageResist);
	if (resist >= 100) {
		return 0;
	}
	const double divisor = BlowDivisor(game, target) * (args->missile != 0 ? 1.5 : 1.0);
	const int32_t efficiency = Stat(unit, StatCbEfficiency) + HandStatSum(unit, StatCbEfficiency);
	const double effectiveness = BlowEffectiveness(game, unit);
	const double life = static_cast<uint32_t>(Stat(target, StatLife));
	double blow = life / (divisor / (efficiency / 100.0 + effectiveness));
	blow -= static_cast<int32_t>(static_cast<int64_t>(static_cast<int32_t>(blow)) * resist / 100);  // 0x1037c4c0
	const int32_t after = std::max(static_cast<int32_t>(life - blow), 0);
	d2r::Engine<void (*)(Unit*, uint32_t, int32_t, uint32_t) noexcept>(SetUnitStat)(target, StatLife, after, 0);
	if (after < 1 && args->damage != nullptr) {
		*reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(args->damage) + DamageResult) |= 2;  // killed
	}
	if (blow > 0) {
		d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetOverlay)(target, OverlayCrushingBlow, 0);
	}
	const uint32_t n = ++g_blows;
	return 1;
}

// PD2's event 16 roll on a hit: item_crushingblow plus its per-hand sum, % 100 on the unit's seed. D2R would roll it
// from the item's event node (itemeventfunc 16), but that node runs D2RCore's own copy of the event table: D2R's blow
// (a quarter of a monster's life, less its damage resist), never D2R's event 16 or CrushingBlowApply's slot (seen in
// game, #446). core-rules hides those nodes from D2RCore's event dispatch for the call (no SDK service reaches it) and
// rolls PD2's blow itself after it.
// Smite rolls its own (skill_functions.cpp, srvdo 150) with calc3 added, so a Smite hit gets no second roll.
constexpr uint32_t StatCrushingBlow = 136;
constexpr int32_t EventMeleeDamage = 5, EventMissileDamage = 6;
constexpr int MaxHiddenNodes = 16;  // a unit wears 12 items; more only guards a corrupt chain
constexpr uint16_t NoEvent = 0xffff;  // no event raises it

std::atomic<uint32_t> g_blowRolls {0};

// D2R's unit-stat event function (93847: 0x44d570; game, event, attacker, target, damage), which HitEvents (0x44ce80)
// calls for events 5 and 6 and which reaches D2RCore's dispatcher through the 0x5881e0 thunk and its slot. Taken from
// the two call sites' own rel32 (below), not a fixed address.
using HitEventFn = int32_t (*)(Game*, int32_t, Unit*, Unit*, void*) noexcept;
HitEventFn g_hitEventOriginal = nullptr;

// PD2's open wounds (below): item_openwounds and map_mon_openwounds nodes are hidden the same way
constexpr uint32_t StatOpenWounds = 135;     // item_openwounds
constexpr uint32_t StatMapOpenWounds = 460;  // map_mon_openwounds (PD2 407), the same event
auto OpenWounds(Game* game, Unit* unit, Unit* target, uint32_t stat) noexcept -> void;
thread_local int32_t t_hiddenEvent = -1;  // the event whose nodes an outer HitEvent on this thread has hidden

// Called in place of D2R's unit-stat event function at HitEvents' two calls that raise events 5 and 6 (relayed by
// PatchHitEvents). Not the dispatch slot: Cast Triggers requires D2RCore's own dispatcher there (#385).
auto HitEvent(Game* game, int32_t event, Unit* unit, Unit* target, void* damage) noexcept -> int32_t {
	using namespace d2r::rva;
	if ((event != EventMeleeDamage && event != EventMissileDamage) || game == nullptr || unit == nullptr ||
	    target == nullptr || damage == nullptr) {
		return g_hitEventOriginal(game, event, unit, target, damage);
	}
	uint8_t* hidden[MaxHiddenNodes];
	uint32_t woundStats[MaxHiddenNodes];
	int count = 0, wounds = 0;
	for (auto* node = At<uint8_t*>(unit, UnitEventNodes);
	     node != nullptr && count < MaxHiddenNodes && wounds < MaxHiddenNodes; node = At<uint8_t*>(node, EventNodeNext)) {
		const uint16_t nodeEvent = At<uint16_t>(node, EventNodeEvent);
		const auto stat = static_cast<uint32_t>(At<int32_t>(node, EventNodeStat));
		const bool wound = stat == StatOpenWounds || stat == StatMapOpenWounds;
		// A hit inside the outer dispatch (a splash item event hitting more units) finds the nodes the outer call hid;
		// PD2 runs open wounds on those hits too
		if (nodeEvent == NoEvent && wound && t_hiddenEvent == event) {
			woundStats[wounds++] = stat;
			continue;
		}
		if (nodeEvent != event) {
			continue;
		}
		if (stat == StatCrushingBlow || wound) {
			*reinterpret_cast<uint16_t*>(node + EventNodeEvent) = NoEvent;
			hidden[count++] = node;
			if (stat != StatCrushingBlow) {
				woundStats[wounds++] = stat;
			}
		}
	}
	const int32_t outerHidden = t_hiddenEvent;
	if (count != 0) {
		t_hiddenEvent = event;
	}
	SlowNodes slow;  // PD2's slow skips prime evils (#2004, skill/slow_prime_evil.cpp)
	SlowPrimeEvilBegin(game, event, unit, target, slow);
	const int32_t result = g_hitEventOriginal(game, event, unit, target, damage);
	// Inside the hidden-node window, so the splashed units get open wounds as a plain splash's do
	if (event == EventMeleeDamage) {
		SpinSplashes(game, event, unit, target, static_cast<const uint8_t*>(damage));
	}
	SlowPrimeEvilEnd(slow);
	t_hiddenEvent = outerHidden;
	for (int i = 0; i < count; ++i) {
		*reinterpret_cast<uint16_t*>(hidden[i] + EventNodeEvent) = static_cast<uint16_t>(event);
	}
	for (int i = 0; i < wounds; ++i) {
		if (d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(target) == 0) {
			OpenWounds(game, unit, target, woundStats[i]);
		}
	}
	const int32_t chance = Stat(unit, StatCrushingBlow) + HandStatSum(unit, StatCrushingBlow);
	if (chance <= 0 || d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(target) != 0) {
		return result;
	}
	void* current = d2r::Engine<void* (*)(Unit*) noexcept>(CurrentSkill)(unit);
	if (current != nullptr &&
	    d2r::Engine<int32_t (*)(const void*, const char*, int32_t) noexcept>(UnitSkillId)(current, __FILE__, __LINE__) ==
	        SkillSmite) {
		return result;
	}
	const int32_t roll = static_cast<int32_t>(d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit), 100));
	++g_blowRolls;
	if (roll < chance) {
		d2r::CrushingBlowArgs args {damage, {4, 10, 10}, static_cast<uint8_t>(event == EventMissileDamage ? 1 : 0)};
		CrushingBlow(game, unit, target, &args);
	}
	return result;
}

// HitEvents' two calls to the event function, found by their argument set-up (event 6 missile, event 5 melee; 93847:
// 0x44cfc5 and 0x44cfe4), each followed by `E8 rel32`.
sigscan::Entry HitMissileCall {"HitEventMissile", "hitevent", sigscan::Kind::Code, 0, 0,
                               "4c 8b ce 48 89 7c 24 20 4d 8b c6 ba 06 00 00 00 49 8b cf e8 ?? ?? ?? ??"};
sigscan::Entry HitMeleeCall {"HitEventMelee", "hitevent", sigscan::Kind::Code, 0, 0,
                             "4c 8b ce 48 89 7c 24 20 4d 8b c6 ba 05 00 00 00 49 8b cf e8 ?? ?? ?? ??"};
sigscan::Entry* const HitOwn[] = {&HitMissileCall, &HitMeleeCall};
constexpr int HitCallAt = 19;  // the E8 inside the match

// A 14-byte `jmp [rip+0]; &HitEvent` within +-2 GB of the image: a rel32 call reaches it, which the SDK's
// PatchCallRel32 cannot target (plugin memory). No SDK service covers unit events (#385), so the two calls are
// rewritten with the MutationService's byte patch (PatchBytes), as Cast Triggers does for its own relays.
auto AllocateRelay(uint64_t image) noexcept -> uint8_t* {
	// From the far ends of the window, so the near range stays free for other plugins' relays (Cast Triggers'
	// allocation failed intermittently when this took a block next to the image).
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	const uint64_t step = info.dwAllocationGranularity;
	const uint64_t reach = 0x70000000ull;
	const uint64_t low = image > reach ? image - reach : step;
	for (uint64_t at = (low + step - 1) & ~(step - 1); at < image; at += step) {
		if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
			return static_cast<uint8_t*>(p);
		}
	}
	for (uint64_t at = (image + reach) & ~(step - 1); at > image; at -= step) {
		if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
			return static_cast<uint8_t*>(p);
		}
	}
	return nullptr;
}

// Both calls or neither: one MutationService transaction checks and writes both, and restores both on a failure.
auto PatchHitEvents(const D2RL::PluginContext* context) noexcept -> bool {
	sigscan::Resolve(HitOwn, std::size(HitOwn), context->exeBase, nullptr, 0);
	if (HitMissileCall.state != sigscan::State::Ok || HitMeleeCall.state != sigscan::State::Ok) {
		return false;
	}
	const uint64_t sites[2] = {HitMissileCall.address + HitCallAt, HitMeleeCall.address + HitCallAt};
	uint8_t stock[2][5];
	uint64_t target[2];
	for (int i = 0; i < 2; ++i) {
		std::memcpy(stock[i], reinterpret_cast<const void*>(sites[i]), 5);
		int32_t rel;
		std::memcpy(&rel, stock[i] + 1, 4);
		target[i] = sites[i] + 5 + static_cast<int64_t>(rel);
	}
	if (target[0] != target[1]) {
		return false;
	}
	const D2RL::MutationService* mutations = nullptr;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize)) {
		return false;
	}
	uint8_t* relay = AllocateRelay(context->exeBase);
	if (relay == nullptr) {
		return false;
	}
	const uint64_t entry = reinterpret_cast<uint64_t>(&HitEvent);
	const uint8_t jump[6] = {0xff, 0x25, 0, 0, 0, 0};
	std::memcpy(relay, jump, sizeof(jump));
	std::memcpy(relay + sizeof(jump), &entry, 8);
	DWORD old;
	uint8_t call[2][5] = {{0xe8}, {0xe8}};
	bool ok = VirtualProtect(relay, 0x1000, PAGE_EXECUTE_READ, &old) != 0;
	for (int i = 0; ok && i < 2; ++i) {
		const int64_t rel = static_cast<int64_t>(reinterpret_cast<uint64_t>(relay)) - static_cast<int64_t>(sites[i] + 5);
		const int32_t rel32 = static_cast<int32_t>(rel);
		ok = rel == rel32;
		std::memcpy(call[i] + 1, &rel32, 4);
	}
	D2RL::Mutations::TransactionHandle transaction = 0;
	ok = ok && mutations->beginTransaction(context, &transaction) == D2RL::Mutations::Result::Success;
	if (!ok) {
		VirtualFree(relay, 0, MEM_RELEASE);
		return false;
	}
	g_hitEventOriginal = reinterpret_cast<HitEventFn>(target[0]);  // before any call can reach HitEvent
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	for (int i = 0; ok && i < 2; ++i) {
		const D2RL::Mutations::BytePatchRequest patch{.structSize = D2RL::Mutations::BytePatchRequestSize,
		                                              .rva = sites[i] - context->exeBase,
		                                              .expected = stock[i],
		                                              .expectedSize = 5,
		                                              .bytes = call[i],
		                                              .size = 5};
		D2RL::Mutations::OperationHandle operation = 0;
		ok = mutations->stageBytePatch(context, transaction, &patch, &operation) == D2RL::Mutations::Result::Success;
	}
	const bool staged = ok;
	if (!ok || mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		// A failed commit that did not restore the stock bytes may leave a call aimed at the relay: keep the page then.
		if (!staged || D2RL::Mutations::HasFlag(result.flags, D2RL::Mutations::CommitFlags::OriginalStateRestored)) {
			VirtualFree(relay, 0, MEM_RELEASE);
		}
		return false;
	}
	return true;
}

// --- Open wounds (IR3): PD2's event 15 (0x103463f0) in place of D2R's ----------------------------------------------
constexpr uint32_t StatLevel = 12;
constexpr uint32_t StatHpRegen = 74;
constexpr uint32_t StatOpenWoundsStack = 373;  // openwounds_stack (PD2 189)
constexpr uint32_t StatDeepWounds = 554;       // deep_wounds (PD2 501)
constexpr uint32_t StatPhysPierce = 478;       // passive_phys_pierce (PD2 425)
constexpr int32_t StateOpenWounds = 62;
constexpr int32_t StateDeepWounds = 271;  // the Barbarian's Deep Wounds passive (PD2 225)
constexpr int32_t BloodGolem = 290;
constexpr int32_t OpenWoundsFrames = 125;  // D2R's 200
constexpr int32_t MaxWoundStacks = 3;
constexpr int32_t PlayerWoundPercent[] = {8, 6, 4};  // on a player, by difficulty (doubles 0x1044fbc0 / b8 / a8)

std::atomic<uint32_t> g_wounds {0};
constexpr int32_t StateResistAll = 8;

// PD2's 0x1037c4c0 with 100: value * percent / 100 in doubles, truncated, saturated at INT32_MAX
auto Percent(int32_t value, double percent) noexcept -> int32_t {
	const double v = static_cast<double>(value) * percent / 100.0;
	return v > 2147483647.0 ? 2147483647 : static_cast<int32_t>(v);
}

// 1.13c D2Game 0xac940 with PD2's slopes {9, 18, 27, 36, 45}, plus 40: D2R's own level table (0x584170) is the same
auto WoundsByLevel(int32_t level) noexcept -> int32_t {
	constexpr int32_t s[] = {9, 18, 27, 36, 45};
	int32_t v = 0;
	if (level >= 2) {
		if (level > 60) {
			v = (s[1] + s[2] + s[3]) * 15 + (level - 60) * s[4] + s[0] * 14;
		} else if (level > 45) {
			v = (s[1] + s[2]) * 15 + (level - 45) * s[3] + s[0] * 14;
		} else if (level > 30) {
			v = (level - 30) * s[2] + s[1] * 15 + s[0] * 14;
		} else if (level < 16) {
			v = (level - 1) * s[0];
		} else {
			v = (level - 15) * s[1] + s[0] * 14;
		}
	}
	return v + 40;
}

// PD2's 0x103749f0 (unit, 1) is a player: the monster's owner, or its owner's owner, is a player (a summon, a
// hireling, a summon's own minion)
auto OwnedByPlayer(Game* game, Unit* unit) noexcept -> bool {
	using namespace d2r::rva;
	for (int depth = 0; depth < 2 && unit != nullptr && At<int32_t>(unit, UnitType) == 1; ++depth) {
		if (pd2r::skills::OwnedByPlayer(unit)) {
			return true;
		}
		const int32_t type = At<int32_t>(unit, UnitOwnerType);
		const uint32_t id = At<uint32_t>(unit, UnitOwnerId);
		const auto find = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(FindUnit);
		Unit* owner = type == UnitMonster && pd2r::skills::ValidOwnerId(id) ? find(game, UnitMonster, id) : nullptr;
		if (owner == nullptr) {
			const auto* ai = pd2r::skills::MonsterAiOwner(unit);
			if (ai != nullptr && At<int32_t>(ai, pd2r::skills::AiOwnerType) == UnitMonster) {
				const uint32_t aiId = At<uint32_t>(ai, pd2r::skills::AiOwnerId);
				if (pd2r::skills::ValidOwnerId(aiId)) {
					owner = find(game, UnitMonster, aiId);
				}
			}
		}
		unit = owner;
	}
	return false;
}

// PD2's list step (event 15, step 7) on one unit: below 3 stacks the hit adds to the old list's damage and a stack; at 3
// it only refreshes the time. `damage` is the hit's (the list holds it negative); returns the list, `stacks` the count.
auto ApplyWounds(Game* game, Unit* unit, Unit* target, int32_t damage, int32_t* stacks) noexcept -> void* {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto listStat = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(ListStatValue);
	const auto setStat = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	void* old = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(target, StateOpenWounds);
	*stacks = 1;
	if (old != nullptr) {
		*stacks = listStat(dataFlag, old, StatOpenWoundsStack, 0);
		if (*stacks < MaxWoundStacks) {
			++*stacks;
			damage -= listStat(dataFlag, old, StatHpRegen, 0);  // the older damage, negative
			setStat(dataFlag, old, StatHpRegen, -damage, 0);
		}
	}
	d2r::StateListRequest request {unit, target, 0, 1, OpenWoundsFrames, static_cast<int32_t>(StatHpRegen), -damage,
	                               StateOpenWounds, nullptr};
	void* list = d2r::Engine<void* (*)(d2r::StateListRequest*) noexcept>(NewStateList)(&request);
	if (list != nullptr) {
		setStat(dataFlag, list, StatOpenWoundsStack, *stacks, 0);
	}
	return list;
}

// PD2's open wounds on a hit, one roll per event node of the stat (PD2's dispatcher runs each node):
// - chance: the node's stat on the attacker, % 100 on its seed;
// - damage per frame (1/256 life): the level table + 5 x deep_wounds. On a player, the level part times 8 / 6 / 4%
//   by difficulty; deep_wounds the same (half again from a Blood Golem), the Deep Wounds passive's part a third of
//   that;
// - less the target's damageresist after the attacker's passive_phys_pierce (a negative result counts half; 100 or
//   more ignores the pierce): a player loses at most 50%. A monster a player owns takes a quarter;
// - on one of Rathma's clones with both boss slots set, the damage is halved and the other clone,
//   unless it is dying or dead or has resistall (state 8), gets the same half on its own list (#645; the lookup is common/rathma_pair.h,
//   shared with core-events' rathma_life.cpp);
// - hpregen on the target's open wounds list (125 frames). Below 3 stacks the hit adds to the list's damage and a
//   stack; at 3 it only refreshes the time (NewStateList keeps an older list of the same skill and level).
// shortcut: PD2 keys the list by attacker (0x102fc880), so two attackers stack apart; D2R keeps one open wounds list
// per unit, which all attackers share. Not ported: no damage to a player inside PD2's PvP maps, cut (not in
// the merged data) and PD2's damage meter.
auto OpenWounds(Game* game, Unit* unit, Unit* target, uint32_t stat) noexcept -> void {
	using namespace d2r::rva;
	const int32_t chance = Stat(unit, stat);
	if (chance <= 0) {
		return;
	}
	const int32_t roll = static_cast<int32_t>(d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit), 100));
	if (roll >= chance) {
		return;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto listStat = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(ListStatValue);
	const auto stateList = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList);
	const bool player = At<int32_t>(target, UnitType) == 0;
	int32_t base = WoundsByLevel(std::max(Stat(unit, StatLevel), 1));
	int32_t deep = Stat(unit, StatDeepWounds);
	if (player) {
		const uint8_t difficulty = std::min<uint8_t>(At<uint8_t>(game, GameDifficulty), 2);
		const double percent = PlayerWoundPercent[difficulty];
		base = Percent(base, percent);
		const double deepPercent =
			At<int32_t>(unit, UnitType) == 1 && At<int32_t>(unit, UnitClassId) == BloodGolem ? percent * 0.5 : percent;
		void* passive = stateList(unit, StateDeepWounds);
		const int32_t fromPassive = passive != nullptr ? listStat(dataFlag, passive, StatDeepWounds, 0) : 0;
		deep = passive != nullptr ? Percent(deep - fromPassive, deepPercent) + Percent(fromPassive, deepPercent / 3.0)
		                          : Percent(deep, deepPercent);
	}
	int32_t damage = base + deep * 5;
	const int32_t resist = Stat(target, StatDamageResist);
	int32_t r = resist < 100 ? resist - Stat(unit, StatPhysPierce) : 100;
	if (r < 0) {
		r /= 2;
	}
	damage = player && r > 50 ? Percent(damage, 50.0) : Percent(damage, 100.0 - r);
	if (!player && OwnedByPlayer(game, target)) {
		damage /= 4;
	}
	Unit* other = pd2::rathma::Other(game, target);
	if (other != nullptr) {
		damage = pd2::rathma::Half(damage);
	}
	int32_t stacks = 0;
	ApplyWounds(game, unit, target, damage, &stacks);
	if (other != nullptr) {
		const int32_t mode = At<int32_t>(other, UnitMode);
		const bool otherList = mode != 0 && mode != 12 &&
			d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(other, StateResistAll) == 0;
		int32_t otherStacks = 0;
		if (otherList) {
			ApplyWounds(game, unit, other, damage, &otherStacks);
		}
	}
	++g_wounds;
}

}  // namespace itemrules
