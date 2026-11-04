// Split Throw's return missiles: PD2 missile pSrvHitFunc 65 on the thrown rows, pSrvDoFunc 38 / pCltDoFunc 70 on the
// <name>return rows. D2R fills sdo 38 and cdo 70, so ours sit in sdo 46 / cdo 78 and the data points there.
//
// W6 adds Exploding Arrow's spread (shit 63) and Multiple Shot's pierce falloff (PD2 dmg 17, here 20), both read from
// ProjectDiablo.dll (docs/research/amazon-w6.md).
//
// The server calls missile srvdo as int64_t fn(Game*, Unit* missile) every frame, srvhit as
// void fn(Game*, Unit* missile, Unit* hit) on a collision, srvdmg as void fn(Game*, Unit* missile, Unit* target,
// uint8_t* damage) before a hit's damage is applied, and the client missile cltdo as void fn(Unit* missile, int32_t).
#include "missile_functions.h"

#include "log.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

std::atomic<uint32_t> g_returnSpawns {0};
std::atomic<uint32_t> g_returnsCaught {0};

constexpr int32_t ReturnSteerFrames = 2;  // re-aim every 2nd frame; Guided Arrow steers on its own param interval
constexpr int32_t ReturnCatchDistance = 2;  // subtiles from the thrower that count as caught

// Point the missile's path at `owner` and re-aim it every ReturnSteerFrames frames. Shared by both halves.
void SteerHome(Unit* missile, Unit* owner, int32_t framesLeft) noexcept {
	using namespace d2r::rva;
	uint8_t* path = At<uint8_t*>(missile, UnitPath);
	if (path == nullptr) {
		return;
	}
	if (At<Unit*>(path, PathTargetUnit) != owner) {
		d2r::Engine<void (*)(uint8_t*, Unit*) noexcept>(PathSetTargetUnit)(path, owner);
	}
	if (framesLeft % ReturnSteerFrames == 0) {
		d2r::Engine<void (*)(uint8_t*, Unit*, int32_t, int32_t) noexcept>(PathSteer)(path, missile, 0, 1);
	}
}

}  // namespace

// shit 65: on a collision, start HitSubMissile1-4 (the <name>return row) at the missile, as D2R's srvhit 4 does.
// shortcut: approximation from PD2's data, srvhit 4 runs on any collision (ground and walls too) and explodes the
// sub-missile when the parent row has an explosion; PD2's may differ. Upgrade when PD2's shit 65 is read from
// ProjectDiablo.dll.
void SrvHitSpawnReturn(Game* game, Unit* missile, Unit* hit) noexcept {
	if (game == nullptr || missile == nullptr) {
		return;
	}
	d2r::Engine<MissileSrvHitFn>(d2r::rva::SrvHitSubMissiles)(game, missile, hit);
	if (plog::Sample(g_returnSpawns)) {
		plog::Info("return missile: spawned on hit, hit %u", g_returnSpawns.load());
	}
}

// sdo 46 (PD2 sdo 38): fly back to the thrower and end on reaching them; otherwise the default missile frame.
// shortcut: approximation from PD2's data (Guided Arrow's steering, aimed at the owner); homing, PD2's catch distance
// and steering rate are guesses. Upgrade when PD2's sdo 38 is read from ProjectDiablo.dll.
auto SrvDoReturn(Game* game, Unit* missile) noexcept -> int64_t {
	using namespace d2r::rva;
	const auto fallback = d2r::Engine<MissileSrvDoFn>(MissileSrvDoDefault);
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	if (owner == nullptr) {
		return fallback(game, missile);
	}
	if (d2r::Engine<int32_t (*)(Unit*, Unit*) noexcept>(UnitDistance)(missile, owner) <= ReturnCatchDistance) {
		if (plog::Sample(g_returnsCaught)) {
			plog::Info("return missile: caught by the thrower, catch %u", g_returnsCaught.load());
		}
		return d2r::Engine<int64_t (*)(Game*, Unit*, Unit*, int32_t) noexcept>(MissileSrvEnd)(game, missile, nullptr, 1);
	}
	SteerHome(missile, owner, d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileSrvFrames)(missile));
	return fallback(game, missile);
}

// cdo 78 (PD2 cdo 70): the client's copy steers the same way; the server ends it.
void CltDoReturn(Unit* missile, int32_t a2) noexcept {
	using namespace d2r::rva;
	Unit* owner = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientMissileOwner)(missile);
	if (owner != nullptr) {
		SteerHome(missile, owner, d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileCltFrames)(missile));
	}
	d2r::Engine<MissileCltDoFn>(MissileCltDoDefault)(missile, a2);
}

