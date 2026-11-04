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

#include <algorithm>
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
// ProjectDiablo.dll. Returns srvhit 4's result, so the collision deals the missile's damage as srvhit 4's do.
auto SrvHitSpawnReturn(Game* game, Unit* missile, Unit* hit) noexcept -> int32_t {
	if (game == nullptr || missile == nullptr) {
		return 1;
	}
	const int32_t result = d2r::Engine<MissileSrvHitFn>(d2r::rva::SrvHitSubMissiles)(game, missile, hit);
	if (plog::Sample(g_returnSpawns)) {
		plog::Info("return missile: spawned on hit, result %d, hit %u", result, g_returnSpawns.load());
	}
	return result;
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

// --- Psychic Hammer (PD2 pSrvDmgFunc 15 -> 22; docs/research/w31-dragon-flight-psychic-hammer.md) -----------------
namespace {

std::atomic<uint32_t> g_chainFalloffs {0};
std::atomic<uint32_t> g_chainFalloffCuts {0};

// PD2 dmg 15's cut: calc2 percent per hit already made, (calc1 - bounces left) of them; at 100% or more, none left.
// PD2 multiplies in 32 bits; 64 here, the same below 2^31.
constexpr auto ChainFalloffMagic(int32_t magic, int32_t hits, int32_t bounces, int32_t step) noexcept -> int32_t {
	const int64_t cut = static_cast<int64_t>(hits - bounces) * step;
	return cut > 99 ? 0 : static_cast<int32_t>((100 - cut) * magic / 100);
}
static_assert(ChainFalloffMagic(1000, 3, 3, 20) == 1000);  // the first hit: no cut
static_assert(ChainFalloffMagic(1000, 3, 1, 20) == 600);   // the third: 40% less
static_assert(ChainFalloffMagic(1000, 6, 1, 20) == 0);     // the sixth: 100%, nothing left
static_assert(ChainFalloffMagic(999, 2, 1, 20) == 799);    // truncated

}  // namespace

// dmg 22 (PD2 15, ProjectDiablo.dll 0x103a6c90, Psychic Hammer): each unit the chain hits after the first takes calc2
// percent less magic damage per earlier hit, counted as calc1 (the skill's hits) - the missile's bounces left (D2Common
// #11018, D2R's MissileBounces). D2R's collision (0x4639a0) calls it when srvhit 12 returns 3 (bit 2: deal the damage).
void SrvDmgChainFalloff(Game* game, Unit* missile, Unit* target, uint8_t* damage) noexcept {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr || target == nullptr || damage == nullptr) {
		return;
	}
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	int32_t skillId = 0, level = 0;
	const uint8_t* skill = MissileSkillRecord(game, missile, &skillId, &level);
	if (owner == nullptr || skill == nullptr) {
		return;
	}
	const int32_t hits = d2r::EvalCalc(game, owner, skill, SkCalc1, skillId, level);
	const int32_t bounces = d2r::Engine<MissileIntFn>(MissileBounces)(missile);
	const int32_t step = d2r::EvalCalc(game, owner, skill, SkCalc2, skillId, level);
	auto* magic = reinterpret_cast<int32_t*>(damage + DmgMagic);
	const int32_t before = *magic;
	*magic = ChainFalloffMagic(before, hits, bounces, step);
	const bool sampleHit = plog::Sample(g_chainFalloffs);
	if ((*magic != before && plog::Sample(g_chainFalloffCuts)) || sampleHit) {
		plog::Info("chain falloff: skill %d level %d: calc1 %d, bounces %d, calc2 %d: magic %d -> %d, hit %u", skillId,
		           level, hits, bounces, step, before, *magic, g_chainFalloffs.load());
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
// Returns srvhit 4's result: 3 (PD2's, deal the arrow's damage), or 1 without a missile row (PD2's early exits).
auto SrvHitExplodeSpread(Game* game, Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr) {
		return 1;
	}
	const int32_t result = d2r::Engine<MissileSrvHitFn>(SrvHitSubMissiles)(game, missile, hit);
	Unit* owner = hit == nullptr ? nullptr : d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	if (owner == nullptr) {
		return result;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	// PD2 takes the skill from the missile row, so a monster's Exploding Arrow spreads too.
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
		dataFlag, At<int32_t>(missile, UnitClassId));
	if (row == nullptr) {
		return result;
	}
	const int32_t skillId = At<int16_t>(row, MsSkill);
	const int32_t level = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileLevel)(missile);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	const uint8_t* explode = d2r::api.getSkillRecord(dataFlag, ExplodingArrow);
	int32_t hx = 0, hy = 0, ox = 0, oy = 0;
	if (skill == nullptr || explode == nullptr || !d2r::UnitXY(hit, &hx, &hy) || !d2r::UnitXY(owner, &ox, &oy)) {
		return result;
	}
	int32_t count = d2r::EvalCalc(game, owner, skill, SkCalc2, skillId, level);
	if (count <= 0) {
		return result;
	}
	count = count > MaxExplosions ? MaxExplosions : count;
	const Vec h {static_cast<double>(hx), static_cast<double>(hy)};
	const Vec d = Vec {static_cast<double>(ox), static_cast<double>(oy)} - h;  // toward the owner
	const double length = std::sqrt(d.x * d.x + d.y * d.y);
	if (length == 0.0) {
		return result;
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
		plog::Info("explode spread: skill %d level %d: %d of %d explosions from frame %d, result %d, hit %u",
		           skillId, level, created, count, start, result, g_spreadHits.load());
	}
	return result;
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
	// No RemoveSkillState: like PD2's timed-state helper (0x1036d7d0), NewStateList extends a same-level list and
	// refuses a lower level, so a weaker cloud never replaces a stronger one.
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

// --- Bolt splash (PD2 shit 62, W28, docs/research/w28-sorceress-bolts.md) -------------------------------------------
namespace {

std::atomic<uint32_t> g_boltHits {0};
// The first splash with units hit is logged for each missile row too, so every bolt skill shows in the log.
std::atomic<uint64_t> g_boltRowsLogged[32] {};

auto FirstUnitHit(int32_t row) noexcept -> bool {
	if (row < 0 || row >= 32 * 64) {
		return false;
	}
	const uint64_t bit = 1ull << (row % 64);
	return (g_boltRowsLogged[row / 64].fetch_or(bit) & bit) == 0;
}

using AreaFn = void (*)(Game*, Unit*, d2r::AreaCenter*, int32_t, uint32_t, int32_t (*)(d2r::AreaContext*, Unit*), void*,
                        int32_t) noexcept;  // rva::AreaUnits

struct BoltSplash {
	d2r::LeapKnockback* rolled;  // the one roll every unit in range takes
	uint32_t hitFlags, resultFlags;
	int64_t victims;
};

// Copies the rolled block's plain fields (all but the list at 0x40 and the tagged object at 0x158) into a fresh one:
// ElementalDamage (0x437d00) writes only 0x0c-0x3c and 0x118-0x144, and never the flags.
void CopyRolled(const d2r::LeapKnockback& from, d2r::LeapKnockback& to) noexcept {
	const auto* src = reinterpret_cast<const uint8_t*>(&from);
	auto* dst = reinterpret_cast<uint8_t*>(&to);
	std::memcpy(dst, src, offsetof(d2r::LeapKnockback, listData));
	std::memcpy(dst + offsetof(d2r::LeapKnockback, pad118), src + offsetof(d2r::LeapKnockback, pad118),
	            sizeof(from.pad118));
}

// One unit in range (PD2's callback 0x10304a90): a copy of the roll through the area apply.
auto BoltSplashOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	auto* b = static_cast<BoltSplash*>(area->arg);
	d2r::LeapKnockback damage;
	CopyRolled(*b->rolled, damage);
	damage.hitFlags = b->hitFlags;
	damage.resultFlags = b->resultFlags;
	d2r::Engine<void (*)(Game*, Unit*, Unit*, void*) noexcept>(d2r::rva::AreaApply)(static_cast<Game*>(area->game),
	                                                                               area->unit, victim, &damage);
	b->victims += (damage.resultFlags & d2r::rva::ResultHit) != 0 ? 1 : 0;
	ReleaseDamage(damage);
	return 1;
}

}  // namespace

