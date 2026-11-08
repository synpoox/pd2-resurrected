// PD2's Summoner AI (ProjectDiablo.dll 0x10358510; docs/research/pd2-uber-bosses-dll.md, section 5) for rathmaBone,
// rathmaPoison and their two clones: the life events every 10% (stat 446 the event index, life held at each mark),
// the retreat below 15% through a ghostly portal to the next level, the immunity rules, the stage-6 RathmaDeath buff
// and the attack picks by the aip numbers. Every other Summoner monster keeps D2R's think.
//
// The four register on monai row 53 through ai_hooks (no SDK service runs monster AI; see ai_hooks.cpp). PD2 keeps
// the fight's stage in game +0x262a and each boss's state, sub-step and last state in its AI control (+0x14, +0x18,
// +0x1c); the port keeps them in D2R's per-game counters (rva::GameCounter), the AI's by unit id, as two of a class
// can live at once (one per level). PD2 reads its numbers from the monstats record as one int16 list (aip1, aip1(N),
// aip1(H), aip2, ...) whatever the difficulty, and the Hell aidel; so does the port.
#include "rathma_ai.h"

#include "ai_hooks.h"
#include "log.h"
#include "map_open.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <iterator>
#include <utility>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// Merged monstats ids (PD2 933-939, 966, 968, 969, 1106, 1107)
constexpr int32_t RathmaBone = 950, RathmaPoison = 951, BoneClone = 952, PoisonClone = 953;
constexpr int32_t VoidGolem = 954, BloodGolem = 955, Totem = 956, SpireFire = 983;
constexpr int32_t Knights[] = {985, 986, 1123, 1124};
// Killed when an original retreats (PD2 checks mode 12 too: the dead ones stay)
constexpr int32_t RetreatKills[] = {BloodGolem, VoidGolem, Totem, SpireFire, 985, 986, 1123, 1124};
constexpr int32_t StatEvent = 499, StatPick = 500, StatBias = 501;  // mon_cooldown1-3 (PD2 446-448)
constexpr int32_t StateMonFrenzy = 103;
constexpr int32_t RathmaDeath = 554;     // the skill (PD2 485)
constexpr int32_t GhostlyPortal = 588;   // xactportal_ghostly (PD2 585)
constexpr int32_t ImmuneStats[] = {39, 43, 41, 45, 37, 36};  // fire, cold, lightning, poison, magic, damage resist
constexpr int32_t SummonerRow = 53;      // monai.txt
constexpr uint64_t AiSummoner = 0x5b4d50;  // row 53's think, read from rva::AiTable

constexpr uint32_t TierKey = 0x55425254;   // 'UBRT', as map_open.cpp
constexpr uint32_t StageKey = 0x55425247;  // 'UBRG': PD2 game +0x262a
constexpr uint32_t BossKey = 0x55425230;   // 'UBR0' / 'UBR1': the unit ids PD2 keeps at game +0x2600 / +0x2604
// Per-unit counters: the field in bits 20-23, the unit id below
enum Field : uint32_t { Next, Sub, Last, Immune, Resist0 };
constexpr auto UnitKey(uint32_t field, uint32_t id) noexcept -> uint32_t {
	return 0x52000000u | field << 20 | (id & 0xfffffu);
}
static_assert(UnitKey(Resist0 + 5, 0xfffff) == 0x529fffff);

std::atomic<uint32_t> g_thinks {0};
std::atomic<uint32_t> g_events {0};
std::atomic<uint32_t> g_stages {0};
std::atomic<uint32_t> g_portals {0};
std::atomic<uint32_t> g_casts {0};
std::atomic<uint32_t> g_buffs {0};
std::atomic<uint32_t> g_survivorCasts {0};

auto Counter(Game* game, uint32_t key) noexcept -> int32_t& {
	return *d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

auto Stat(Unit* unit, int32_t stat) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(unit, stat, 0);
}

auto SetStat(Unit* unit, int32_t stat, int32_t value) noexcept -> void {
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::rva::SetUnitStat)(unit, stat, value, 0);
}

auto Rand(Unit* unit, uint32_t n) noexcept -> int32_t {
	return static_cast<int32_t>(
		d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(d2r::rva::UnitSeed)(unit), n));
}

auto Dead(Unit* unit) noexcept -> bool {
	return d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::UnitDead)(unit) != 0;
}