// --- Double Throw's missiles (PD2 pSrvHitFunc 30 -> 66, pSrvDmgFunc 16 -> 21) ------------------------------------
namespace {

std::atomic<uint32_t> g_bounces {0};
std::atomic<uint32_t> g_firstHits {0};

using MissileIntFn = int32_t (*)(Unit*) noexcept;

auto MissileSkillRecord(Game* game, Unit* missile, int32_t* skillId, int32_t* level) noexcept -> const uint8_t* {
	*skillId = d2r::Engine<MissileIntFn>(d2r::rva::MissileSkill)(missile);
	*level = d2r::Engine<MissileIntFn>(d2r::rva::MissileLevel)(missile);
	return d2r::api.getSkillRecord(d2r::DataFlag(game), *skillId);
}

}  // namespace

// shit 66 (PD2 30, ProjectDiablo.dll 0x103a7a50): D2R's Chain Lightning hit (srvhit 12) as PD2 changed it. While the
// missile has bounces left, throw the same missile from it at the nearest other target within sHitPar1 or the skill's
// aurarangecalc (no minimum of 1, unlike srvhit 12), with one bounce less, and set damagepercent = calc1 on the
// missile that hit (PD2 sets it there, not on the new one).
// shortcut: PD2 also never bounces while the owner is in a PvP map (levels 157, 159, 166); those maps are cut.
auto SrvHitBounce(Game* game, Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr || hit == nullptr) {
		return 1;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
		dataFlag, At<int32_t>(missile, UnitClassId));
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	if (row == nullptr || owner == nullptr) {
		return 1;
	}
	int32_t skillId = 0, level = 0;
	const uint8_t* skill = MissileSkillRecord(game, missile, &skillId, &level);
	const int32_t bounces = d2r::Engine<MissileIntFn>(MissileBounces)(missile);
	int32_t x = 0, y = 0, tx = 0, ty = 0;
	if (skill == nullptr || bounces <= 1 || !d2r::UnitXY(missile, &x, &y)) {
		return 3;
	}
	int32_t range = At<int32_t>(row, MsHitPar1);
	if (range < 1) {
		range = d2r::EvalCalc(game, owner, skill, SkAuraRangeCalc, skillId, level);
	}
	using NearestFn = Unit* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t, uint32_t, int32_t*) noexcept;
	Unit* target = d2r::Engine<NearestFn>(NearestTarget)(game, owner, x, y, range, NearestTargetFlags,
	                                                       At<uint32_t>(hit, UnitIdField), nullptr);
	if (target == nullptr || target == hit || !d2r::UnitXY(target, &tx, &ty)) {
		return 3;
	}
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x21;  // start at x/y, as srvhit 12 sets it
	params.owner = owner;
	params.target = target;
	params.missile = At<int32_t>(missile, UnitClassId);
	params.x = x;
	params.y = y;
	params.targetX = tx;
	params.targetY = ty;
	params.skill = skillId;
	params.skillLevel = level;
	params.sourceKey = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey)(missile);  // srvhit 12 sets it too
	Unit* next = d2r::api.createMissile(game, &params);
	if (next == nullptr) {
		return 3;
	}
	const int32_t damage = d2r::EvalCalc(game, owner, skill, SkCalc1, skillId, level);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(missile, StatDamagePercent, damage, 0);
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetMissileBounces)(next, bounces - 1);
	if (plog::Sample(g_bounces)) {
		plog::Info("double throw bounce: missile %d range %d, bounces %d -> %d, damage%% %d, bounce %u",
		           params.missile, range, bounces, bounces - 1, damage, g_bounces.load());
	}
	return 3;
}

