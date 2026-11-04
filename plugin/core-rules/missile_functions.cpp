// Split Throw's return missiles: PD2 missile pSrvHitFunc 65 on the thrown rows, pSrvDoFunc 38 / pCltDoFunc 70 on the
// <name>return rows. D2R fills sdo 38 and cdo 70, so ours sit in sdo 46 / cdo 78 and the data points there.
//
// The server calls missile srvdo as int64_t fn(Game*, Unit* missile) every frame, srvhit as
// void fn(Game*, Unit* missile, Unit* hit) on a collision, and the client missile cltdo as void fn(Unit* missile, int32_t).
#include "missile_functions.h"

#include "log.h"

#include <atomic>
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

auto MissileFunctionCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "returns spawned %u, caught %u, bounces %u, first hits %u", g_returnSpawns.load(),
	              g_returnsCaught.load(), g_bounces.load(), g_firstHits.load());
}
