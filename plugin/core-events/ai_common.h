// Helpers the Dclone AI (dclone_ai.cpp, lane D) and the event Nihlathak AI (nihlathak_ai.cpp, lane C) share: moved
// unchanged out of dclone_ai.cpp (#943), so both keep one counter set and one cast log. Shared: frozen.
#pragma once

#include "log.h"
#include "hot_log.h"
#include "pd2_engine.h"

#include <atomic>
#include <cstdint>

namespace ai_common {

using d2r::At;
using d2r::Game;
using d2r::Unit;

inline std::atomic<uint32_t> g_casts {0};

inline auto Counter(Game* game, uint32_t key) noexcept -> int32_t& {
	return *d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

inline auto LevelOf(Unit* unit) noexcept -> int32_t {
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(d2r::rva::UnitRoom)(unit);
	return room != nullptr ? d2r::Engine<int32_t (*)(void*) noexcept>(d2r::rva::RoomLevelId)(room) : -1;
}

inline auto Dead(Unit* unit) noexcept -> bool {
	return d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::UnitDead)(unit) != 0;
}

inline auto Dist2(Unit* unit, int32_t x, int32_t y) noexcept -> int32_t {
	int32_t ux = 0, uy = 0;
	d2r::UnitXY(unit, &ux, &uy);
	return (ux - x) * (ux - x) + (uy - y) * (uy - y);
}

inline auto Idle(Game* game, Unit* unit, int32_t frames) noexcept -> void {
	d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(d2r::rva::AiIdle)(game, unit, frames);
}

// PD2 0x103a1880 then 0x103a1910: cast, then think again after `frames`
inline auto Cast(Game* game, Unit* unit, int32_t skill, int32_t mode, Unit* target, int32_t x, int32_t y,
          int32_t frames) noexcept -> int32_t {
	const int32_t ok = d2r::Engine<int32_t (*)(Game*, Unit*, uint8_t, int32_t, Unit*, int32_t, int32_t, uint8_t) noexcept>(
		d2r::rva::AiUseSkill)(game, unit, static_cast<uint8_t>(mode), skill, target, x, y, 0);
	if (pd2::HotSample(g_casts)) {
		plog::Info("dclone ai: cast skill %d mode %d at (%d, %d)%s -> %d (%u so far)", skill, mode, x, y,
		           target != nullptr ? " on target" : "", ok, g_casts.load());
	}
	if (frames > 0) {  // 0: PD2 casts without 0x103a1910
		d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(d2r::rva::AiSetDelay)(game, unit, frames);
	}
	return ok;
}

// PD2 0x10356e00, as lucion.cpp's KillQuietly: no drop, then the death mode
inline auto KillQuietly(Game* game, Unit* monster) noexcept -> void {
	using namespace d2r::rva;
	d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(SetUnitFlags)(monster, UnitNoDrop, 1);
	alignas(8) uint8_t block[0x30] = {};
	d2r::Engine<void (*)(Unit*, int32_t, void*) noexcept>(ModeCmdInit)(monster, ModeDeath, block);
	*reinterpret_cast<void**>(block + 0x10) = d2r::Engine<void* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, monster);
	d2r::Engine<uint64_t (*)(Game*, void*, int32_t) noexcept>(ModeCmdRun)(game, block, 1);
}

struct Mon {
	const uint8_t* rec;
	auto Skill(int slot) const noexcept -> int32_t { return At<int16_t>(rec, d2r::rva::MonSkills + 2 * (slot - 1)); }
	auto Mode(int slot) const noexcept -> int32_t { return At<uint8_t>(rec, d2r::rva::MonSkillModes + slot - 1); }
	// aip1-8 are int16[8][3]; PD2 reads the Hell column
	auto Aip(int n) const noexcept -> int32_t { return At<int16_t>(rec, d2r::rva::MonAiParams + 2 * (3 * (n - 1) + 2)); }
	// One difficulty column (0 Normal, 1 Nightmare, 2 Hell), unsigned as PD2 reads it
	auto Aip(int n, int col) const noexcept -> int32_t {
		return At<uint16_t>(rec, d2r::rva::MonAiParams + 2 * (3 * (n - 1) + col));
	}
	auto AiDel() const noexcept -> int32_t { return At<uint8_t>(rec, d2r::rva::MonAiDel + 2); }
};

inline auto NearestPlayer(Game* game, Unit* unit, int32_t level) noexcept -> Unit* {
	using namespace d2r::rva;
	int32_t sx = 0, sy = 0;
	d2r::UnitXY(unit, &sx, &sy);
	Unit* nearest = nullptr;
	int32_t best = 0x7fffffff;
	for (auto* client = At<uint8_t*>(game, GameClients); client != nullptr; client = At<uint8_t*>(client, ClientNext)) {
		Unit* p = d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(ClientUnit)(client, 0);
		if (p != nullptr && !Dead(p) && LevelOf(p) == level && Dist2(p, sx, sy) < best) {
			best = Dist2(p, sx, sy);
			nearest = p;
		}
	}
	return nearest;
}

}  // namespace ai_common