auto LevelOf(Unit* unit) noexcept -> int32_t {
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(d2r::rva::UnitRoom)(unit);
	return room != nullptr ? d2r::Engine<int32_t (*)(void*) noexcept>(d2r::rva::RoomLevelId)(room) : -1;
}

auto IsIn(int32_t classId, const int32_t* first, const int32_t* last) noexcept -> bool {
	return std::find(first, last, classId) != last;
}

// Up to `max` live monsters of the listed classes, in `level` (or anywhere for -1). PD2 walks only the head of each
// of the game's 128 monster lists; the port walks them whole.
template <size_t N>
auto FindMonsters(Game* game, const int32_t (&classes)[N], int32_t level, Unit** out, int max) noexcept -> int {
	using namespace d2r::rva;
	int n = 0;
	for (int bucket = 0; bucket < 128 && n < max; ++bucket) {
		for (auto* u = At<Unit*>(game, GameMonsters + bucket * 8); u != nullptr && n < max;
		     u = At<Unit*>(u, UnitListNext)) {
			if (IsIn(At<int32_t>(u, UnitClassId), std::begin(classes), std::end(classes)) && !Dead(u) &&
			    (level < 0 || LevelOf(u) == level)) {
				out[n++] = u;
			}
		}
	}
	return n;
}

auto Idle(Game* game, Unit* unit, int32_t frames) noexcept -> void {
	d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(d2r::rva::AiIdle)(game, unit, frames);
}
auto Delay(Game* game, Unit* unit, int32_t frames) noexcept -> void {
	d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(d2r::rva::AiSetDelay)(game, unit, frames);
}
// PD2 0x103a1880: (skill, target, x, y, mode)
auto Cast(Game* game, Unit* unit, int32_t skill, int32_t mode, Unit* target, int32_t x, int32_t y) noexcept -> void {
	if (skill < 0) {
		Idle(game, unit, 10);
		return;
	}
	const int32_t ok = d2r::Engine<int32_t (*)(Game*, Unit*, uint8_t, int32_t, Unit*, int32_t, int32_t, uint8_t) noexcept>(
		d2r::rva::AiUseSkill)(game, unit, static_cast<uint8_t>(mode), skill, target, x, y, 0);
	if (plog::Sample(g_casts)) {
		plog::Info("rathma ai: class %d cast skill %d mode %d at (%d, %d)%s -> %d (%u so far)",
		           At<int32_t>(unit, d2r::rva::UnitClassId), skill, mode, x, y, target != nullptr ? " on target" : "", ok,
		           g_casts.load());
	}
}

// As lucion.cpp's KillQuietly: no drop, then the death mode
auto KillQuietly(Game* game, Unit* monster) noexcept -> void {
	using namespace d2r::rva;
	d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(SetUnitFlags)(monster, UnitNoDrop, 1);
	alignas(8) uint8_t block[0x30] = {};
	d2r::Engine<void (*)(Unit*, int32_t, void*) noexcept>(MonsterModeBlock)(monster, ModeDeath, block);
	*reinterpret_cast<void**>(block + 0x10) = d2r::Engine<void* (*)(Game*, Unit*) noexcept>(UnitTarget)(game, monster);
	d2r::Engine<uint64_t (*)(Game*, void*, int32_t) noexcept>(SetMonsterMode)(game, block, 1);
}

// PD2 0x10380770: on sets the six resists to 200, removes the poison and open wounds lists and, with `setState`,
// state 8; off puts back the saved resists (PD2 reads monstats' Normal columns) and clears state 8
auto SetImmune(Game* game, Unit* unit, bool on, bool setState) noexcept -> void {
	using namespace d2r::rva;
	const uint32_t id = At<uint32_t>(unit, UnitIdField);
	int32_t& immune = Counter(game, UnitKey(Immune, id));
	if (on) {
		if (immune == 0) {
			for (size_t i = 0; i < std::size(ImmuneStats); ++i) {
				Counter(game, UnitKey(Resist0 + static_cast<uint32_t>(i), id)) = Stat(unit, ImmuneStats[i]);
			}
			immune = 1;
		}
		for (const int32_t s : ImmuneStats) {
			SetStat(unit, s, 200);
		}
		ImmunityStates(game, unit, true, setState);
	} else if (immune != 0) {
		for (size_t i = 0; i < std::size(ImmuneStats); ++i) {
			SetStat(unit, ImmuneStats[i], Counter(game, UnitKey(Resist0 + static_cast<uint32_t>(i), id)));
		}
		ImmunityStates(game, unit, false, setState);
		immune = 0;
	}
}

