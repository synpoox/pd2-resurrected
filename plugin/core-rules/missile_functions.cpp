// Split Throw's return missiles: PD2 missile pSrvHitFunc 65 on the thrown rows, pSrvDoFunc 38 / pCltDoFunc 70 on the
// <name>return rows. D2R fills sdo 38 and cdo 70, so ours sit in sdo 46 / cdo 78 and the data points there.
//
// The server calls missile srvdo as int64_t fn(Game*, Unit* missile) every frame, srvhit as
// void fn(Game*, Unit* missile, Unit* hit) on a collision, and the client missile cltdo as void fn(Unit* missile, int32_t).
#include "missile_functions.h"

#include "log.h"

#include <atomic>
#include <cstdio>

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

auto MissileFunctionCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "returns spawned %u, caught %u", g_returnSpawns.load(), g_returnsCaught.load());
}