// shit 62 (PD2 0x103a86d0; Fire Bolt, Ice Bolt, Blizzard, Chilling Armor's bolt): wherever the missile hits, a unit or
// not, every unit within sHitPar1 of the missile takes the missile row's Skill's elemental damage at the missile's
// level + sHitPar2, rolled once, from the owner with PD2's unit mask, the row's HitFlags and ResultFlags | hit. Returns
// 1 as PD2 does, so the collision deals no damage of its own: the struck unit takes the splash.
// shortcut: PD2 also adds the skill's physical damage (1.13c D2Game 0x9f1a0) and puts the missile in the block
// (1.13c +0x5c); none of shit 62's skills has MinDam/MaxDam. Upgrade if a physical row joins shit 62.
auto SrvHitBoltSplash(Game* game, Unit* missile, Unit* /*hit*/) noexcept -> int32_t {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr) {
		return 1;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
		dataFlag, At<int32_t>(missile, UnitClassId));
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	int32_t x = 0, y = 0;
	if (row == nullptr || owner == nullptr || !d2r::UnitXY(missile, &x, &y)) {
		return 1;
	}
	const int32_t skillId = At<int16_t>(row, MsSkill);
	const int32_t level = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileLevel)(missile) + At<int32_t>(row, MsHitPar2);
	const int32_t radius = At<int32_t>(row, MsHitPar1);
	d2r::LeapKnockback rolled;
	d2r::Engine<void (*)(Unit*, void*, int32_t, int32_t) noexcept>(ElementalDamage)(owner, &rolled, skillId, level);
	BoltSplash b {&rolled, At<uint32_t>(row, MsHitFlags), At<uint16_t>(row, MsResultFlags) | uint32_t {ResultHit}, 0};
	d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(missile), x, y};
	d2r::Engine<AreaFn>(AreaUnits)(game, owner, &center, radius, AreaUnitMask, BoltSplashOne, &b, 0);
	if (plog::Sample(g_boltHits) || (b.victims > 0 && FirstUnitHit(At<int32_t>(missile, UnitClassId)))) {
		plog::Info("bolt hit: missile row %d skill %d level %d radius %d: fire %d cold %d (1/256), units hit %lld, "
		           "hit %u", At<int32_t>(missile, UnitClassId), skillId, level, radius, At<int32_t>(&rolled, DmgFire),
		           At<int32_t>(&rolled, DmgCold), static_cast<long long>(b.victims), g_boltHits.load());
	}
	ReleaseDamage(rolled);
	return 1;
}

