// PD2's shrine rules in D2R (#1143, docs/research/w37-server-rules.md "Gem shrine"). D2R does none of them:
// - Shared boosters: shrine codes 6-15 run D2R's own effect on the user, then on every other player within a squared
//   distance of 450 of the user (PD2 0x1037d840), through D2R's player iterator.
// - Resist shrines (codes 8-11) add +10 to the matching max resist on the shrine's state list, as PD2 does.
// - Gem shrine (code 18): a stack of gems loses one gem and one upgraded gem is made; D2R's upgrade function replaces
//   the whole stack with the one upgraded gem. The no-gem path (a random chipped gem) is D2R's own. The Perfect Gem
//   Shrine announcement is cut (owner).
//
// How: D2R's shrine function table (16-byte rows: function, stat, state; called as fn(dataFlag, ctx, data) with
// data[0] = the code and ctx = {game, shrine object, user, ...}) takes our wrapper in the slots of those codes, written
// with the SDK's PatchWriteU64 (the almanac's way): no hook for the booster rules. The gem fix needs one inline hook,
// on the upgrade function (game, player, new gem index, old gem unit -> 1 on success), because the loop that picks the
// gem and the code it upgrades to is inside D2R's gem shrine, and the SDK has no shrine event or service. The hook acts
// only while the wrapper of code 18 runs, so no other caller of that function changes.
// Engine addresses are signatures: the three below are ours, the others are d2r::sig entries (resolved by d2r::Bind).
// The part is installed only when every entry resolved. The 93847 addresses in the comments are for reference only.
#include "shrine_rules.h"

#include "log.h"
#include "pd2_engine.h"
#include "shrine_math.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace {

using d2r::Game;
using d2r::Unit;
namespace sh = pd2::shrines;

// 93847: 0x1d3f1d0, D2R's shrine function table (rows of 16: function, stat int at +8, state int at +0xc)
sigscan::Entry ShrineTable {"ShrineTable", "shrines", sigscan::Kind::Rip, 3, 7,
                            "4c 8d 0d ?? ?? ?? ?? 4d 8b 0c c1 4d 85 c9 74 15"};
// 93847: 0x490d00, (game, callback(game, player, arg), arg): the callback for every live player of the game
sigscan::Entry PlayerIterate {"PlayerIterate", "shrines", sigscan::Kind::Code, 0, 0,
                              "48 89 5c 24 10 48 89 6c 24 18 48 89 74 24 20 57 41 56 41 57 48 83 ec 20 4c 8d b1 30 22 00 00"};
// 93847: 0x594b60, (game, player, new gem index, old gem unit or null) -> 1 when it made the gem; with an old gem it
// removes that item first
sigscan::Entry UpgradeGem {"UpgradeGem", "shrines", sigscan::Kind::Code, 0, 0,
                           "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8d 6c 24 98 48 81 ec 68 01 00 00 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 45 50 48 8b fa"};
constexpr uint8_t UpgradeGemEntry[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
                                       0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0x98};

sigscan::Entry* const Own[] = {&ShrineTable, &PlayerIterate, &UpgradeGem};
sigscan::Entry* const Used[] = {&d2r::sig::UnitCoords,    &d2r::sig::UnitId,       &d2r::sig::UnitStat,
                                &d2r::sig::SetUnitStat,   &d2r::sig::SendUnitStat, &d2r::sig::PlayerClient,
                                &d2r::sig::UnitStateList, &d2r::sig::SetListStat};

constexpr int Codes = 24;  // D2R's table length; the dispatcher calls no code of 0x18 and over
constexpr int RowSize = 16, RowState = 12;

using ShrineFn = void (*)(uint32_t dataFlag, void** ctx, const uint8_t* data) noexcept;
using UpgradeFn = uint32_t (*)(Game* game, Unit* player, int32_t gem, Unit* old) noexcept;
using PlayerFn = void (*)(Game* game, Unit* player, void* arg) noexcept;
using IterateFn = void (*)(Game* game, PlayerFn callback, void* arg) noexcept;

