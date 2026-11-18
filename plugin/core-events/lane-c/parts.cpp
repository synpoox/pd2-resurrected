// Lane C map events: the parts and status counters this lane installs, registered by order (parts.h; #943).
// A lane adds its parts here, in its own folder, and never edits plugin.cpp. Orders are one global sequence.
#include "part_registry.h"

#include "anya_maps.h"
#include "guardian_ai.h"
#include "horazon.h"
#include "invaders.h"
#include "map_boss_tc.h"
#include "map_events.h"
#include "pd2_engine.h"
#include "treasure_fallen.h"
#include "wanderer.h"

#include <cstdio>

namespace {

// The signature entries the Dark Wanderer part reads (wanderer.cpp, ai_hooks.cpp, level_fx.cpp): it installs on any image
// where they all resolved, so it reads no fixed rva value.
sigscan::Entry* const WandererUses[] = {
	&d2r::sig::AiDarkWanderer, &d2r::sig::AiTable, &d2r::sig::GameKey, &d2r::sig::GameById, &d2r::sig::UnitStat,
	&d2r::sig::SetUnitStat, &d2r::sig::UnitId, &d2r::sig::NewStatList, &d2r::sig::SetListState, &d2r::sig::SetListStat,
	&d2r::sig::AttachStatList, &d2r::sig::SetStateBit, &d2r::sig::SetStateGfxFlag, &d2r::sig::QueueUnitUpdate,
	&d2r::sig::FindUnit, &d2r::sig::UnitRoom, &d2r::sig::RoomAtXY, &d2r::sig::FindFreeSpot, &d2r::sig::DataTables,
	&d2r::sig::SpawnMonsterAt, &d2r::sig::SetUnitFlags, &d2r::sig::AiSetModeXY, &d2r::sig::AiIdle, &d2r::sig::WalkToSpot,
	&d2r::sig::SpotDistance, &d2r::sig::AiStepBeforeWalk,
	// PlayLevelSound and LevelOf (level_fx.cpp)
	&d2r::sig::ClientMessage, &d2r::sig::SoundPacket, &d2r::sig::ClientUnit, &d2r::sig::RoomLevelId,
	// the record offsets the part reads (unit, game, client and AI tick fields), gated as a set
	&d2r::sig::RecordOffsets,
};

void InstallMapeventsPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallMapEvents(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

void InstallEventsPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallEventPortals(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

void InstallGuardianaiPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallGuardianAi(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

PartReg r40(40, "mapevents", InstallMapeventsPart);
PartReg r110(110, "events", InstallEventsPart);
void InstallWandererPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallWanderer(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

void InstallTreasureFallenPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallTreasureFallen(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

void InstallInvadersPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallInvaders(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

void InstallAnyamapsPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallAnyaMaps(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

// Map boss loot by map tier (#1423): the TC drop it wraps and the TC lookups it makes
void InstallMapBossTcPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallMapBossTc(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

sigscan::Entry* const MapBossTcUses[] = {&d2r::sig::TcDrop, &d2r::sig::TcIndexByName, &d2r::sig::TcRecord, &d2r::sig::DataTables};

sigscan::Entry* const AnyamapsUses[] = {&d2r::sig::FindUnit, &d2r::sig::ItemRecord};

PartReg r240(240, "anyamaps", InstallAnyamapsPart, AnyamapsUses);
CountersReg c240(240, AnyaMapsCounters);
PartReg r160(160, "guardianai", InstallGuardianaiPart);
PartReg r170(170, "wanderer", InstallWandererPart, WandererUses);
PartReg r180(180, "treasurefallen", InstallTreasureFallenPart);
PartReg r190(190, "invaders", InstallInvadersPart);
PartReg r250(250, "mapbosstc", InstallMapBossTcPart, MapBossTcUses);
CountersReg c40(40, MapEventsCounters);
CountersReg c120(120, EventPortalsCounters);
CountersReg c200(200, GuardianAiCounters);
CountersReg c210(210, WandererCounters);
CountersReg c220(220, TreasureFallenCounters);
CountersReg c230(230, InvadersCounters);
CountersReg c250(250, MapBossTcCounters);

}  // namespace