// --- Client hits around the hit point (W17, docs/research/w17-holy-light.md) ---------------------------------------
namespace {

std::atomic<uint32_t> g_holyLightHits {0};
std::atomic<uint32_t> g_fistDelayHits {0};

using ClientAreaCallback = int32_t (*)(d2r::ClientAreaContext*, Unit*) noexcept;
using ClientAreaFn = void (*)(Unit*, int32_t, int32_t, int32_t, uint32_t, ClientAreaCallback, void*, int32_t) noexcept;

struct ClientBurst {
	Unit* owner;
	Unit* missile;
	Unit* skip;  // a unit that gets no sub-missile
	int32_t sub, skillId, level, key;
	int32_t created;
};

auto CltMissileRecord(Unit* missile) noexcept -> const uint8_t* {
	return d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::rva::MissileRecord)(
		d2r::api.unitDataFlag(missile), At<int32_t>(missile, d2r::rva::UnitClassId));
}

// PD2's callback 0x103a6bd0: CltHitSubMissile1 placed on each unit in range but the one hit (flags 0x22, the unit as
// origin and target).
auto HolyLightClientOne(d2r::ClientAreaContext* area, Unit* unit) noexcept -> int32_t {
	auto* b = static_cast<ClientBurst*>(area->arg);
	if (unit == b->skip) {
		return 0;
	}
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x22;
	params.owner = b->owner;
	params.origin = unit;
	params.target = unit;
	params.missile = b->sub;
	params.skill = b->skillId;
	params.skillLevel = b->level;
	b->created += d2r::api.clientCreateMissile(&params, 0) != nullptr ? 1 : 0;
	return 1;
}

// 1.13c's callback 0x6fb663c0 (D2R's 0x1bc380 without its swap): CltHitSubMissile1 from the delay missile to each unit
// in range (flags 0x20), the struck unit included.
auto FistBoltOne(d2r::ClientAreaContext* area, Unit* unit) noexcept -> int32_t {
	auto* b = static_cast<ClientBurst*>(area->arg);
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x20;
	params.owner = b->owner;
	params.origin = b->missile;
	params.missile = b->sub;
	if (!d2r::UnitXY(unit, &params.targetX, &params.targetY)) {
		return 1;
	}
	params.skill = b->skillId;
	params.skillLevel = b->level;
	params.sourceKey = b->key;
	b->created += d2r::api.clientCreateMissile(&params, 0) != nullptr ? 1 : 0;
	return 1;
}

}  // namespace

// chit 66 (PD2 0x103a69c0): the hit's CltHitSubMissile1 on every unit within cHitPar1 of the missile but the unit hit,
// with the row's Skill. PD2's holylightdelay names no CltHitSubMissile1, so its hit draws nothing more.
auto CltHitHolyLight(Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint8_t* row = missile != nullptr ? CltMissileRecord(missile) : nullptr;
	if (row == nullptr) {
		return 1;
	}
	Unit* owner = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientMissileOwner)(missile);
	ClientBurst b {owner, missile, hit, At<int16_t>(row, MsCltHitSubMissile1), At<int16_t>(row, MsSkill),
	               d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileLevel)(missile), 0, 0};
	int32_t x = 0, y = 0;
	const int32_t radius = At<int32_t>(row, MsCltHitPar1);
	const bool burst = owner != nullptr && d2r::ValidMissile(d2r::api.unitDataFlag(missile), b.sub) &&
	                   d2r::UnitXY(missile, &x, &y);
	if (burst) {
		d2r::Engine<ClientAreaFn>(ClientAreaUnits)(owner, x, y, radius, 0, HolyLightClientOne, &b, 0);
	}
	if (plog::Sample(g_holyLightHits)) {
		plog::Info("holy light client hit: missile row %d skill %d level %d: radius %d, sub-missile %d, %d created%s, "
		           "hit %u", At<int32_t>(missile, UnitClassId), b.skillId, b.level, radius, b.sub, b.created,
		           burst ? "" : " (no sub-missile or owner)", g_holyLightHits.load());
	}
	return 1;
}