// PD2 0x1036d7d0 with the skill's auratargetstate, auralencalc and aurastats: the timed state `owner` puts on `unit`
// (RathmaDeath's monfrenzy and masteries on the survivor, RathmaPrison's disable_tele on the target)
auto ApplyAuraState(Game* game, Unit* owner, Unit* unit, int32_t skillId, int32_t level) noexcept -> bool {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	const int32_t state = skill != nullptr ? At<int16_t>(skill, SkAuraTargetState) : 0;
	if (state <= 0 || unit == nullptr) {
		return false;
	}
	const int32_t frames = d2r::Engine<int32_t (*)(uint32_t, Unit*, int32_t, int32_t, int32_t) noexcept>(EvalSkillCalc)(
		dataFlag, owner, At<int32_t>(skill, SkAuraLenCalc), skillId, level);
	d2r::StateListRequest request {owner, unit, skillId, level, frames > 0 ? frames : 1, -1, 0, state,
	                               d2r::Engine<void (*)()>(SkillStateExpired)};
	void* list = d2r::Engine<void* (*)(d2r::StateListRequest*) noexcept>(NewStateList)(&request);
	if (list == nullptr) {
		return false;
	}
	// shortcut: PD2's helper (0x1036d7d0) ORs 8 into the list's flags in expansion games (game+0x10 & 2) and 0x800 for
	// a state with states.txt `exp` (D2Common #10226; blank for monfrenzy and disable_tele). D2R's NewStateList's own
	// flags stand, as for the uber scaling and map stat lists
	d2r::Engine<void (*)(Unit*, void*, const uint8_t*, int32_t, int32_t) noexcept>(AddAuraStats)(unit, list, skill,
	                                                                                           skillId, level);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateGfxFlag)(unit, state, 1);
	return true;
}

struct Mon {
	const uint8_t* rec;
	// PD2's 1.13c offsets: skills at +0x170, modes at +0x180, levels at +0x198, aip list at +0x56, Hell aidel +0x51
	auto Skill(int slot) const noexcept -> int32_t { return At<int16_t>(rec, d2r::rva::MonSkills + 2 * (slot - 1)); }
	auto Mode(int slot) const noexcept -> int32_t { return At<uint8_t>(rec, d2r::rva::MonSkillModes + slot - 1); }
	// The n-th int16 of the aip list (0 aip1, 1 aip1(N), 2 aip1(H), 3 aip2, ...)
	auto P(int n) const noexcept -> int32_t { return At<int16_t>(rec, d2r::rva::MonAiParams + 2 * n); }
	auto AiDel() const noexcept -> int32_t { return At<uint8_t>(rec, d2r::rva::MonAiDel + 2); }
};

auto LifePercent(Unit* unit) noexcept -> int32_t {
	const int32_t max = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::UnitMaxLife)(unit);
	return max > 0 ? static_cast<int32_t>(static_cast<int64_t>(Stat(unit, d2r::rva::StatLife)) * 100 / max) : 100;
}

// PD2 0x1037c4c0: life set to `percent` of max
auto HoldLife(Unit* unit, int32_t percent) noexcept -> void {
	const int64_t max = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::UnitMaxLife)(unit);
	SetStat(unit, d2r::rva::StatLife, static_cast<int32_t>(max * percent / 100));
}

// PD2 0x103567d0: the stage's spot, where Blink lands and the retreat portal opens
constexpr auto Spot(int32_t stage) noexcept -> std::pair<int32_t, int32_t> {
	return stage <= 2 ? std::pair {0x476c, 0x35bf} : stage <= 4 ? std::pair {0x47d3, 0x394b} : std::pair {0x4e9a, 0x1403};
}
static_assert(Spot(1).first == 0x476c && Spot(4).second == 0x394b && Spot(6).first == 0x4e9a);

// The life events (bone set, poison set): below `life`, with stat 446 under `event`. `odd` events are the healing
// waves: life is held at the mark; the poison set's are rathmaPoison's alone.
struct Event {
	int32_t life, event;
};
constexpr Event Events[] = {{90, 1}, {80, 2}, {70, 3}, {60, 4}, {50, 5}, {40, 6}, {30, 7}};
static_assert(std::size(Events) == 7 && Events[6].life == 30);

