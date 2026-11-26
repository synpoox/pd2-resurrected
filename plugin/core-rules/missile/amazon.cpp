// W6 Amazon group: Exploding Arrow's spread (shit 63) and Multiple Shot's pierce falloff (dmg 20).
// Moved verbatim from missile_functions.cpp (#2198).
#include "shared.h"

namespace missiles {

std::atomic<uint32_t> g_spreadHits {0};
std::atomic<uint32_t> g_spreadExplosions {0};
std::atomic<uint32_t> g_falloffHits {0};


}  // namespace missiles

// --- W6 Amazon group -----------------------------------------------------------------------------------------------
namespace {


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
	++g_spreadHits;
	return result;
}

// dmg 20 (PD2 dmg 17, Multiple Shot arrows and bolts): each further unit a piercing missile hits takes DmgCalc1 percent
// less damage, down to dParam1 percent. Scales physical, the elements and the leeches.
// shortcut: PD2 counts pierces in its own pierce hook; this counts hits on the missile's pierce_count stat, so the last
// (not piercing) hit takes one step less than PD2's. Upgrade if the pierce hook is ported.
void SrvDmgPierceFalloff(Game* game, Unit* missile, Unit*, d2r::Damage* damageBlock) noexcept {
	auto* damage = reinterpret_cast<uint8_t*>(damageBlock);  // the block is opaque in the toolkit; its fields are read by offset
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
	++g_falloffHits;
}