// chit 71 (PD2 26, vanilla 1.13c D2Client 0xbad40): CltHitSubMissile1 from the delay missile to every unit within
// cHitPar1 (else the skill's aurarangecalc, at least 1). Zero when the missile's target unit is gone, as both do.
// D2R's 26 (0x1adcb0) differs: it stops at cHitPar2 / calc4 bolts and aims the struck unit's bolt at the caster.
// shortcut: no bolt limit; 1.13c computes cHitPar2 / calc4 and never stores it, so its callback compares against an unset
// stack slot. Upgrade if PD2 in play shows a bolt count cap (docs/unported.md).
auto CltHitFistDelay(Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	(void)hit;
	const uint8_t* row = missile != nullptr ? CltMissileRecord(missile) : nullptr;
	if (row == nullptr || At<int16_t>(row, MsHitSubMissile1) < 0) {
		return 1;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(missile);
	Unit* owner = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientMissileOwner)(missile);
	const int32_t skillId = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileSkill)(missile);
	const int32_t level = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileLevel)(missile);
	const uint8_t* skill = owner != nullptr ? d2r::api.getSkillRecord(dataFlag, skillId) : nullptr;
	if (skill == nullptr) {
		return 1;
	}
	const auto id = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileTargetId)(missile);
	const auto type = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileTargetType)(missile);
	if (d2r::Engine<Unit* (*)(int32_t, int32_t) noexcept>(ClientFindUnit)(id, type) == nullptr) {
		return 0;
	}
	int32_t radius = At<int32_t>(row, MsCltHitPar1);
	if (radius < 1) {
		radius = std::max(d2r::api.evalSkillCalc(dataFlag, owner, At<int32_t>(skill, SkAuraRangeCalc), skillId, level), 1);
	}
	ClientBurst b {owner, missile, nullptr, At<int16_t>(row, MsCltHitSubMissile1), skillId, level,
	               d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey)(missile), 0};
	int32_t x = 0, y = 0;
	const bool burst = d2r::ValidMissile(dataFlag, b.sub) && d2r::UnitXY(missile, &x, &y);
	if (burst) {
		d2r::Engine<ClientAreaFn>(ClientAreaUnits)(owner, x, y, radius, At<int32_t>(skill, SkAuraFilter) != 0 ? 0xa683 : 0,
		                                           FistBoltOne, &b, 0);
	}
	if (plog::Sample(g_fistDelayHits)) {
		plog::Info("fist delay client hit: skill %d level %d: radius %d, sub-missile %d, %d bolts%s, hit %u", skillId,
		           level, radius, b.sub, b.created, burst ? "" : " (no sub-missile)", g_fistDelayHits.load());
	}
	return 1;
}

// --- Assassin charge-up finishers' client missiles (W30, docs/research/w30-assassin-finishers.md) ------------------
namespace {

std::atomic<uint32_t> g_chargeExplodes {0};
std::atomic<uint32_t> g_chargeBoltHits {0};

}  // namespace

// cdo 79 (PD2 cdo 69, 0x103a60d0; fistsoffireexplode, bladesoficecubes): the missile's step (PD2 calls 1.13c's
// D2Client 0xb17c0 first, D2R's cltdo 1), then on its first frame CltSubMissile1 (fistsoffirefirewall,
// bladesoficeexplode) placed at its position (flags 3, no aim), with its skill and level; none without an owner,
// as in PD2.
void CltDoChargeExplode(Unit* missile, int32_t a2) noexcept {
	using namespace d2r::rva;
	if (missile == nullptr) {
		return;
	}
	d2r::Engine<MissileCltDoFn>(MissileCltDoDefault)(missile, a2);
	if (d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileCltFrames)(missile) != 1) {
		return;
	}
	const uint8_t* row = CltMissileRecord(missile);
	Unit* owner = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientMissileOwner)(missile);
	const int32_t sub = row != nullptr ? At<int16_t>(row, MsCltSubMissile1) : -1;
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	if (owner == nullptr || !d2r::ValidMissile(d2r::api.unitDataFlag(missile), sub) ||
	    !d2r::UnitXY(missile, &params.x, &params.y)) {
		return;
	}
	params.flags = MissileAtXY | MissileAimOffset;
	params.owner = owner;
	params.missile = sub;
	params.skill = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileSkill)(missile);
	params.skillLevel = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileLevel)(missile);
	const bool created = d2r::api.clientCreateMissile(&params, 0) != nullptr;
	if (plog::Sample(g_chargeExplodes)) {
		plog::Info("charge explode client: missile row %d skill %d level %d: sub-missile %d at %d,%d%s, explode %u",
		           At<int32_t>(missile, UnitClassId), params.skill, params.skillLevel, sub, params.x, params.y,
		           created ? "" : " (not created)", g_chargeExplodes.load());
	}
}

