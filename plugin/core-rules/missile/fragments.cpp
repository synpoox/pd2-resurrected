// Blade Fury's fragment (W32) and Blessed Hammer (W20).
// Moved verbatim from missile_functions.cpp (#2198).
#include "shared.h"

namespace missiles {

std::atomic<uint32_t> g_fragmentWalls {0};
std::atomic<uint32_t> g_fragmentSplits {0};
std::atomic<uint32_t> g_fragmentDamage {0};
std::atomic<uint32_t> g_fragmentClientWalls {0};
std::atomic<uint32_t> g_hammerFrames {0};
std::atomic<uint32_t> g_hammerHits {0};
std::atomic<uint32_t> g_hammerKept {0};
std::atomic<uint32_t> g_hammerClientFrames {0};

auto MissileInt(uint64_t rva, Unit* missile) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*) noexcept>(rva)(missile);
}

}  // namespace missiles

// --- Blade Fury's fragment (PD2 shit 34 -> 69, chit 6 -> 68, dmg 19; W32, docs/research/skills/w32-blade-fury.md) ----------
namespace {


constexpr uint32_t FragmentFlags = 0x22;  // from the origin (the fragment), aimed at an offset: PD2's flags
constexpr uint16_t ResultFlag20 = 0x20;   // PD2's dmg 19 sets this result bit; the same bit in D2R's block
constexpr double DirectionsPerRadian = 64.0 / (2.0 * 3.14159265358979323846);  // PD2's 0x1044fbd0

// D2's 64 directions wrap. PD2 takes a signed % 64, so a negative index reads before its direction tables
// (docs/pd2/origin-bugs.md).
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

// --- Blessed Hammer (W20, docs/research/skills/w20-blessed-hammer.md) ------------------------------------------------------
namespace {


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
// found, so another player sees split fragments only if D2R syncs them (docs/deviations.md). Upgrade when D2R's send of a
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
		if (created >= 0) {
			++g_fragmentWalls;
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
	++g_fragmentSplits;
	return 3;
}

// dmg 19 (PD2 0x103a7130): the hit keeps dParam2% of its life, mana and stamina leech (PD2's unsigned multiply and
// divide); a roll over dParam1 (of 0-99) sets result flag 0x20, and on a fragment of a later generation drops the
// leech.
// shortcut: the roll is the fragment's seed, where PD2 calls the C runtime's rand(); the odds are the same. Upgrade
// only if a check needs PD2's exact sequence.
void SrvDmgBladeFragment(Game* game, Unit* missile, Unit* target, d2r::Damage* damageBlock) noexcept {
	auto* damage = reinterpret_cast<uint8_t*>(damageBlock);  // the block is opaque in the toolkit; its fields are read by offset
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
	++g_fragmentDamage;
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
	if (created >= 0) {
		++g_fragmentClientWalls;
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
		++g_hammerKept;
	}
	const uint32_t hitAfter = LastHitId(missile);
	if (hitAfter != hitBefore && hitAfter != 0xffffffffu) {
		++g_hammerHits;
	}
	++g_hammerFrames;
	return result;
}

// cdo 80 (PD2 72, ProjectDiablo.dll 0x103a6300): the client's copy flies the same path: cltdo 1 with the same mask.
void CltDoBlessedHammer(Unit* missile, float partFrame) noexcept {
	uint8_t* path = HammerPath(missile);
	if (path == nullptr) {
		return;
	}
	d2r::Engine<MissileCltDoFn>(d2r::rva::MissileCltDoDefault)(missile, partFrame);
	++g_hammerClientFrames;
}
