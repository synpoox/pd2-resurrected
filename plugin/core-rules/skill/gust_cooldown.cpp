// Twister/Tornado shorten Gust's running cooldown by Param6 frames (#2222, PD2 srvdo 118).
// No SDK service edits skill delays. D2R stores local delays in state 185, stat 359 layered by skill:
// srv setter 93847 0x439500 -> 0x339e60; client setter 0x217b90 uses milliseconds (40 per frame).
// Keep D2R's missiles, change only Gust's entry, then let its own callbacks recompute the earliest
// expiry across ALL local delays. The server callback also requeues event 12 keyed by state 185.
#include "common.h"
#include "registry.h"
#include "sig_hook.h"

#include <algorithm>
#include <cstdio>

namespace {

using d2r::Game;
using d2r::Unit;
using ServerFn = int64_t (*)(Game*, Unit*, int32_t, int32_t) noexcept;
using ClientFn = int32_t (*)(Unit*, int32_t, int32_t) noexcept;
using ExpireFn = bool (*)(Unit*, int32_t, void*) noexcept;
using ClockFn = int32_t (*)() noexcept;
ServerFn g_server = nullptr;
ClientFn g_twister = nullptr, g_tornado = nullptr;
ExpireFn g_serverExpire = nullptr, g_clientExpire = nullptr;
ClockFn g_clock = nullptr;
bool g_on = false;  // a partially installed set of hooks must leave both sides unchanged

constexpr int32_t Gust = 442, Twister = 240, Tornado = 245;
constexpr int32_t CooldownState = 185, CooldownStat = 359;

constexpr auto Shorten(int32_t end, int32_t now, int32_t frames, int32_t scale) noexcept -> int32_t {
	if (end <= now || frames <= 0) {
		return end;
	}
	return static_cast<int32_t>(std::min(int64_t{end},
	    std::max(int64_t{now} + scale, int64_t{end} - int64_t{frames} * scale)));
}
static_assert(Shorten(200, 100, 13, 1) == 187);
static_assert(Shorten(110, 100, 13, 1) == 101);  // at least next frame, as PD2
static_assert(Shorten(101, 100, 13, 1) == 101);
static_assert(Shorten(0, 100, 13, 1) == 0);  // no cooldown: never create one
static_assert(Shorten(100, 100, 13, 1) == 100);
static_assert(Shorten(200, 100, 0, 1) == 200);
static_assert(Shorten(200, 100, -1, 1) == 200);
static_assert(Shorten(5000, 1000, 13, 40) == 4480);
static_assert(Shorten(1200, 1000, 13, 40) == 1040);
static_assert(Shorten(1010, 1000, 13, 40) == 1010);  // never extend a sub-frame client remainder
static_assert(Shorten(Shorten(200, 100, 13, 1), 100, 13, 1) == 174);

auto Reduce(Unit* unit, uint32_t dataFlag, int32_t skillId, int32_t now, int32_t scale) noexcept -> void* {
	if (unit == nullptr || (skillId != Twister && skillId != Tornado)) {
		return nullptr;
	}
	const uint8_t* record = d2r::api.getSkillRecord(dataFlag, skillId);
	void* list = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(d2r::sig::UnitStateList)(unit, CooldownState);
	if (record == nullptr || list == nullptr) {
		return nullptr;
	}
	const int32_t end = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(
	    d2r::sig::ListStatValue)(dataFlag, list, CooldownStat, Gust);
	const int32_t shortened = Shorten(end, now, d2r::At<int32_t>(record, d2r::rva::SkParam6), scale);
	if (shortened == end) {
		return nullptr;
	}
	d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(d2r::sig::SetListStat)(
	    dataFlag, list, CooldownStat, shortened, Gust);
	return list;
}

auto Server(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	const int64_t result = g_server(game, unit, skillId, level);
	if (g_on && result != 0 && game != nullptr) {
		if (void* list = Reduce(unit, d2r::DataFlag(game), skillId, d2r::At<int32_t>(game, d2r::rva::GameFrame), 1)) {
			d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(d2r::sig::RemoveUnitEvent)(
			    game, unit, 12, CooldownState);
			g_serverExpire(unit, 0, list);  // 0x43ac00: requeue earliest expiry and update the list timer
		}
	}
	return result;
}

auto Client(Unit* unit, int32_t skillId, int32_t level, ClientFn original) noexcept -> int32_t {
	const int32_t result = original(unit, skillId, level);
	if (g_on && result != 0 && unit != nullptr) {
		if (void* list = Reduce(unit, d2r::api.unitDataFlag(unit), skillId, g_clock(), 40)) {
			g_clientExpire(unit, 0, list);  // 0x217fc0: update the client's cooldown display too
		}
	}
	return result;
}
auto ClientTwister(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	return Client(unit, skillId, level, g_twister);
}
auto ClientTornado(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	return Client(unit, skillId, level, g_tornado);
}

// D2R callbacks and clock, resolved independently of their build-93847 reference RVAs above.
sigscan::Entry ServerExpire {"GustServerExpire", "skills", sigscan::Kind::Code, 0, 0,
    "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 48 83 ec 40 49 8b f0 48 8b f9 e8 ?? ?? ?? ?? "
    "48 8b d6 48 8b cf 48 8b e8 44 8b 80 70 01 00 00"};
sigscan::Entry ClientExpire {"GustClientExpire", "skills", sigscan::Kind::Code, 0, 0,
    "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 49 8b f8 48 8b d9 e8 ?? ?? ?? ?? "
    "44 8b c0 48 8b d7 48 8b cb 8b f0"};
// The client callback's first call is its clock (0x08f220), not the OS clock.
sigscan::Entry ClientClock {"GustClientClock", "skills", sigscan::Kind::Rip, 22, 26, ClientExpire.pattern};

}  // namespace

