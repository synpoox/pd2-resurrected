// PD2's changes to the mercenary AI (#1803): monai row 61 Hireable, every hireling. ProjectDiablo.dll writes its own
// think (0x1035a270) into the AI table; it runs a few rules, then jumps to 1.13c's Hireable think (D2Game +0x1dd40),
// which its patch table patches in eight places. D2R's live think (0x5beac0, AI table row 61) is the same code as
// 1.13c's (melee vs ranged from monstats aip1), so each 1.13c site has one D2R site. Ported here:
// - PD2's pre-think Battle Orders (0x10356bc0): a merc with Battle Orders (hireling ids 25/27/29) casts it whenever it or
//   its owner lacks the BO state and the owner is within 30 subtiles, before the rest of the think. An inline hook on
//   D2R's think entry: no SDK service runs inside an AI think.
// - PD2's attack call (1.13c +0x1e05d -> 0x10360fd0): before the stock attack (D2R 0x5bf6e0), an act5hire2 merc with
//   MonWhirlwind whirls at a target 6 to 19 subtiles away, and an act4hire merc with A4 AmpDmg curses a target without
//   amplifydamage on 25% of attacks. The think's one call is redirected (RedirectCall).
// - 1.13c +0x1e000 jge -> jmp: no 6/128 (12/128 ranged) roll that skips the target search in the think's 0x40-collision
//   branch. D2R's jc at 0x5bee23 becomes a 2-byte nop (MutationService).
// Not ported: its aura restart and its 5% party and pet buffing (#1925), and the follow
// rework (1.13c +0x1deb3, +0x1ded6, +0x1defb, +0x1e0eb, +0x1e11c; #1926), whose replacement is not in the think.
// PD2 keys its rules on its own hireling ids and skill ids; ours are renumbered, so these key on the merc's skills and
// monstats class. A merc without the skill (a Classic hireling row, another merc type) keeps D2R's think.
#include "merc_ai.h"

#include "hot_log.h"
#include "item_rules.h"
#include "log.h"
#include "monster_count.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// files/data: skills.txt ids, hireling.txt modes of those skills, monstats classes (PD2: skill 425, classes 0x231, 0x420)
constexpr int32_t SkillBattleOrders = 149, SkillMonWhirlwind = 495, SkillA4AmpDmg = 621;
constexpr uint8_t ModeBattleOrders = 5, ModeWhirlwind = 14, ModeA4AmpDmg = 7;
constexpr int32_t ClassAct5Hire2 = 561, ClassAct4Hire = 1073;
constexpr int32_t StatLevel = 12, StateAmplifyDamage = 9;
constexpr int32_t BuffRange = 30;                   // PD2: owner closer than 0x1e
constexpr int32_t WhirlMin = 6, WhirlMax = 19;       // PD2: distance - 6 <= 0xd
constexpr uint32_t AmpChance = 25;                   // PD2: rand() % 100 < 0x19
constexpr uint32_t SkInTown = 0x100;                 // skills record +0x24: +0x25 bit 0

using ThinkFn = void (*)(Game*, Unit* merc, uint8_t* tick) noexcept;
using OwnerFn = Unit* (*)(Unit* merc) noexcept;
// (game, merc, class, owner, target, seed, tick, aip block): D2R's attack, 0x5bf6e0
using AttackFn = void (*)(Game*, Unit* merc, int32_t cls, Unit* owner, Unit* target, uint32_t* seed, uint8_t* tick,
                          int16_t* aip) noexcept;
using UseSkillFn = int32_t (*)(Game*, Unit*, uint8_t mode, int32_t skill, Unit* target, int32_t x, int32_t y, uint8_t) noexcept;
using HasStateFn = int32_t (*)(Unit*, int32_t state) noexcept;
using DistanceFn = int32_t (*)(Unit*, Unit*) noexcept;
using UnitSkillFn = void* (*)(Unit*, int32_t skill, uint8_t) noexcept;

ThinkFn g_thinkOriginal = nullptr;
AttackFn g_attackOriginal = nullptr;
OwnerFn g_owner = nullptr;

std::atomic<uint32_t> g_thinks {0}, g_battleOrders {0}, g_whirlwinds {0}, g_amps {0}, g_attacks {0};