// chit 67 (PD2 0x103a6aa0; clawsofthunderbolt): CltHitSubMissile1 (cotthunderstorm) placed at the unit hit (flags 1)
// with the row's Skill and the missile's level, raised CHitCalc1 (its z height) and moving cHitPar1-3 per frame, as
// PD2 writes them through 1.13c's D2Client 0xc7e30 and the graphic's motion fields (D2R: SetMissileOffset/Motion).
auto CltHitChargeBolt(Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint8_t* row = missile != nullptr ? CltMissileRecord(missile) : nullptr;
	if (row == nullptr || hit == nullptr) {
		return 1;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(missile);
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = MissileAtXY;
	params.owner = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientMissileOwner)(missile);
	params.missile = At<int16_t>(row, MsCltHitSubMissile1);
	params.skill = At<int16_t>(row, MsSkill);
	params.skillLevel = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileLevel)(missile);
	if (!d2r::ValidMissile(dataFlag, params.missile) || !d2r::UnitXY(hit, &params.x, &params.y)) {
		return 1;
	}
	Unit* bolt = d2r::api.clientCreateMissile(&params, 0);
	int32_t z = 0;
	if (bolt != nullptr) {
		using EvalFn = int32_t (*)(uint32_t, Unit*, Unit*, int32_t, int32_t, int32_t) noexcept;
		// shortcut: no owner gives height 0 where PD2 still evaluates CHitCalc1 (D2R's evaluator is only known to run
		// with one); PD2's only user has the constant 280. Upgrade if a row's CHitCalc1 reads its owner.
		z = params.owner != nullptr ? d2r::Engine<EvalFn>(EvalMissileCalc)(dataFlag, missile, params.owner,
		                                                                  At<int32_t>(row, MsCltHitCalc1), -1, -1)
		                            : 0;
		d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetMissileOffset)(bolt, 0, 0, z);
		d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t, int32_t) noexcept>(SetMissileMotion)(
			bolt, At<int32_t>(row, MsCltHitPar1), At<int32_t>(row, MsCltHitPar2), At<int32_t>(row, MsCltHitPar3), 0);
	}
	if (plog::Sample(g_chargeBoltHits)) {
		plog::Info("charge bolt client hit: missile row %d skill %d level %d: sub-missile %d at %d,%d, height %d, "
		           "motion %d,%d,%d%s, hit %u", At<int32_t>(missile, UnitClassId), params.skill, params.skillLevel,
		           params.missile, params.x, params.y, z, At<int32_t>(row, MsCltHitPar1), At<int32_t>(row, MsCltHitPar2),
		           At<int32_t>(row, MsCltHitPar3), bolt != nullptr ? "" : " (not created)", g_chargeBoltHits.load());
	}
	return 1;
}

// --- Blade Fury's fragment (PD2 shit 34 -> 69, chit 6 -> 68, dmg 19; W32, docs/research/w32-blade-fury.md) ----------
namespace {

std::atomic<uint32_t> g_fragmentWalls {0};
std::atomic<uint32_t> g_fragmentSplits {0};
std::atomic<uint32_t> g_fragmentDamage {0};
std::atomic<uint32_t> g_fragmentClientWalls {0};

constexpr uint32_t FragmentFlags = 0x22;  // from the origin (the fragment), aimed at an offset: PD2's flags
constexpr uint16_t ResultFlag20 = 0x20;   // PD2's dmg 19 sets this result bit; the same bit in D2R's block
constexpr double DirectionsPerRadian = 64.0 / (2.0 * 3.14159265358979323846);  // PD2's 0x1044fbd0

// D2's 64 directions wrap. PD2 takes a signed % 64, so a negative index reads before its direction tables
// (docs/pd2-origin-bugs.md).
constexpr auto Direction64(int32_t d) noexcept -> int32_t {
	return ((d % 64) + 64) % 64;
}
static_assert(Direction64(-5) == 59 && Direction64(64) == 0 && Direction64(3) == 3);

// A new fragment's frames: the parent's less sHitPar3, at least cHitPar1 on a first wall bounce, 0 under cHitPar2.
constexpr auto ChildFrames(int32_t left, int32_t drop, int32_t firstMin, int32_t floor) noexcept -> int32_t {
	const int32_t frames = std::max(std::max(left - drop, 0), firstMin);
	return frames < floor ? 0 : frames;
}
static_assert(ChildFrames(20, 2, 10, 5) == 18 && ChildFrames(6, 2, 10, 5) == 10 && ChildFrames(6, 2, 0, 5) == 0);

auto MissileInt(uint64_t rva, Unit* missile) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*) noexcept>(rva)(missile);
}

auto AtLeast1(int32_t v) noexcept -> int32_t {
	return v == 0 ? 1 : v;  // PD2 reads an empty sHitPar1-3 as 1
}

// 1.13c's #10573 at sizes 0-2 (D2Common 0x4c570 / 0x4c5c0): the subtile, plus its four neighbours from size 2 on.
// shortcut: size 3 and up test the plus too, where 1.13c tests a wider box; fragments are size 1 (tested at 1 and 2).
// Upgrade when a missile of size 3 or more reaches it.
auto Collides(void* room, int32_t x, int32_t y, int32_t size) noexcept -> bool {
	const auto at = d2r::Engine<uint64_t (*)(void*, int32_t, int32_t, uint16_t) noexcept>(d2r::rva::CollisionAt);
	const uint16_t mask = d2r::rva::CollisionMissile;
	if (at(room, x, y, mask) != 0) {
		return true;
	}
	return size >= 2 &&
	       (at(room, x - 1, y, mask) | at(room, x + 1, y, mask) | at(room, x, y - 1, mask) | at(room, x, y + 1, mask)) != 0;
}