// dmg 21 (PD2 16, 0x103a6da0): while the missile still has all calc2 bounces (its first hit), raise the physical,
// fire, lightning, magic, cold and poison damage by calc3 percent. PD2's rows have calc3 = par4 = 0, so it adds nothing.
// shortcut: the fire to poison offsets assume 1.13c's +0x10 shift seen on phys and poison length (rvas_93847.h);
// read a D2R srvdmg function that writes them when a row gives calc3 a value.
void SrvDmgFirstHit(Game* game, Unit* missile, Unit* hit, uint8_t* damage) noexcept {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr || hit == nullptr || damage == nullptr) {
		return;
	}
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	int32_t skillId = 0, level = 0;
	const uint8_t* skill = MissileSkillRecord(game, missile, &skillId, &level);
	if (owner == nullptr || skill == nullptr) {
		return;
	}
	const int32_t bounces = d2r::EvalCalc(game, owner, skill, SkCalc2, skillId, level);
	const int32_t bonus = d2r::EvalCalc(game, owner, skill, SkCalc3, skillId, level);
	if (bounces != d2r::Engine<MissileIntFn>(MissileBounces)(missile) || bonus <= 0) {
		return;
	}
	for (int field : {DmgPhys, DmgLight, DmgCold, DmgFire, DmgMagic, DmgPoison}) {
		auto* value = reinterpret_cast<uint32_t*>(damage + field);
		*value = *value * static_cast<uint32_t>(bonus + 100) / 100;  // PD2's unsigned multiply and divide
	}
	if (plog::Sample(g_firstHits)) {
		plog::Info("double throw first hit: +%d%%, hit %u", bonus, g_firstHits.load());
	}
}

// --- W6 Amazon group -----------------------------------------------------------------------------------------------
namespace {

std::atomic<uint32_t> g_spreadHits {0};
std::atomic<uint32_t> g_spreadExplosions {0};
std::atomic<uint32_t> g_falloffHits {0};

constexpr int32_t ExplodingArrow = 16;  // PD2's callback hard-codes this skill's missiles
constexpr int32_t MaxExplosions = 16;   // a broken formula must not spawn hundreds of missiles
constexpr int32_t PierceCount = 522;    // merged id of PD2's pierce_count (merge/ids-itemstatcost.tsv, ADR 0003)

struct Vec {
	double x, y;
};
auto operator+(Vec a, Vec b) noexcept -> Vec { return {a.x + b.x, a.y + b.y}; }
auto operator-(Vec a, Vec b) noexcept -> Vec { return {a.x - b.x, a.y - b.y}; }
auto operator*(Vec a, double k) noexcept -> Vec { return {a.x * k, a.y * k}; }

auto Random01(uint32_t* seed) noexcept -> double {
	return d2r::RandomBelow(seed, 0x10000) / 65536.0;
}

// PD2 0x3813a0: a random point of triangle apex/b/c, between fractions u and v of the way from the apex.
auto PointInBand(uint32_t* seed, Vec apex, Vec b, Vec c, double u, double v) noexcept -> Vec {
	const double ra = Random01(seed), rb = Random01(seed);
	const double t = std::sqrt(Random01(seed) * (v * v - u * u) + u * u);
	const double s = std::sqrt(ra);
	return apex + ((b - apex) * (1.0 - s) + (c - apex) * (s * rb)) * t;
}

// The share of damage the n-th unit a piercing missile hits takes (PD2 dmg 17): 100 - (n-1)*step percent, at least
// `floor` percent.
constexpr auto PierceDamagePercent(int32_t n, int32_t step, int32_t floor) noexcept -> int32_t {
	const int32_t percent = 100 - (n - 1) * step;
	return percent > floor ? percent : floor;
}
static_assert(PierceDamagePercent(1, 20, 20) == 100);
static_assert(PierceDamagePercent(3, 20, 20) == 60);
static_assert(PierceDamagePercent(9, 20, 20) == 20);

}  // namespace

