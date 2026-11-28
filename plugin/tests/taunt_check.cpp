// Exercise the actual cast and callback with host engine doubles; no game required (#2221).
#include "../core-rules/skill/taunt.cpp"

#include <array>
#include <cstdlib>

namespace {
void Check(bool ok) { if (!ok) { std::abort(); } }
template <typename T> void Put(void* p, int offset, T value) {
	std::memcpy(static_cast<uint8_t*>(p) + offset, &value, sizeof(value));
}
std::array<uint8_t, 0x180> gameBytes {}, skillBytes {};
std::array<uint8_t, 0x1268> tables {};
std::array<std::array<uint8_t, 0x40>, 7> units {};
Game* game = reinterpret_cast<Game*>(gameBytes.data());
Unit* caster = reinterpret_cast<Unit*>(units[0].data());
int radius = 16, scans = 0, applied = 0, targets = 0, step = 0;
bool missingRow = false;
auto Skill(uint32_t, int32_t id) noexcept -> uint8_t* {
	Check(id == 137);
	return missingRow ? nullptr : skillBytes.data();
}
auto Tables(uint8_t) noexcept -> const uint8_t* { return tables.data(); }
auto Key(Unit* unit) noexcept -> int32_t { Check(unit == caster); return 42; }
auto Calc(uint32_t, Unit* unit, int32_t code, int32_t id, int32_t level) noexcept -> int32_t {
	Check(unit == caster && id == 137 && level == 12);
	return code == 1 ? 300 : code == 2 ? radius : code + level;
}
auto Room(Unit*) noexcept -> void* { return units.data(); }
auto Can(Game* g, Unit* unit) noexcept -> int32_t {
	Check(g == game);
	return At<int32_t>(unit, 8) != 3;
}
auto Curse(Unit* unit, d2r::CurseParams* c) noexcept -> int64_t {
	Check(c->game == game && c->caster == caster && c->skill == 137 && c->level == 12);
	Check(c->state == 28 && c->frames == 300 && c->key == 42 && c->pad10[0] == 1);
	Check(c->stats[0] == 10 && c->values[0] == 22 && c->stats[1] == 11 && c->values[1] == 23);
	for (int i = 2; i < 6; ++i) { Check(c->stats[i] == -1 && c->values[i] == -1); }
	for (int i = 0; i < 4; ++i) { Check(At<int32_t>(c, 0x60 + 4 * i) == 0); }
	Check(step == 0);
	if (At<int32_t>(unit, 8) == 4) { return 0; }
	++applied;
	step = 1;
	return 1;
}
void Clear(Unit*, Unit* target) noexcept { Check(step == 1 && target == nullptr); step = 2; }
void Remove(Game* g, Unit*, int32_t event, int32_t key) noexcept {
	Check(step == 2 && g == game && event == 2 && key == 0); step = 3;
}
void Add(Game* g, Unit*, int32_t event, int32_t frame, int32_t key, int32_t skill, int32_t state) noexcept {
	Check(step == 3 && g == game && event == 2 && frame == 101 && key == 0 && skill == 0 && state == 0); step = 4;
}
void Target(Unit* unit, Unit* target) noexcept {
	Check(step == 4 && target == caster && At<int32_t>(unit, 8) <= 2); step = 0; ++targets;
}
void Area(Game* g, Unit* unit, d2r::AreaCenter* center, int32_t range, uint32_t filter,
          int32_t (*callback)(d2r::AreaContext*, Unit*), void* arg, int32_t flag) noexcept {
	Check(g == game && unit == caster && center->room == units.data() && center->x == 0 && center->y == 0);
	Check(range == radius && filter == 34691 && flag == 1);
	++scans;
	d2r::AreaContext context {g, unit, 1, arg};
	Check(callback(&context, nullptr) == 0);
	// Engine area search supplies four eligible units; outside-radius and filtered units are never callbacks.
	for (int i = 1; i <= 4; ++i) {
		Check(callback(&context, reinterpret_cast<Unit*>(units[i].data())) == (i <= 2 ? 1 : 0));
	}
}
template <typename Fn> void Bind(sigscan::Entry& e, Fn fn) { e.address = reinterpret_cast<uint64_t>(fn); }
}

int main() {
	using namespace d2r::rva;
	for (int i = 0; i < 7; ++i) { Put(units[i].data(), 8, i); }
	Put(gameBytes.data(), GameFrame, 100);
	Put(tables.data(), TablesStateCount, uint64_t {200});
	Put(tables.data(), 0x1260, uint64_t {653});
	Put(skillBytes.data(), SkAuraTargetState, int16_t {28});
	Put(skillBytes.data(), SkAuraFilter, uint32_t {34691});
	Put(skillBytes.data(), SkAuraLenCalc, 1);
	Put(skillBytes.data(), SkAuraRangeCalc, 2);
	for (int i = 0; i < 6; ++i) {
		Put(skillBytes.data(), SkAuraStat1 + 2 * i, int16_t(i < 2 ? 10 + i : i == 2 ? 653 : -1));
		Put(skillBytes.data(), SkAuraStatCalc1 + 4 * i, 10 + i);
	}
	d2r::api.getSkillRecord = Skill;
	d2r::api.evalSkillCalc = Calc;
	Bind(d2r::sig::DataTables, Tables); Bind(d2r::sig::UnitStateKey, Key);
	Bind(d2r::sig::UnitRoom, Room); Bind(d2r::sig::AreaUnits, Area);
	Bind(CanTaunt, Can); Bind(d2r::sig::ApplyCurse, Curse); Bind(ClearAi, Clear);
	Bind(d2r::sig::RemoveUnitEvent, Remove); Bind(d2r::sig::AddUnitEvent, Add); Bind(SetTarget, Target);
	Check(SrvDoTaunt(game, caster, 137, 12) == 1 && scans == 1 && applied == 2 && targets == 2 && step == 0);
	radius = 0;
	Check(SrvDoTaunt(game, caster, 137, 12) == 1 && scans == 1);
	radius = -1;
	Check(SrvDoTaunt(game, caster, 137, 12) == 1 && scans == 1);
	Check(SrvDoTaunt(nullptr, caster, 137, 12) == 0 && SrvDoTaunt(game, nullptr, 137, 12) == 0);
	missingRow = true;
	Check(SrvDoTaunt(game, caster, 137, 12) == 0);
	missingRow = false;
	Put(skillBytes.data(), SkAuraTargetState, int16_t {200});
	Check(SrvDoTaunt(game, caster, 137, 12) == 0);
	Put(skillBytes.data(), SkAuraTargetState, int16_t {28});
	Put(skillBytes.data(), SkAuraStat1, int16_t {-1});
	Check(SrvDoTaunt(game, caster, 137, 12) == 0 && scans == 1 && targets == 2);
}