// PD2's wall bounce, both halves (server 0x103a7c70, client 0x103a6530): the owner -> fragment direction mirrored
// across x, else across y, gives sHitPar1 fragments sHitPar2 directions apart; each takes the first mirror whose point
// `size` subtiles ahead is free. Children get generation + 1 and ChildFrames; the parent ends. -1 when PD2 does not
// bounce (frames outside its window, the fragment not in a wall, no owner position).
template <typename Create>
auto WallBounce(Unit* missile, Unit* owner, const uint8_t* row, int32_t generation, Create create) noexcept -> int32_t {
	using namespace d2r::rva;
	const int32_t left = MissileInt(MissileSrvFrames, missile);
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(missile);
	int32_t x = 0, y = 0, ox = 0, oy = 0;
	if (left > MissileInt(MissileActivateFrame, missile) || left < 2 || MissileInt(MissileCltFrames, missile) <= 0 ||
	    room == nullptr || !d2r::UnitXY(missile, &x, &y) || !d2r::UnitXY(owner, &ox, &oy)) {
		return -1;
	}
	const int32_t size = MissileInt(UnitSize, missile);
	const double dx = x - ox, dy = y - oy, length = std::sqrt(dx * dx + dy * dy);
	if (!Collides(room, x, y, size + 1) || length == 0.0) {  // PD2 divides by the length unchecked
		return -1;
	}
	const int32_t count = AtLeast1(At<int32_t>(row, MsHitPar1)), step = AtLeast1(At<int32_t>(row, MsHitPar2));
	const int32_t frames = ChildFrames(left, AtLeast1(At<int32_t>(row, MsHitPar3)),
	                                   generation == 0 ? At<int32_t>(row, MsCltHitPar1) : 0, At<int32_t>(row, MsCltHitPar2));
	const auto* tx = reinterpret_cast<const int32_t*>(d2r::imageBase + DirectionX);
	const auto* ty = reinterpret_cast<const int32_t*>(d2r::imageBase + DirectionY);
	int32_t created = 0;
	for (int32_t i = 0, offset = -((count - 1) / 2) * step; i < count; ++i, offset += step) {
		for (int axis = 0; axis < 2; ++axis) {
			const double rx = axis == 0 ? -dx : dx, ry = axis == 0 ? dy : -dy;
			double angle = std::atan2(ry, rx) * DirectionsPerRadian;
			angle += angle < 0.0 ? 64.0 : 0.0;
			const int32_t d = Direction64(static_cast<int32_t>(angle) + offset);
			const double reach = size / std::sqrt(static_cast<double>(tx[d] * tx[d] + ty[d] * ty[d]));
			if (Collides(room, static_cast<int32_t>(tx[d] * reach + x), static_cast<int32_t>(ty[d] * reach + y), size)) {
				continue;
			}
			if (Unit* child = create(tx[d], ty[d])) {
				d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetMissileBounces)(child, generation + 1);
				d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetMissileFramesLeft)(child, frames);
				++created;
			}
			break;
		}
	}
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetMissileFramesLeft)(missile, 0);
	return created;
}

auto FragmentParams(Unit* owner, Unit* missile, int32_t sub) noexcept -> d2r::MissileParams {
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = FragmentFlags;
	params.owner = owner;
	params.origin = missile;
	params.missile = sub;
	params.skill = MissileInt(d2r::rva::MissileSkill, missile);
	params.skillLevel = MissileInt(d2r::rva::MissileLevel, missile);
	return params;
}

}  // namespace

// --- Blessed Hammer (W20, docs/research/w20-blessed-hammer.md) ------------------------------------------------------
namespace {

std::atomic<uint32_t> g_hammerFrames {0};
std::atomic<uint32_t> g_hammerHits {0};
std::atomic<uint32_t> g_hammerKept {0};
std::atomic<uint32_t> g_hammerClientFrames {0};

constexpr uint32_t HammerCollisionMask = 0x180;  // players and monsters only: no wall stops the hammer

// Both halves set the path's mask before the default frame, as PD2 does every frame; null without missile data or
// a path (PD2 then skips the frame).
auto HammerPath(Unit* missile) noexcept -> uint8_t* {
	if (missile == nullptr || At<uint8_t*>(missile, d2r::rva::UnitData) == nullptr) {
		return nullptr;
	}
	uint8_t* path = At<uint8_t*>(missile, d2r::rva::UnitPath);
	if (path != nullptr) {
		d2r::Engine<void (*)(uint8_t*, uint32_t) noexcept>(d2r::rva::PathSetCollisionMask)(path, HammerCollisionMask);
	}
	return path;
}

auto LastHitId(Unit* missile) noexcept -> uint32_t {
	const uint8_t* data = At<uint8_t*>(missile, d2r::rva::UnitData);
	return data != nullptr ? At<uint32_t>(data, d2r::rva::MdLastHitId) : 0xffffffffu;
}

}  // namespace