// shit 63 (PD2 0x3a8800, Fire Arrow and Exploding Arrow): srvhit 4's HitSubMissile1-4, then on a unit hit calc2
// explosions in a cone past the target, calc3 subtiles deep and Param4 wide, one pair per band, every 2 frames
// starting calc4 + 2 frames on. Exploding Arrow: min(3 + lvl/5, 8) explosions; Fire Arrow has no calc2, so none.
// shortcut: PD2 queues a timer per explosion with its own RNG and creates srvmissile b (fireexplosion3) only to send
// and remove it; this creates srvmissile a and b at once with `activation` = the delay and rolls on the owner's seed.
// Upgrade if the explosions show at the wrong time or fireexplosion3 lingers in play.
void SrvHitExplodeSpread(Game* game, Unit* missile, Unit* hit) noexcept {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr) {
		return;
	}
	d2r::Engine<MissileSrvHitFn>(SrvHitSubMissiles)(game, missile, hit);
	Unit* owner = hit == nullptr ? nullptr : d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	if (owner == nullptr) {
		return;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	// PD2 takes the skill from the missile row, so a monster's Exploding Arrow spreads too.
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
		dataFlag, At<int32_t>(missile, UnitClassId));
	if (row == nullptr) {
		return;
	}
	const int32_t skillId = At<int16_t>(row, MsSkill);
	const int32_t level = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileLevel)(missile);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	const uint8_t* explode = d2r::api.getSkillRecord(dataFlag, ExplodingArrow);
	int32_t hx = 0, hy = 0, ox = 0, oy = 0;
	if (skill == nullptr || explode == nullptr || !d2r::UnitXY(hit, &hx, &hy) || !d2r::UnitXY(owner, &ox, &oy)) {
		return;
	}
	int32_t count = d2r::EvalCalc(game, owner, skill, SkCalc2, skillId, level);
	if (count <= 0) {
		return;
	}
	count = count > MaxExplosions ? MaxExplosions : count;
	const Vec h {static_cast<double>(hx), static_cast<double>(hy)};
	const Vec d = Vec {static_cast<double>(ox), static_cast<double>(oy)} - h;  // toward the owner
	const double length = std::sqrt(d.x * d.x + d.y * d.y);
	if (length == 0.0) {
		return;
	}
	const Vec n = d * (1.0 / length), side {-n.y, n.x};
	const Vec base = h - n * d2r::EvalCalc(game, owner, skill, SkCalc3, skillId, level);
	const double halfWidth = At<int32_t>(skill, SkParam4) * 0.5;
	const Vec left = base + side * halfWidth, right = base - side * halfWidth;
	const int32_t start = d2r::EvalCalc(game, owner, skill, SkCalc4, skillId, level) + 2;
	const double pairs = count / 2 > 0 ? count / 2 : 1;  // PD2 divides by count / 2 unchecked
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(owner);

	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 1;  // placed at x/y, as PD2's callback 0x3044b0 creates them
	params.owner = owner;
	params.skill = ExplodingArrow;
	params.skillLevel = level;
	int32_t created = 0;
	for (int32_t i = 0; i < count; ++i) {
		const Vec p = PointInBand(seed, h, left, right, (i >> 1) / pairs, ((i >> 1) + 1) / pairs);
		params.x = static_cast<int32_t>(p.x);
		params.y = static_cast<int32_t>(p.y);
		params.activation = start + 2 * i;
		for (int column = 0; column < 2; ++column) {
			params.missile = At<int16_t>(explode, SkSrvMissileA + column * 2);
			if (d2r::ValidMissile(dataFlag, params.missile) && d2r::api.createMissile(game, &params) != nullptr) {
				created += column == 0 ? 1 : 0;
			}
		}
	}
	g_spreadExplosions.fetch_add(static_cast<uint32_t>(created));
	if (plog::Sample(g_spreadHits)) {
		plog::Info("explode spread: skill %d level %d: %d of %d explosions from frame %d, hit %u", skillId, level,
		           created, count, start, g_spreadHits.load());
	}
}

// dmg 20 (PD2 dmg 17, Multiple Shot arrows and bolts): each further unit a piercing missile hits takes DmgCalc1 percent
// less damage, down to dParam1 percent. Scales physical, the elements and the leeches.
// shortcut: PD2 counts pierces in its own pierce hook; this counts hits on the missile's pierce_count stat, so the last
// (not piercing) hit takes one step less than PD2's. Upgrade if the pierce hook is ported.
void SrvDmgPierceFalloff(Game* game, Unit* missile, Unit* target, uint8_t* damage) noexcept {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr || damage == nullptr) {
		return;
	}
	const int32_t n = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(missile, PierceCount, 0) + 1;
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(missile, PierceCount, n, 0);
	if (n <= 1) {
		return;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
		dataFlag, At<int32_t>(missile, UnitClassId));
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	if (row == nullptr || owner == nullptr) {
		return;
	}
	using EvalFn = int32_t (*)(uint32_t, Unit*, Unit*, int32_t, int32_t, int32_t) noexcept;
	const int32_t step =
		d2r::Engine<EvalFn>(EvalMissileCalc)(dataFlag, missile, owner, At<int32_t>(row, MsDmgCalc1), -1, -1);
	const int32_t percent = PierceDamagePercent(n, step, At<int32_t>(row, MsDParam1));
	for (int offset : {DmgPhys, DmgFire, DmgLight, DmgMagic, DmgCold, DmgPoison, DamageLifeLeech,
	                   DamageManaLeech, DamageStaminaLeech}) {
		auto* value = reinterpret_cast<int32_t*>(damage + offset);
		const int64_t scaled = static_cast<int64_t>(*value) * percent / 100;
		*value = scaled > INT32_MAX ? INT32_MAX : static_cast<int32_t>(scaled);
	}
	if (plog::Sample(g_falloffHits)) {
		plog::Info("pierce falloff: missile row %d target type %d, hit %d: %d%% damage, hit %u",
		           At<int32_t>(missile, UnitClassId), target == nullptr ? -1 : At<int32_t>(target, UnitType), n, percent,
		           g_falloffHits.load());
	}
}