auto InstallGustCooldown(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	sigscan::Entry* entries[] = {&ServerExpire, &ClientExpire, &ClientClock};
	sigscan::Resolve(entries, 3, context->exeBase, nullptr, 0);
	bool on = ServerExpire.state == sigscan::State::Ok && ClientExpire.state == sigscan::State::Ok &&
	          ClientClock.state == sigscan::State::Ok && d2r::sig::UnitStateList.state == sigscan::State::Ok &&
	          d2r::sig::ListStatValue.state == sigscan::State::Ok && d2r::sig::SetListStat.state == sigscan::State::Ok &&
	          d2r::sig::RemoveUnitEvent.state == sigscan::State::Ok;
	if (on) {
		g_serverExpire = reinterpret_cast<ExpireFn>(ServerExpire.address);
		g_clientExpire = reinterpret_cast<ExpireFn>(ClientExpire.address);
		g_clock = reinterpret_cast<ClockFn>(ClientClock.address);
		constexpr uint8_t ServerEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18};
		constexpr uint8_t ClientEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10};
		on = sigscan::HookBySignature(context, "GustSrvDo118",
		    "c7 44 24 30 00 00 00 00 44 8b cb 44 89 7c 24 28 44 8b c0 49 8b d6 89 6c 24 20 48 8b ce e8", -0x106,
		    ServerEntry, sizeof(ServerEntry), &Server, &g_server) &&
		    sigscan::HookBySignature(context, "GustCltDo68",
		    "c7 44 24 38 01 00 00 00 44 8b cd c7 44 24 30 00 00 00 00 44 8b c3 c7 44 24 28 00 01 00 00", -0x10f,
		    ClientEntry, sizeof(ClientEntry), &ClientTwister, &g_twister) &&
		    sigscan::HookBySignature(context, "GustCltDo69",
		    "c7 44 24 38 01 00 00 00 44 8b cd c7 44 24 30 00 00 00 00 44 8b c3 c7 44 24 28 00 02 00 00", -0x10f,
		    ClientEntry, sizeof(ClientEntry), &ClientTornado, &g_tornado);
	}
	g_on = on;
	plog::Info("gust-cooldown: Twister/Tornado reduction %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; gust-cooldown: %s", on ? "on" : "FAILED");
	}
	return on;
}

PD2R_RULE(InstallGustCooldown, 400)
