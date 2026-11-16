// Level effects PD2 sends to players: the act's darkness (0x1037dc70) and a sound in a level (0x1037f240). Moved out of
// lucion_ai.cpp (#606) for the Spire of Darkness map event (map_events.cpp). No PluginSDK service sends a packet or
// switches an act's light, so these are engine calls.
#include "level_fx.h"

#include "log.h"

#include <cstring>

using d2r::At;
using d2r::Game;
using d2r::Unit;

namespace {

auto LevelOf(Unit* unit) noexcept -> int32_t {
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(d2r::rva::UnitRoom)(unit);
	return room != nullptr ? d2r::Engine<int32_t (*)(void*) noexcept>(d2r::rva::RoomLevelId)(room) : -1;
}

// The game's clients in game (PD2 walks its player slots, game +0x1120)
template <class F>
auto ForEachClient(Game* game, F&& f) noexcept -> void {
	using namespace d2r::rva;
	for (auto* c = At<uint8_t*>(game, GameClients); c != nullptr; c = At<uint8_t*>(c, ClientNext)) {
		if (At<int32_t>(c, ClientState) == ClientInGame) {
			if (Unit* player = d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(ClientUnit)(c, 0); player != nullptr) {
				f(c, player);
			}
		}
	}
}

}  // namespace

// PD2 0x1037dc70 (game, act, on): the act's eclipse (1.13c D2Common #10337 on, #10895 off), switched once, and packet
// 0x53 {act, 5, on} to every client in game, as PD2 sends it (D2R's client reads the act as the light period)

auto SetDarkness(Game* game, Unit* unit, bool on) noexcept -> void {
	using namespace d2r::rva;
	uint8_t* act = At<uint8_t*>(unit, UnitAct);
	auto* const* acts = At<uint8_t**>(game, GameActs);
	int32_t index = -1;
	for (uint64_t i = 0; act != nullptr && i < At<uint64_t>(game, GameActCount); ++i) {
		index = acts[i] == act ? static_cast<int32_t>(i) : index;
	}
	uint8_t* env = act != nullptr ? At<uint8_t*>(act, ActEnv) : nullptr;
	if (index < 0 || env == nullptr) {
		return;
	}
	alignas(8) uint8_t packet[10] {0x53};
	std::memcpy(packet + 1, &index, 4);
	const int32_t time = 5;
	std::memcpy(packet + 5, &time, 4);
	packet[9] = on ? 1 : 0;
	const uint32_t flag = d2r::DataFlag(game);
	int sent = 0;
	ForEachClient(game, [&](void* client, Unit*) {
		const bool eclipse = At<int32_t>(env, EnvEclipse) != 0;
		if (on && !eclipse) {
			d2r::Engine<void (*)(uint8_t, void*) noexcept>(ActEclipseOn)(static_cast<uint8_t>(flag), act);
		} else if (!on && eclipse) {
			*reinterpret_cast<int32_t*>(env + EnvPause) = 0;
			*reinterpret_cast<int32_t*>(env + EnvSpeed) = *d2r::Engine<const int32_t*>(EnvSpeedNormal);
			d2r::Engine<void (*)(uint8_t, void*, void*, int32_t, int32_t, int32_t) noexcept>(ActLightSet)(
				static_cast<uint8_t>(flag), act, nullptr, 2, 0, 0);
		}
		d2r::Engine<void (*)(void*, uint8_t*) noexcept>(SendPacket10)(client, packet);
		++sent;
	});
	plog::Info("level fx: act %d darkness %s, eclipse %d, %d clients", index + 1, on ? "on" : "off",
	           At<int32_t>(env, EnvEclipse), sent);
}

// PD2 0x1037f240 (game, sound, 0, level): the sound played on each player in the level (packet 0x2c)
auto PlayLevelSound(Game* game, int32_t level, int32_t sound) noexcept -> void {
	using namespace d2r::rva;
	ForEachClient(game, [&](void* client, Unit* player) {
		if (level <= 0 || LevelOf(player) == level) {
			d2r::Engine<void (*)(Unit*, int32_t, Unit*) noexcept>(ClientMessage)(player, sound, player);
			d2r::Engine<void (*)(Unit*, void*) noexcept>(SoundPacket)(player, client);
		}
	});
}

auto SetLevelDarkness(Game* game, int32_t level, bool on) noexcept -> bool {
	Unit* inLevel = nullptr;
	ForEachClient(game, [&](void*, Unit* player) {
		inLevel = inLevel == nullptr && LevelOf(player) == level ? player : inLevel;
	});
	if (inLevel != nullptr) {
		SetDarkness(game, inLevel, on);
	}
	return inLevel != nullptr;
}
