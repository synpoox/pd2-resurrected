// The red portal pair of PD2's map events that open areas, shared by Horazon (horazon.cpp) and the Void/Tomb event
// (map_events.cpp). Split from Horazon in #943.
#include "event_portals.h"

using d2r::Game;
using d2r::Unit;

auto OpenEventPortal(Game* game, void* room, int32_t x, int32_t y, int32_t level) noexcept -> Unit* {
	using namespace d2r::rva;
	Unit* portal = nullptr;
	const bool made = room != nullptr &&
		d2r::Engine<int32_t (*)(Game*, Unit*, void*, int32_t, int32_t, int32_t, Unit**, int32_t, int32_t, int32_t) noexcept>(
			PortalMaker)(game, nullptr, room, x, y, level, &portal, RedPortal, 0, 1) != 0;
	if (made && portal != nullptr) {
		d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetPortalLevel)(portal, level);
	}
	return made ? portal : nullptr;
}