struct Tick {
	Game* game;
	Unit* unit;
	uint8_t* tick;
	Mon m;
	int32_t classId;
	bool bone;  // rathmaBone or its clone; else rathmaPoison or its clone
	int32_t stage, delay, life, event, bias;
	Unit* target;
	int32_t far;  // the farthest player in the level within 0x5dc (squared)
	bool knights, golem, immune;
	int32_t cur, sub, next, last;
};

auto TargetXY(const Tick& t, int32_t* x, int32_t* y) noexcept -> bool {
	*x = *y = 0;
	return t.target != nullptr && d2r::UnitXY(t.target, x, y);
}

// The cast at the stage's spot (+dx), states 6, 0x1f and 0x29
auto BlinkToSpot(Tick& t, int dx) noexcept -> void {
	const auto [x, y] = Spot(t.stage);
	Cast(t.game, t.unit, t.m.Skill(8), t.m.Mode(8), nullptr, x + dx, y);
}

// The retreat (state 6), for the originals: Blink to the spot and kill the adds; then the portal to the next level
// and the summon; then (PD2) walk into it; then gone
auto Retreat(Tick& t) noexcept -> void {
	t.next = 6;
	int32_t& stage = Counter(t.game, StageKey);
	if (t.sub == 0) {
		t.sub = 1;
		BlinkToSpot(t, 4);
		Unit* adds[32] = {};
		const int n = FindMonsters(t.game, RetreatKills, -1, adds, static_cast<int>(std::size(adds)));
		for (int i = 0; i < n; ++i) {
			KillQuietly(t.game, adds[i]);
		}
		plog::Info("rathma: class %d retreats at %d%% life (stage %d), %d adds killed", t.classId, t.life, stage, n);
	} else if (t.sub == 1) {
		t.sub = 2;
		const int32_t before = stage;
		int32_t level = 161;  // PD2 162
		if (stage == 1) {
			stage = 2;
		} else if (stage == 3) {
			stage = 4;
			level = 162;  // PD2 163
		}
		if (stage != before) {
			++g_stages;
			plog::Info("rathma: stage %d -> %d (retreat of class %d)", before, stage, t.classId);
		}
		const auto [x, y] = Spot(stage);
		// PD2 0x1038a060 / 0x1038a1f0 / 0x1038a510: a ghostly portal here linked to object 587 in the next level
		if (OpenPortalAt(t.game, d2r::Engine<void* (*)(Unit*) noexcept>(d2r::rva::UnitRoom)(t.unit), GhostlyPortal, x, y,
		                 level) != nullptr) {
			++g_portals;
			plog::Info("rathma: ghostly portal at (%d, %d) to level %d (%u so far)", x, y, level, g_portals.load());
		}
		SetStat(t.unit, StatPick, 0);
		Cast(t.game, t.unit, t.m.Skill(7), t.m.Mode(7), nullptr, x, y);
		Delay(t.game, t.unit, 25);
	} else if (t.sub == 2) {
		// shortcut: PD2 walks him to (x - 4, y) into the portal (D2Game 0xf2540); the port has no walk-to-point move
		// yet, so he waits there; add it when D2R's is found
		t.sub = 3;
		Delay(t.game, t.unit, 50);
	} else if (t.sub == 3) {
		// shortcut: PD2 removes him (0x1036d1e0); D2R's RemoveUnit frees the unit at once, unsafe inside its own
		// think, so he dies with no drop and no experience; upgrade to a deferred removal if the corpse matters
		*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(t.unit) + d2r::rva::UnitMainFlags) |=
			d2r::rva::UnitNoTreasure | d2r::rva::UnitNoExperience;
		KillQuietly(t.game, t.unit);
		plog::Info("rathma: class %d leaves through the portal", t.classId);
		t.next = 1;
		t.sub = 0;
	} else {
		t.sub = 0;
	}
}

