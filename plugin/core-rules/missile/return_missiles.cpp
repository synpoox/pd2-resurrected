// Split Throw's return missiles (shit 65, sdo 46, cdo 78).
// Moved verbatim from missile_functions.cpp (#2198).
#include "shared.h"

namespace missiles {

std::atomic<uint32_t> g_returnSpawns {0};
std::atomic<uint32_t> g_returnsCaught {0};


}  // namespace missiles

namespace {



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
	++g_returnSpawns;
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
		++g_returnsCaught;
		return d2r::Engine<int64_t (*)(Game*, Unit*, Unit*, int32_t) noexcept>(MissileSrvEnd)(game, missile, nullptr, 1);
	}
	SteerHome(missile, owner, d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileSrvFrames)(missile));
	return fallback(game, missile);
}

// cdo 78 (PD2 cdo 70): the client's copy steers the same way; the server ends it.
void CltDoReturn(Unit* missile, float partFrame) noexcept {
	using namespace d2r::rva;
	Unit* owner = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientMissileOwner)(missile);
	if (owner != nullptr) {
		SteerHome(missile, owner, d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileCltFrames)(missile));
	}
	d2r::Engine<MissileCltDoFn>(MissileCltDoDefault)(missile, partFrame);
}