// shit 69 (PD2 34, 0x103a7c70; bladefragment1): a fragment of generation cHitPar3 or more only deals its damage (3).
// Otherwise a wall hit bounces it (WallBounce, HitSubMissile1) and returns 0; a unit hit splits it with Param1% chance
// into sHitPar1 - 1 fragments sHitPar2 directions apart from a random one, with the parent's frames less sHitPar3 (0
// under cHitPar2), and returns 3.
// shortcut: PD2 also sends each split fragment to every client (0x1037fe90, 1.13c D2Game 0x7b0a0); D2R's send is not
// found, so another player sees split fragments only if D2R syncs them (docs/unported.md). Upgrade when D2R's send of a
// missile to a client is found.
// shortcut: the roll is the owner's seed, where PD2 calls the C runtime's rand(); the odds are the same. Upgrade only if
// a check needs PD2's exact sequence.
auto SrvHitBladeFragment(Game* game, Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
		dataFlag, At<int32_t>(missile, UnitClassId));
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	if (row == nullptr || owner == nullptr) {
		return 0;
	}
	const int32_t generation = MissileInt(MissileBounces, missile);
	d2r::MissileParams params = FragmentParams(owner, missile, At<int16_t>(row, MsHitSubMissile1));
	if (generation >= At<int32_t>(row, MsCltHitPar3)) {
		return 3;
	}
	if (!d2r::ValidMissile(dataFlag, params.missile)) {
		return hit == nullptr ? 0 : 3;
	}
	const auto create = [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		return d2r::api.createMissile(game, &params);
	};
	if (hit == nullptr) {
		const int32_t created = WallBounce(missile, owner, row, generation, create);
		if (created >= 0 && plog::Sample(g_fragmentWalls)) {
			plog::Info("blade fragment wall: missile row %d skill %d level %d generation %d -> %d: %d of %d fragments, "
			           "wall %u", At<int32_t>(missile, UnitClassId), params.skill, params.skillLevel, generation,
			           generation + 1, created, AtLeast1(At<int32_t>(row, MsHitPar1)), g_fragmentWalls.load());
		}
		return 0;
	}
	uint32_t roll = d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(owner), 0x8000);
	const int32_t chance = std::clamp(At<int32_t>(row, MsParam1), 0, 100);
	const int32_t count = AtLeast1(At<int32_t>(row, MsHitPar1)) - 1, step = AtLeast1(At<int32_t>(row, MsHitPar2));
	const int32_t frames = ChildFrames(MissileInt(MissileSrvFrames, missile), AtLeast1(At<int32_t>(row, MsHitPar3)), 0,
	                                   At<int32_t>(row, MsCltHitPar2));
	const bool split = static_cast<int32_t>(roll % 100) < chance && count > 0;
	const auto* tx = reinterpret_cast<const int32_t*>(d2r::imageBase + DirectionX);
	const auto* ty = reinterpret_cast<const int32_t*>(d2r::imageBase + DirectionY);
	int32_t created = 0;
	for (int32_t i = 0, offset = -((count - 1) / 2) * step; split && i < count; ++i, offset += step, roll += 0x11) {
		const int32_t d = Direction64(static_cast<int32_t>(roll & 63) + offset);
		if (Unit* child = create(tx[d], ty[d])) {
			d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetMissileBounces)(child, generation + 1);
			d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetMissileFramesLeft)(child, frames);
			++created;
		}
	}
	if (plog::Sample(g_fragmentSplits)) {
		plog::Info("blade fragment hit: missile row %d skill %d level %d generation %d on unit type %d: chance %d%%, "
		           "%d of %d fragments with %d frames, hit %u", At<int32_t>(missile, UnitClassId), params.skill,
		           params.skillLevel, generation, At<int32_t>(hit, UnitType), chance, created, split ? count : 0, frames,
		           g_fragmentSplits.load());
	}
	return 3;
}

// dmg 19 (PD2 0x103a7130): the hit keeps dParam2% of its life, mana and stamina leech (PD2's unsigned multiply and
// divide); a roll over dParam1 (of 0-99) sets result flag 0x20, and on a fragment of a later generation drops the
// leech.
// shortcut: the roll is the fragment's seed, where PD2 calls the C runtime's rand(); the odds are the same. Upgrade
// only if a check needs PD2's exact sequence.
void SrvDmgBladeFragment(Game* game, Unit* missile, Unit* target, uint8_t* damage) noexcept {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr || target == nullptr || damage == nullptr) {
		return;
	}
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
		d2r::DataFlag(game), At<int32_t>(missile, UnitClassId));
	if (row == nullptr) {
		return;
	}
	const auto keep = static_cast<uint32_t>(At<int32_t>(row, MsDParam2));
	for (int field : {DamageLifeLeech, DamageManaLeech, DamageStaminaLeech}) {
		auto* value = reinterpret_cast<uint32_t*>(damage + field);
		*value = *value * keep / 100;
	}
	const auto roll = static_cast<int32_t>(d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(missile), 100));
	const int32_t generation = MissileInt(MissileBounces, missile);
	const bool flagged = At<int32_t>(row, MsDParam1) < roll;
	if (flagged) {
		*reinterpret_cast<uint16_t*>(damage + DamageResult) |= ResultFlag20;
		if (generation != 0) {
			for (int field : {DamageLifeLeech, DamageManaLeech, DamageStaminaLeech}) {
				*reinterpret_cast<uint32_t*>(damage + field) = 0;
			}
		}
	}
	if (plog::Sample(g_fragmentDamage)) {
		plog::Info("blade fragment damage: missile row %d generation %d on unit type %d: physical %d (1/256), leech "
		           "kept %u%% (life %u), roll %d over %d: %s, hit %u", At<int32_t>(missile, UnitClassId), generation,
		           At<int32_t>(target, UnitType), At<int32_t>(damage, DmgPhys), keep,
		           At<uint32_t>(damage, DamageLifeLeech), roll, At<int32_t>(row, MsDParam1),
		           flagged ? (generation != 0 ? "flag 0x20 and leech dropped" : "flag 0x20") : "no flag",
		           g_fragmentDamage.load());
	}
}