// PD2's pick (state 1 and every unlisted state)
auto Pick(Tick& t) noexcept -> void {
	const Mon& m = t.m;
	if (t.far > m.P(0)) {
		if (t.bone) {
			if (t.last == 5 || t.last == 0xb || t.last == 0x2a) {
				if (!t.golem) {
					t.next = 5;
					t.sub = 0;
					Idle(t.game, t.unit, t.delay);
					return;
				}
			} else if (!t.golem) {
				t.next = 0x2a;
				t.sub = 1;
				Idle(t.game, t.unit, t.delay);
				return;
			}
		} else if (!t.golem) {
			t.next = 5;
			t.sub = 0;
			Idle(t.game, t.unit, t.delay);
			return;
		}
	}
	t.sub = 0;
	const int32_t r1 = Rand(t.unit, 100), r2 = Rand(t.unit, 100);
	const int32_t bias = m.P(2) + t.bias;  // aip1(H) + stat 448
	if (r1 < bias && !t.golem) {
		// PD2 divides aip2(N) and aip2(N) + aip2(H) by the bias
		if (r1 < m.P(4) / bias) {
			t.next = 2;
		} else {
			t.next = r1 >= (m.P(5) + m.P(4)) / bias ? 5 : 3;
		}
		Idle(t.game, t.unit, t.delay);
		return;
	}
	SetStat(t.unit, StatBias, t.golem ? 100 : m.P(3) + t.bias);
	const int32_t a = m.P(6), b = a + m.P(7), c = b + m.P(8);  // aip3, + aip3(N), + aip3(H)
	if (!t.bone) {
		t.next = r2 < a ? 0xc : r2 < b ? 0xb : r2 < c && !t.golem ? 0xe : 0xa;
	} else if (r2 < a && t.last != 0x2a) {
		t.next = 0x2a;
	} else if ((r2 < c && t.event < 2) || r2 < b) {
		t.next = 0xe;
	} else if (r2 < c) {
		t.next = 0x20;
	} else if (r2 < c + m.P(9)) {
		t.next = 0xb;
	} else if (r2 < c + m.P(9) + m.P(10)) {
		t.next = 0x28;
	} else {
		t.next = 0xa;
	}
	Idle(t.game, t.unit, t.delay);
}