// --- W11: state on hit (Poison Dagger's cloud, the throwing potions' clouds, Kanemith's blaze dot) ---------------------
namespace {

std::atomic<uint32_t> g_stateHits {0};

}  // namespace

// shit 61 (PD2 0x103a84f0, through 0x1036c920): the hit unit gets state sHitPar1 for sHitPar2 frames, carrying stat
// sHitPar3 = cHitPar1 and stat cHitPar2 = cHitPar3 (a stat of -1 or none is left out), from the missile row's Skill at
// the missile's level. Returns PD2's 2 + CollideKill: D2R's collision (0x4639a0) deals the missile's damage only when
// the result has bit 2, so a 0 or 1 (no hit unit or owner, a bad row) deals none.
// shortcut: PD2 also halves a negative resist (stats 36-45) on a unit already over 99 in it, and treats states 16, 87
// and 188 specially; none of shit 61's rows reach those states, and the halving is an approximated row on
// docs/unported.md. Upgrade if a resist-lowering row must match PD2 on immune monsters.
auto SrvHitState(Game* game, Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr) {
		return 1;
	}
	if (hit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
		dataFlag, At<int32_t>(missile, UnitClassId));
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
	if (owner == nullptr) {
		return 0;
	}
	if (row == nullptr || tables == nullptr) {
		return 1;
	}
	const int32_t damage = 2 + (At<uint8_t>(row, MsCollideKill) != 0);  // PD2's result: 2 deals the hit's damage
	const int32_t state = At<int32_t>(row, MsHitPar1), frames = At<int32_t>(row, MsHitPar2);
	if (state < 0 || static_cast<uint64_t>(state) >= At<uint64_t>(tables, TablesStateCount) || frames <= 0) {
		return 1;
	}
	const int32_t skillId = At<int16_t>(row, MsSkill);
	const int32_t level = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileLevel)(missile);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(RemoveSkillState)(hit, state, 1);
	d2r::StateListRequest request {owner, hit, skillId, level, frames, -1, 0, state,
	                               d2r::Engine<void (*)()>(SkillStateExpired)};
	void* list = d2r::Engine<void* (*)(d2r::StateListRequest*) noexcept>(NewStateList)(&request);
	if (list == nullptr) {
		return damage;
	}
	const auto setStat = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	const int32_t stats[2][2] = {{At<int32_t>(row, MsHitPar3), At<int32_t>(row, MsCltHitPar1)},
	                             {At<int32_t>(row, MsCltHitPar2), At<int32_t>(row, MsCltHitPar3)}};
	for (const auto& [stat, value] : stats) {
		if (stat >= 0) {  // the merge gives these the mod's itemstatcost ids (pd2_ids.HIT_PARAMS)
			setStat(dataFlag, list, stat, value, 0);
		}
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetUnitState)(hit, state, 1);
	if (plog::Sample(g_stateHits)) {
		plog::Info("state hit: missile row %d skill %d level %d: state %d for %d frames on unit type %d, stat %d %+d, "
		           "stat %d %+d, result %d, hit %u", At<int32_t>(missile, UnitClassId), skillId, level, state, frames,
		           At<int32_t>(hit, UnitType), stats[0][0], stats[0][1], stats[1][0], stats[1][1], damage,
		           g_stateHits.load());
	}
	return damage;
}

auto MissileFunctionCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "returns spawned %u, caught %u, bounces %u, first hits %u, spread hits %u, explosions %u, "
	              "falloff hits %u, state hits %u", g_returnSpawns.load(), g_returnsCaught.load(), g_bounces.load(),
	              g_firstHits.load(), g_spreadHits.load(), g_spreadExplosions.load(), g_falloffHits.load(),
	              g_stateHits.load());
}