ShrineFn g_orig[Codes] = {};
UpgradeFn g_upgradeOriginal = nullptr;
thread_local bool g_inGemShrine = false;  // the game thread runs a shrine start to end
std::atomic<uint32_t> g_shared {0}, g_reached {0}, g_stacks {0};

auto TableRow(int code) noexcept -> const uint8_t* {
	return reinterpret_cast<const uint8_t*>(ShrineTable.address) + code * RowSize;
}

auto IdOf(Unit* unit) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(d2r::sig::UnitId)(unit, __FILE__, __LINE__);
}

// +10 on the shrine state's list: the state is the table row's, the list the one the booster just made on the unit
auto RaiseMaxResist(uint32_t flag, Unit* unit, int code) noexcept -> void {
	const int32_t stat = sh::MaxResistStat(code);
	if (stat == 0) {
		return;
	}
	int32_t state;
	std::memcpy(&state, TableRow(code) + RowState, sizeof(state));
	void* list = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(d2r::sig::UnitStateList)(unit, state);
	if (list == nullptr) {
		plog::Warn("shrines: code %d left no list of state %d on unit %d, max resist not raised", code, state, IdOf(unit));
		return;
	}
	d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(d2r::sig::SetListStat)(
		flag, list, stat, sh::MaxResistBonus, 0);
}

struct Reach {
	ShrineFn fn;
	uint32_t flag;
	void** ctx;
	const uint8_t* data;
	Unit* user;
	int32_t x, y;
	int code;
	int reached;
};

void ReachPlayer(Game*, Unit* player, void* arg) noexcept {
	auto* r = static_cast<Reach*>(arg);
	if (player == r->user) {
		return;
	}
	int32_t xy[2] = {0, 0};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(d2r::sig::UnitCoords)(player, xy);
	if (!sh::InShareRange(r->x, r->y, xy[0], xy[1])) {
		return;
	}
	void* copy[5];  // the 0x28-byte shrine context with the other player as the user
	std::memcpy(copy, r->ctx, sizeof(copy));
	copy[2] = player;
	r->fn(r->flag, copy, r->data);
	RaiseMaxResist(r->flag, player, r->code);
	++r->reached;
	plog::Info("shrines: code %d reached unit %d at (%d,%d)", r->code, IdOf(player), xy[0], xy[1]);
}

void Shrine(uint32_t flag, void** ctx, const uint8_t* data) noexcept {
	const int code = data[0];
	const ShrineFn fn = code < Codes ? g_orig[code] : nullptr;
	if (fn == nullptr) {
		return;
	}
	if (code == sh::GemShrine) {
		g_inGemShrine = true;
		fn(flag, ctx, data);
		g_inGemShrine = false;
		return;
	}
	auto* game = static_cast<Game*>(ctx[0]);
	auto* user = static_cast<Unit*>(ctx[2]);
	fn(flag, ctx, data);
	RaiseMaxResist(flag, user, code);
	Reach r {fn, flag, ctx, data, user, 0, 0, code, 0};
	int32_t xy[2] = {0, 0};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(d2r::sig::UnitCoords)(user, xy);
	r.x = xy[0];
	r.y = xy[1];
	d2r::Engine<IterateFn>(PlayerIterate)(game, ReachPlayer, &r);
	++g_shared;
	g_reached += static_cast<uint32_t>(r.reached);
	plog::Info("shrines: code %d used by unit %d at (%d,%d), reached %d other players", code, IdOf(user), xy[0], xy[1],
	           r.reached);
}