auto Run(Tick& t) noexcept -> void {
	const Mon& m = t.m;
	Unit* target = t.target;
	int32_t tx = 0, ty = 0;
	TargetXY(t, &tx, &ty);
	// Blink near the target: +-8, or +-16 below half life
	const auto blinkNear = [&]() {
		const int32_t r = t.life < 50 ? 16 : 8;
		Cast(t.game, t.unit, m.Skill(8), m.Mode(8), nullptr, tx - r + Rand(t.unit, 2 * r), ty - r + Rand(t.unit, 2 * r));
	};
	const auto castAt = [&](int slot) { Cast(t.game, t.unit, m.Skill(slot), m.Mode(slot), target, 0, 0); };
	switch (t.cur) {
	case 0:
		SetImmune(t.game, t.unit, true, true);
		Delay(t.game, t.unit, 62);
		t.next = 1;
		t.sub = 0;
		return;
	case 2:
		Idle(t.game, t.unit, t.delay / 2);
		SetStat(t.unit, StatBias, 0);
		t.sub = 0;
		return;
	case 3:
		// shortcut: PD2 walks to a random spot within 5-7 of himself (D2Game 0xf1b10); the port has no walk-to-point
		// move yet, so he idles; add it when D2R's is found
		Idle(t.game, t.unit, t.delay);
		SetStat(t.unit, StatBias, 0);
		t.sub = 0;
		return;
	case 4:
		VanillaAiThink(SummonerRow)(t.game, t.unit, t.tick);  // PD2 0x103188b0: D2Game's Summoner think
		t.sub = 0;
		return;
	case 5:
		if (target == nullptr) {
			Idle(t.game, t.unit, t.delay);
		} else if (!t.golem) {
			blinkNear();
			if (t.sub == 1 && t.bone) {
				t.next = 0xb;
				Delay(t.game, t.unit, 5);
			}
			SetStat(t.unit, StatBias, 0);
		} else {
			castAt(1);
		}
		t.sub = 0;
		return;
	case 6:
		if (t.classId == RathmaBone || t.classId == RathmaPoison) {
			t.next = 6;
			Retreat(t);
		} else {
			t.sub = 0;
		}
		return;
	case 0xa: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf: case 0x10:
		castAt(t.cur == 0x10 ? 7 : t.cur - 9);
		t.sub = 0;
		return;
	case 0x11:
		if (!t.golem) {
			castAt(8);
		}
		t.sub = 0;
		return;
	case 0x1e:
	case 0x20:
	case 0x21:
		SetStat(t.unit, StatPick, t.cur);
		castAt(7);
		t.sub = 0;
		return;
	case 0x1f:
		t.next = 0x1f;
		if (t.sub == 0) {
			BlinkToSpot(t, 0);
			t.sub = 1;
		} else if (t.sub == 1) {
			SetStat(t.unit, StatPick, 0x1f);
			castAt(7);
			t.next = 0xa;
			t.sub = 0;
		} else {
			t.sub = 0;
		}
		return;
	case 0x28:
		// Blink, bone spear, Blink, bone spear, Blink, then a last spear and on to 5
		t.next = 0x28;
		if (t.sub == 0 || t.sub == 2 || t.sub == 4) {
			++t.sub;
			blinkNear();
			Delay(t.game, t.unit, 5);
		} else if (t.sub == 1 || t.sub == 3) {
			++t.sub;
			castAt(1);
		} else {
			if (t.sub == 5) {
				castAt(1);
				t.next = 5;
			}
			t.sub = 0;
		}
		SetStat(t.unit, StatBias, 0);
		return;
	case 0x29:
		t.next = 0x29;
		if (t.sub == 0) {
			BlinkToSpot(t, 0);
			t.sub = 1;
		} else if (t.sub == 1) {
			castAt(6);
			t.sub = 2;
		} else if (t.sub == 2) {
			castAt(3);
			t.sub = 3;
		} else if (t.sub <= 8) {
			++t.sub;
			const int32_t r = Rand(t.unit, 100);
			const int32_t a = m.P(6) + m.P(7);
			castAt(r < a ? 2 : r < a + m.P(8) ? 5 : 1);
			Delay(t.game, t.unit, 0x1c);
		} else {
			castAt(4);
			t.next = 5;
			t.sub = 0;
		}
		return;
	case 0x2a: {
		// shortcut: PD2 passes Sk3lvl (monstats +0x19a); the port passes 1, as the D2R record's offset is not read yet.
		// RathmaPrison's auralencalc (75) does not use the level; read the offset if another slot-3 skill ever does
		if (target != nullptr) {
			ApplyAuraState(t.game, t.unit, target, m.Skill(3), 1);
		}
		castAt(3);
		if (t.sub == 1) {
			t.next = 5;
		}
		t.sub = t.sub == 1 ? 1 : 0;
		return;
	}
	default:
		Pick(t);
		return;
	}
}

// The life events (PD2 0x10358f5f-0x103594d8). True when the event ran its own state; false to go on to the
// state's own run.
auto LifeEvent(Tick& t) noexcept -> bool {
	const bool original = t.classId == RathmaBone || t.classId == RathmaPoison;
	int32_t& stage = Counter(t.game, StageKey);
	for (const Event& e : Events) {
		if (t.life >= e.life || t.event >= e.event) {
			continue;
		}
		const bool wave = (e.event & 1) != 0;
		if (!t.bone && wave && t.classId != RathmaPoison) {
			continue;  // the poison set's odd marks are rathmaPoison's alone: the clone skips them
		}
		SetStat(t.unit, StatEvent, e.event);
		++g_events;
		if (t.bone) {
			if (e.event == 1 || e.event == 4 || e.event == 6) {
				plog::Info("rathma: class %d event %d at %d%% life: none", t.classId, e.event, t.life);
				return false;
			}
			t.cur = wave ? 0x21 : 0x20;
			if (wave) {
				HoldLife(t.unit, e.life);
				t.immune = true;
			}
			plog::Info("rathma: class %d event %d at %d%% life: slot 7 (skill %d)%s", t.classId, e.event, t.life,
			           t.m.Skill(7), wave ? ", life held" : "");
			SetImmune(t.game, t.unit, t.immune, true);
			Run(t);
			return true;
		}
		t.cur = wave ? 0x1f : 0x29;
		t.sub = 0;
		if (wave) {
			HoldLife(t.unit, e.life);
			t.immune = true;
		}
		plog::Info("rathma: class %d event %d at %d%% life: slot 8 to the spot, then %s", t.classId, e.event, t.life,
		           wave ? "slot 7 (life held)" : "the cast chain (slots 6, 3, then 2/5/1)");
		SetImmune(t.game, t.unit, t.immune, true);
		Run(t);
		return true;
	}
	if (t.event >= 8) {
		return false;
	}
	if (t.life < 15 && stage < 5) {
		// the retreat: life held at 15%, then state 6 (the clones only store it)
		SetStat(t.unit, StatEvent, 8);
		HoldLife(t.unit, 15);
		SetImmune(t.game, t.unit, true, true);
		t.next = 6;
		plog::Info("rathma: class %d event 8 at %d%% life: retreat (stage %d)", t.classId, t.life, stage);
		if (original) {
			Retreat(t);  // PD2 runs state 6's code here, whatever the state
		} else {
			t.sub = 0;
		}
		return true;
	}
	if (t.life < 20 && stage >= 5) {
		SetStat(t.unit, StatEvent, 8);
		plog::Info("rathma: class %d event 8 at %d%% life%s", t.classId, t.life, t.bone ? ": none" : ": the cast chain");
		if (t.bone) {
			return false;
		}
		t.cur = 0x29;
		t.sub = 0;
		SetImmune(t.game, t.unit, t.immune, true);
		Run(t);
		return true;
	}
	return false;
}