auto UseSkill(Game* game, Unit* merc, uint8_t mode, int32_t skill, Unit* target) noexcept -> bool {
	return d2r::Engine<UseSkillFn>(d2r::sig::AiUseSkill)(game, merc, mode, skill, target, 0, 0, 0) != 0;
}
auto HasState(Unit* unit, int32_t state) noexcept -> bool {
	return d2r::Engine<HasStateFn>(d2r::sig::UnitHasState)(unit, state) != 0;
}
auto Distance(Unit* a, Unit* b) noexcept -> int32_t {
	return d2r::Engine<DistanceFn>(d2r::sig::UnitDistance)(a, b);
}
auto HasSkill(Unit* unit, int32_t skill) noexcept -> bool {
	return d2r::Engine<UnitSkillFn>(d2r::sig::GetUnitSkill)(unit, skill, 0) != nullptr;
}

// PD2's 0x10356bc0 for its owner and itself, after its room guard (0x1037f6b0): no cast without a room, nor in town
// unless the skill has InTown (skills record +0x24 bit 0x100).
auto BattleOrders(Game* game, Unit* merc, Unit* owner, int32_t* distance) noexcept -> bool {
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::UnitRoom)(merc);
	if (room == nullptr || owner == nullptr || At<int32_t>(owner, d2r::rva::UnitType) != 0 || !HasSkill(merc, SkillBattleOrders)) {
		return false;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), SkillBattleOrders);
	if (skill == nullptr || d2r::api.unitStat(merc, StatLevel, 0) < At<uint16_t>(skill, d2r::rva::SkReqLevel)) {
		return false;
	}
	if ((At<uint32_t>(skill, d2r::rva::SkFlags24) & SkInTown) == 0 &&
	    pd2::monstercount::IsTown(d2r::Engine<int32_t (*)(void*) noexcept>(d2r::sig::RoomLevelId)(room))) {
		return false;
	}
	const int32_t state = At<int16_t>(skill, d2r::rva::SkAuraState);
	if (state <= 0 || (HasState(merc, state) && HasState(owner, state))) {
		return false;
	}
	*distance = Distance(merc, owner);
	return *distance < BuffRange && UseSkill(game, merc, ModeBattleOrders, SkillBattleOrders, nullptr);
}

auto MercThink(Game* game, Unit* merc, uint8_t* tick) noexcept -> void {
	const uint32_t n = ++g_thinks;
	Unit* owner = merc != nullptr ? g_owner(merc) : nullptr;
	int32_t distance = -1;
	const bool cast = owner != nullptr && BattleOrders(game, merc, owner, &distance);
	if (cast) {
		++g_battleOrders;
	}
	if (pd2::HotSample(n)) {
		plog::Info("merc think %u: class %d, owner %s, %s", n, merc != nullptr ? At<int32_t>(merc, d2r::rva::UnitClassId) : -1,
		           owner != nullptr ? "yes" : "no", cast ? "PD2 Battle Orders cast" : "D2R think");
	}
	if (!cast) {
		g_thinkOriginal(game, merc, tick);
	}
}

// PD2's 0x10360fd0 (PD2.gpr 0x10360f60): it returns once it issued the skill, whatever the skill command did.
auto MercAttack(Game* game, Unit* merc, int32_t cls, Unit* owner, Unit* target, uint32_t* seed, uint8_t* tick,
                int16_t* aip) noexcept -> void {
	const uint32_t n = ++g_attacks;
	const char* rule = nullptr;
	if (target != nullptr && merc != nullptr) {
		if (cls == ClassAct5Hire2 && HasSkill(merc, SkillMonWhirlwind)) {
			*reinterpret_cast<Unit**>(tick + d2r::rva::AiTickTarget) = target;
			const int32_t d = Distance(merc, target);
			if (d >= WhirlMin && d <= WhirlMax) {
				UseSkill(game, merc, ModeWhirlwind, SkillMonWhirlwind, target);
				++g_whirlwinds;
				rule = "PD2 Whirlwind";
			}
		} else if (cls == ClassAct4Hire && HasSkill(merc, SkillA4AmpDmg)) {
			*reinterpret_cast<Unit**>(tick + d2r::rva::AiTickTarget) = target;
			// shortcut: PD2 rolls the CRT's rand(); the merc's seed gives the same odds
			if (!HasState(target, StateAmplifyDamage) && seed != nullptr && d2r::RandomBelow(seed, 100) < AmpChance) {
				UseSkill(game, merc, ModeA4AmpDmg, SkillA4AmpDmg, target);
				++g_amps;
				rule = "PD2 A4 AmpDmg";
			}
		}
	}
	if (pd2::HotSample(n)) {
		plog::Info("merc attack %u: class %d, %s", n, cls, rule != nullptr ? rule : "D2R attack");
	}
	if (rule == nullptr) {
		g_attackOriginal(game, merc, cls, owner, target, seed, tick, aip);
	}
}