// With an old gem that is a stack of more than one, D2R's upgrade would remove the whole stack: make the upgraded gem
// alone, then take one off the stack and tell the owner's client.
auto UpgradeHook(Game* game, Unit* player, int32_t gem, Unit* old) noexcept -> uint32_t {
	if (!g_inGemShrine || old == nullptr) {
		return g_upgradeOriginal(game, player, gem, old);
	}
	const int32_t quantity = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat)(
		old, d2r::rva::StatQuantity, 0);
	if (quantity <= 1) {
		return g_upgradeOriginal(game, player, gem, old);
	}
	const uint32_t made = g_upgradeOriginal(game, player, gem, nullptr);
	if (made == 0) {
		plog::Warn("shrines: gem shrine could not make the upgraded gem, the stack of %d stays", quantity);
		return 0;
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::sig::SetUnitStat)(
		old, d2r::rva::StatQuantity, quantity - 1, 0);
	if (void* client = d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::PlayerClient)(player)) {
		d2r::Engine<void (*)(void*, Unit*, int32_t, uint32_t, int32_t, uint32_t) noexcept>(d2r::sig::SendUnitStat)(
			client, old, 1, static_cast<uint32_t>(d2r::rva::StatQuantity), quantity - 1, 0);
	}
	++g_stacks;
	plog::Info("shrines: gem shrine took one gem off a stack of %d, made one upgraded gem for unit %d", quantity,
	           IdOf(player));
	return made;
}

auto Unresolved(char* report, size_t size) noexcept -> bool {
	for (sigscan::Entry* e : Own) {
		if (e->state != sigscan::State::Ok) {
			std::snprintf(report, size, "off: signature %s %s", e->name, sigscan::StateText(e->state));
			return true;
		}
	}
	for (sigscan::Entry* e : Used) {
		if (e->state != sigscan::State::Ok) {
			std::snprintf(report, size, "off: signature %s %s", e->name, sigscan::StateText(e->state));
			return true;
		}
	}
	return false;
}

}  // namespace

auto InstallShrines(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	sigscan::Resolve(Own, std::size(Own), context->exeBase, nullptr, 0);
	if (Unresolved(report, reportSize)) {
		return 0;
	}
	const bool hook = context->InstallInlineHook(UpgradeGem.rva, UpgradeGemEntry, sizeof(UpgradeGemEntry), UpgradeHook,
	                                             &g_upgradeOriginal);
	int live = 0, total = 0;
	for (int code = sh::FirstBooster; code <= sh::GemShrine; ++code) {
		if (!sh::IsBooster(code) && code != sh::GemShrine) {
			continue;
		}
		++total;
		uint64_t original;
		std::memcpy(&original, TableRow(code), sizeof(original));
		if (original == 0 || (code == sh::GemShrine && !hook)) {
			continue;
		}
		g_orig[code] = reinterpret_cast<ShrineFn>(original);
		if (context->PatchWriteU64(ShrineTable.rva + static_cast<uint64_t>(code) * RowSize, &original, sizeof(original),
		                           reinterpret_cast<uint64_t>(&Shrine))) {
			++live;
		} else {
			// put the slots already written back, so no code runs half the rules
			g_orig[code] = nullptr;
			for (int done = sh::FirstBooster; done < code; ++done) {
				if (g_orig[done] != nullptr) {
					const uint64_t wrapper = reinterpret_cast<uint64_t>(&Shrine), back = reinterpret_cast<uint64_t>(g_orig[done]);
					if (!context->PatchWriteU64(ShrineTable.rva + static_cast<uint64_t>(done) * RowSize, &wrapper, sizeof(wrapper), back)) {
						plog::Error("shrines: slot of code %d could not be put back", done);
					}
					g_orig[done] = nullptr;
				}
			}
			std::snprintf(report, reportSize, "off: slot of shrine code %d could not be written, slots put back", code);
			return 0;
		}
	}
	std::snprintf(report, reportSize, "shrine slots %d/%d (shared 6-15, +10 max resist 8-11, gem stack 18)%s", live, total,
	              hook ? "" : " (gem hook FAILED)");
	return live == total ? 1 : 0;
}

auto ShrineCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "shrines shared %u, players reached %u, gems off a stack %u", g_shared.load(),
	              g_reached.load(), g_stacks.load());
}