void Think(Game* game, Unit* unit, uint8_t* tick) noexcept {
	using namespace d2r::rva;
	Tick t {};
	t.m.rec = tick != nullptr ? At<const uint8_t*>(tick, AiTickMonStats) : nullptr;
	if (t.m.rec == nullptr) {
		VanillaAiThink(SummonerRow)(game, unit, tick);
		return;
	}
	t.game = game;
	t.unit = unit;
	t.tick = tick;
	t.classId = At<int32_t>(unit, UnitClassId);
	t.bone = t.classId == RathmaBone || t.classId == BoneClone;
	const uint32_t id = At<uint32_t>(unit, UnitIdField);
	Counter(game, BossKey + (t.classId == PoisonClone ? 1 : 0)) = static_cast<int32_t>(id);
	int32_t& stage = Counter(game, StageKey);
	if (stage == 0) {
		stage = 1;
		++g_stages;
		plog::Info("rathma: stage 0 -> 1 (class %d)", t.classId);
	}
	t.stage = stage;
	const int32_t tier = Counter(game, TierKey);
	t.delay = std::max(3, t.m.AiDel() * (stage < 6 ? 2 : 1) - 4 * tier + 1);
	t.life = LifePercent(unit);
	t.event = Stat(unit, StatEvent);
	t.bias = Stat(unit, StatBias);
	const int32_t stored = Counter(game, UnitKey(Next, id));
	t.cur = stored;
	t.sub = Counter(game, UnitKey(Sub, id));
	t.last = Counter(game, UnitKey(Last, id));
	t.next = 1;
	if (plog::Sample(g_thinks)) {
		plog::Info("rathma: class %d unit %u thinks (%u so far), state %d.%d, stage %d, life %d%%", t.classId, id,
		           g_thinks.load(), t.cur, t.sub, stage, t.life);
	}

	// The players in his level: the nearest (squared distance) and the farthest within 0x5dc
	const int32_t level = LevelOf(unit);
	int32_t ux = 0, uy = 0;
	d2r::UnitXY(unit, &ux, &uy);
	Unit* nearest = nullptr;
	int32_t nearD2 = 0x7fffffff;
	for (auto* client = At<uint8_t*>(game, GameClients); client != nullptr; client = At<uint8_t*>(client, ClientNext)) {
		Unit* p = d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(ClientUnit)(client, 0);
		int32_t px = 0, py = 0;
		if (p == nullptr || Dead(p) || LevelOf(p) != level || !d2r::UnitXY(p, &px, &py)) {
			continue;
		}
		const int32_t d2 = (px - ux) * (px - ux) + (py - uy) * (py - uy);
		if (d2 > t.far && d2 < 0x5dc) {
			t.far = d2;
		}
		if (d2 < nearD2) {
			nearD2 = d2;
			nearest = p;
		}
	}
	// The idle window for a boss left alone (PD2 game+0x26fe): idle and immune, counting down while a player is here
	if (IdleWindow(game, unit, nearest != nullptr)) {
		SetImmune(game, unit, true, true);
		Idle(game, unit, 10);
		return;
	}
	auto*& tickTarget = *reinterpret_cast<Unit**>(tick + AiTickTarget);
	if (tickTarget == nullptr) {
		if (nearest == nullptr) {
			Idle(game, unit, 0x19);
			return;
		}
		tickTarget = nearest;
	}

	Unit* found[1] = {};
	t.knights = FindMonsters(game, Knights, level, found, 1) > 0;
	constexpr int32_t Golems[] = {BloodGolem};
	t.golem = FindMonsters(game, Golems, level, found, 1) > 0;

	// From stage 6 the survivor keeps RathmaDeath's buff (monfrenzy and the masteries)
	if (stage > 5 && d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(unit, StateMonFrenzy) == 0 &&
	    ApplyAuraState(game, unit, unit, RathmaDeath, 1)) {
		++g_buffs;
		plog::Info("rathma: class %d takes RathmaDeath's buff (stage %d, %u so far)", t.classId, stage, g_buffs.load());
	}

	// Immune: retreating, the poison set in stages 1 and 5 while the blood golem lives or a cast chain runs, the bone
	// set (and both from stage 5) while a void knight lives, or no player within aip1
	if (t.life < 15 && (stage == 1 || stage == 3)) {
		t.golem = false;
		t.next = 6;
		t.cur = 6;
		if (stored != 6) {
			t.sub = 0;
		}
		t.immune = true;
	} else if (!t.bone && (stage == 1 || stage == 5)) {
		t.immune = t.golem || t.cur == 0x1f || (t.cur == 0x29 && t.sub < 3);
	} else if (t.bone || stage >= 5) {
		t.immune = t.knights || t.cur == 0x21;
	}
	if (nearD2 > t.m.P(0)) {
		if (nearest != nullptr) {
			tickTarget = nearest;
		}
		t.immune = true;
	}
	t.target = tickTarget;

	if (!LifeEvent(t)) {
		SetImmune(game, unit, t.immune, true);
		Run(t);
	}
	Counter(game, UnitKey(Next, id)) = t.next;
	Counter(game, UnitKey(Sub, id)) = t.sub;
	Counter(game, UnitKey(Last, id)) = t.cur;
}

}  // namespace