// push rbp / push rdi / push r15 / sub rsp,0x80 / mov rdi,rdx
constexpr uint8_t ThinkEntry[] = {0x40, 0x55, 0x57, 0x41, 0x57, 0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xfa};
constexpr const char* ThinkPattern =
	"40 55 57 41 57 48 81 EC 80 00 00 00 48 8B FA 4D 8B F8 48 8B E9 48 8D 15 ?? ?? ?? ?? 48 8B CF 41 B8 EB 0D 00 00 E8";
// The think's owner lookup (E8 at +11)
constexpr const char* OwnerCallPattern = "66 44 89 74 24 50 66 89 44 24 56 E8 ?? ?? ?? ?? 48 8B F0 48 85 C0 74 0C 48 8B C8 E8";
// The think's attack call (E8 at +13)
constexpr const char* AttackCallPattern =
	"4C 89 64 24 28 48 8B CD 48 89 44 24 20 E8 ?? ?? ?? ?? E9 ?? ?? ?? ?? 45 85 F6 74 13 41 B0 05";
// The skip roll: `and ecx,0x7f ... cmp ecx,edx / jc <idle>` (jc at +10)
constexpr const char* SearchRollPattern = "83 E1 7F 41 89 44 24 04 3B CA 72 74 EB 03 44 8B F3";
constexpr uint8_t SearchRollStock[] = {0x72, 0x74}, SearchRollNop[] = {0x66, 0x90};

}  // namespace

sigscan::Entry* const MercAiUses[9] = {&d2r::sig::AiUseSkill,  &d2r::sig::UnitHasState, &d2r::sig::UnitDistance,
                                       &d2r::sig::GetUnitSkill, &d2r::sig::UnitStat,     &d2r::sig::GetSkillRecord,
                                       &d2r::sig::RecordOffsets, &d2r::sig::UnitRoom,   &d2r::sig::RoomLevelId};

auto InstallMercAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	const uint64_t base = context->exeBase;
	uint64_t ownerSite = 0, roll = 0;
	bool hooked = false;
	if (sigscan::ScanUnique(base, OwnerCallPattern, 11, &ownerSite) == 1) {
		int32_t rel = 0;
		std::memcpy(&rel, reinterpret_cast<const void*>(base + ownerSite + 1), sizeof(rel));
		g_owner = reinterpret_cast<OwnerFn>(base + ownerSite + 5 + static_cast<int64_t>(rel));
		hooked = sigscan::HookBySignature(context, "MercThink", ThinkPattern, 0, ThinkEntry, sizeof(ThinkEntry), &MercThink,
		                                  &g_thinkOriginal);
	} else {
		plog::Warn("merc ai: skipped the owner call: not one match");
	}
	const bool redirected = RedirectCall(context, AttackCallPattern, 13, reinterpret_cast<uint64_t>(&MercAttack),
	                                     reinterpret_cast<uint64_t*>(&g_attackOriginal));
	const bool patched = sigscan::ScanUnique(base, SearchRollPattern, 10, &roll) == 1 &&
	                     context->PatchBytes(roll, SearchRollStock, sizeof(SearchRollStock), SearchRollNop, sizeof(SearchRollNop));
	plog::Info("merc ai: think hook %s, attack call %s, search roll at 0x%llx %s", hooked ? "on" : "off",
	           redirected ? "on" : "off", static_cast<unsigned long long>(roll), patched ? "on" : "off");
	std::snprintf(report, reportSize, "battle orders%s; whirlwind and amp%s; target search%s", hooked ? "" : " FAILED",
	              redirected ? "" : " FAILED", patched ? "" : " FAILED");
	return (hooked ? 1 : 0) + (redirected ? 1 : 0) + (patched ? 1 : 0);
}

auto MercAiCount() noexcept -> int {
	return 3;
}

auto MercAiCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "merc thinks %u: battle orders %u; attacks %u: whirlwind %u, amp %u", g_thinks.load(),
	              g_battleOrders.load(), g_attacks.load(), g_whirlwinds.load(), g_amps.load());
}