// chit 68 (PD2 6, 0x103a6530): the client's copy of the wall bounce with CltHitSubMissile1, so each client draws the
// fragments it predicts; a unit hit draws nothing more.
auto CltHitBladeFragment(Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint8_t* row = missile != nullptr && hit == nullptr ? CltMissileRecord(missile) : nullptr;
	if (row == nullptr) {
		return 1;
	}
	const int32_t generation = MissileInt(MissileBounces, missile);
	Unit* owner = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientMissileOwner)(missile);
	d2r::MissileParams params = FragmentParams(owner, missile, At<int16_t>(row, MsCltHitSubMissile1));
	if (generation >= At<int32_t>(row, MsCltHitPar3) || owner == nullptr ||
	    !d2r::ValidMissile(d2r::api.unitDataFlag(missile), params.missile)) {
		return 1;
	}
	const int32_t created = WallBounce(missile, owner, row, generation, [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		return d2r::api.clientCreateMissile(&params, 0);
	});
	if (created >= 0 && plog::Sample(g_fragmentClientWalls)) {
		plog::Info("blade fragment client wall: missile row %d skill %d level %d generation %d -> %d: %d of %d "
		           "fragments, wall %u", At<int32_t>(missile, UnitClassId), params.skill, params.skillLevel, generation,
		           generation + 1, created, AtLeast1(At<int32_t>(row, MsHitPar1)), g_fragmentClientWalls.load());
	}
	return 1;
}

// sdo 47 (PD2 40, ProjectDiablo.dll 0x103a7410): srvdo 1 with the path colliding only with units; a frame that ends
// on a collision (2) with range frames left goes on (1).
auto SrvDoBlessedHammer(Game* game, Unit* missile) noexcept -> int64_t {
	using namespace d2r::rva;
	uint8_t* path = game != nullptr ? HammerPath(missile) : nullptr;
	if (path == nullptr) {
		return 0;
	}
	const uint32_t hitBefore = LastHitId(missile);
	int64_t result = d2r::Engine<MissileSrvDoFn>(MissileSrvDoDefault)(game, missile);
	// shortcut: reads the missile after srvdo 1, which ends a missile by its mode (the unit is freed later in the
	// frame, as in 1.13c); move the reads before the call if a hammer end ever crashes here
	const int32_t framesLeft = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileSrvFrames)(missile);
	if (result == 2 && framesLeft > 0) {
		result = 1;
		if (plog::Sample(g_hammerKept)) {
			plog::Info("blessed hammer: collision kept flying, %d frames left, kept %u", framesLeft, g_hammerKept.load());
		}
	}
	const uint32_t hitAfter = LastHitId(missile);
	if (hitAfter != hitBefore && hitAfter != 0xffffffffu && plog::Sample(g_hammerHits)) {
		plog::Info("blessed hammer: hit unit type %d id %u at %u,%u, %d frames left, hit %u",
		           At<int32_t>(At<uint8_t*>(missile, UnitData), MdLastHitType), hitAfter, At<uint16_t>(path, 0x2),
		           At<uint16_t>(path, 0x6), framesLeft, g_hammerHits.load());
	}
	if (plog::Sample(g_hammerFrames)) {
		plog::Info("blessed hammer: missile row %d at %u,%u mask %#x, %d frames left, result %lld, frame %u",
		           At<int32_t>(missile, UnitClassId), At<uint16_t>(path, 0x2), At<uint16_t>(path, 0x6),
		           At<uint32_t>(path, PathCollisionMask), framesLeft, static_cast<long long>(result),
		           g_hammerFrames.load());
	}
	return result;
}

// cdo 80 (PD2 72, ProjectDiablo.dll 0x103a6300): the client's copy flies the same path: cltdo 1 with the same mask.
void CltDoBlessedHammer(Unit* missile, int32_t a2) noexcept {
	uint8_t* path = HammerPath(missile);
	if (path == nullptr) {
		return;
	}
	d2r::Engine<MissileCltDoFn>(d2r::rva::MissileCltDoDefault)(missile, a2);
	if (plog::Sample(g_hammerClientFrames)) {
		plog::Info("blessed hammer client: missile row %d at %u,%u mask %#x, frame %u",
		           At<int32_t>(missile, d2r::rva::UnitClassId), At<uint16_t>(path, 0x2), At<uint16_t>(path, 0x6),
		           At<uint32_t>(path, d2r::rva::PathCollisionMask), g_hammerClientFrames.load());
	}
}

auto MissileFunctionCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "returns spawned %u, caught %u, bounces %u, first hits %u, spread hits %u, explosions %u, "
	              "falloff hits %u, state hits %u, bolt hits %u, holy light client hits %u, fist delay client hits %u, "
	              "charge explodes %u, charge bolt hits %u, hammer frames %u, hits %u, kept %u, client frames %u, "
	              "chain falloffs %u, fragment walls %u, splits %u, damage %u, client walls %u",
	              g_returnSpawns.load(), g_returnsCaught.load(), g_bounces.load(), g_firstHits.load(),
	              g_spreadHits.load(), g_spreadExplosions.load(), g_falloffHits.load(), g_stateHits.load(),
	              g_boltHits.load(), g_holyLightHits.load(), g_fistDelayHits.load(), g_chargeExplodes.load(),
	              g_chargeBoltHits.load(), g_hammerFrames.load(), g_hammerHits.load(), g_hammerKept.load(),
	              g_hammerClientFrames.load(), g_chainFalloffs.load(), g_fragmentWalls.load(), g_fragmentSplits.load(),
	              g_fragmentDamage.load(), g_fragmentClientWalls.load());
}