auto KillRathmaAdds(Game* game) noexcept -> int {
	// PD2 0x10376840's list: the void knights, both golems and the totem (not the spire, which the retreat kills)
	constexpr int32_t Adds[] = {BloodGolem, VoidGolem, Totem, 985, 986, 1123, 1124};
	Unit* adds[32] = {};
	const int n = FindMonsters(game, Adds, -1, adds, static_cast<int>(std::size(adds)));
	for (int i = 0; i < n; ++i) {
		KillQuietly(game, adds[i]);
	}
	return n;
}

auto BuffRathmaSurvivor(Game* game, Unit* dead) noexcept -> void {
	// PD2 0x10376840 at stage 5 -> 6: the surviving clone casts RathmaDeath (0x1036d7d0) at once
	const int32_t other = At<int32_t>(dead, d2r::rva::UnitClassId) == BoneClone ? PoisonClone : BoneClone;
	const int32_t clones[] = {other};
	Unit* survivor[1] = {};
	if (FindMonsters(game, clones, LevelOf(dead), survivor, 1) == 0 ||
	    !ApplyAuraState(game, survivor[0], survivor[0], RathmaDeath, 1)) {
		plog::Warn("rathma: no survivor %d took RathmaDeath's buff", other);
		return;
	}
	plog::Info("rathma: survivor %d casts RathmaDeath on the first clone's death (%u so far)", other,
	           ++g_survivorCasts);
}

auto InstallRathmaAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	bool ok = true;
	for (const int32_t c : {RathmaBone, RathmaPoison, BoneClone, PoisonClone}) {
		ok = ok && RegisterAiThink(context, SummonerRow, AiSummoner, c, &Think);
	}
	std::snprintf(report, reportSize,
	              ok ? "monai %d (Summoner) think: rathmaBone, rathmaPoison and the clones"
	                 : "off: monai %d think not swapped",
	              SummonerRow);
	return ok ? 1 : 0;
}

auto RathmaAiCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "rathma ai: %u thinks, %u life events, %u stages, %u portals, %u casts, %u AI buffs, "
	              "%u survivor casts",
	              g_thinks.load(), g_events.load(), g_stages.load(), g_portals.load(), g_casts.load(), g_buffs.load(),
	              g_survivorCasts.load());
}
