// Throwing potions (shit 68, chit 72).
// Moved verbatim from missile_functions.cpp (#2198).
#include "shared.h"

namespace missiles {

std::atomic<uint32_t> g_potionHits {0};
std::atomic<uint32_t> g_potionClientHits {0};


}  // namespace missiles

// --- Throwing potions (PD2 pSrvHitFunc 60 / pCltHitFunc 65, here 68 / 72; IR5, #240) ----------------------------------
// Read from ProjectDiablo.dll: shit 60 at 0x103a8360, chit 65 at 0x103a6890. The tpot_* potions (and Sharptooth's
// Armageddon, which stays on its stand-in) burst into HitSubMissile1 (the explosion) and a ring of HitSubMissile2 (the
// nova, cloud or bolts).
namespace {


constexpr uint32_t PotionSpawnFlags = 0x4020010;  // PD2's unit flags on the sHitPar3 spawn (Sharptooth's minions)
constexpr int MsHitSubMissile2 = d2r::rva::MsHitSubMissile1 + 2, MsCltHitSubMissile2 = d2r::rva::MsCltHitSubMissile1 + 2;

// PD2's nova ring (ProjectDiablo.dll 0x1036d250, client 0x1036d370): from x/y, aimed at the direction tables' offsets,
// every (64 / count)th direction (count 0 or past 64: 64), so ceil(64 / step) missiles: count 5 makes 6.
constexpr auto RingStep(int32_t count) noexcept -> int32_t {
	return 64 / (count <= 0 || count > 64 ? 64 : count);
}
constexpr auto RingCount(int32_t count) noexcept -> int32_t {
	return (64 + RingStep(count) - 1) / RingStep(count);
}
static_assert(RingCount(0) == 64 && RingCount(16) == 16 && RingCount(65) == 64 && RingCount(5) == 6 && RingCount(8) == 8);

template <typename Create>
auto PotionRing(d2r::MissileParams params, int32_t count, Create create) noexcept -> int32_t {
	const auto* dx = reinterpret_cast<const int32_t*>(d2r::imageBase + d2r::rva::DirectionX);
	const auto* dy = reinterpret_cast<const int32_t*>(d2r::imageBase + d2r::rva::DirectionY);
	const int32_t step = RingStep(count);
	int32_t created = 0;
	for (int32_t i = 0; i < 64; i += step) {
		params.targetX = dx[i];
		params.targetY = dy[i];
		created += create(&params) ? 1 : 0;
	}
	return created;
}

// The explosion: from the potion (flags 0x22, no offset), as PD2's.
auto BurstParams(Unit* owner, Unit* missile, int32_t sub, int32_t skill, int32_t level) noexcept -> d2r::MissileParams {
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x22;
	params.owner = owner;
	params.origin = missile;
	params.missile = sub;
	params.skill = skill;
	params.skillLevel = level;
	return params;
}

}  // namespace

// shit 68: HitSubMissile1 and a ring of sHitPar1 HitSubMissile2, both at the missile level plus sHitPar2 (the potion's
// size), from the row's Skill; then monstats sHitPar3 at the potion when it is set. 3 (the potion's damage) on a unit
// hit, else 1, as PD2's.
auto SrvHitThrowPotion(Game* game, Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	if (game == nullptr || missile == nullptr) {
		return 1;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto* row = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(
		dataFlag, At<int32_t>(missile, UnitClassId));
	int32_t x = 0, y = 0;
	if (row == nullptr || !d2r::UnitXY(missile, &x, &y)) {
		return 1;
	}
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	const int32_t skill = At<int16_t>(row, MsSkill);
	const int32_t level = MissileInt(MissileLevel, missile) + At<int32_t>(row, MsHitPar2);
	int32_t burst = 0, ring = 0;
	d2r::MissileParams params = BurstParams(owner, missile, At<int16_t>(row, MsHitSubMissile1), skill, level);
	if (d2r::ValidMissile(dataFlag, params.missile) && d2r::api.createMissile(game, &params) != nullptr) {
		burst = 1;
	}
	const int32_t nova = At<int16_t>(row, MsHitSubMissile2);
	if (d2r::ValidMissile(dataFlag, nova)) {
		d2r::MissileParams ringParams;
		std::memset(&ringParams, 0, sizeof(ringParams));
		ringParams.flags = MissileAtXY | MissileAimOffset;
		ringParams.owner = owner;
		ringParams.missile = nova;
		ringParams.x = x;
		ringParams.y = y;
		ringParams.skill = skill;
		ringParams.skillLevel = level;
		ring = PotionRing(ringParams, At<int32_t>(row, MsHitPar1),
		                  [&](d2r::MissileParams* p) { return d2r::api.createMissile(game, p) != nullptr; });
	}
	const int32_t mon = At<int32_t>(row, MsHitPar3);
	Unit* spawn = nullptr;
	if (mon > 0 && d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(dataFlag, mon) != nullptr) {
		spawn = d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(
			SpawnMonsterAt)(game, d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(missile), mon, x, y, 1, 0);
		if (spawn != nullptr) {
			*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(spawn) + UnitMainFlags) |= PotionSpawnFlags;
		}
	}
	++g_potionHits;
	return hit != nullptr ? 3 : 1;
}

// chit 72: CltHitSubMissile1 and a ring of cHitPar1 CltHitSubMissile2 at the missile level, from the row's Skill. Always
// 1 (the hit goes on), as PD2's; nothing when the potion has no owner.
auto CltHitThrowPotion(Unit* missile, Unit* /*hit*/) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint8_t* row = missile != nullptr ? CltMissileRecord(missile) : nullptr;
	Unit* owner = row != nullptr ? d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientMissileOwner)(missile) : nullptr;
	int32_t x = 0, y = 0;
	if (owner == nullptr || !d2r::UnitXY(missile, &x, &y)) {
		return 1;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(missile);
	const int32_t skill = At<int16_t>(row, MsSkill), level = MissileInt(MissileLevel, missile);
	int32_t burst = 0, ring = 0;
	d2r::MissileParams params = BurstParams(owner, missile, At<int16_t>(row, MsCltHitSubMissile1), skill, level);
	if (d2r::ValidMissile(dataFlag, params.missile) && d2r::api.clientCreateMissile(&params, 0) != nullptr) {
		burst = 1;
	}
	const int32_t nova = At<int16_t>(row, MsCltHitSubMissile2);
	if (d2r::ValidMissile(dataFlag, nova)) {
		d2r::MissileParams ringParams;
		std::memset(&ringParams, 0, sizeof(ringParams));
		ringParams.flags = MissileAtXY | MissileAimOffset;
		ringParams.owner = owner;
		ringParams.missile = nova;
		ringParams.x = x;
		ringParams.y = y;
		ringParams.skill = skill;
		ringParams.skillLevel = level;
		ring = PotionRing(ringParams, At<int32_t>(row, MsCltHitPar1),
		                  [](d2r::MissileParams* p) { return d2r::api.clientCreateMissile(p, 0) != nullptr; });
	}
	++g_potionClientHits;
	return 1;
}
