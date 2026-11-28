#include "../core-rules/skill/ownership.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace pd2r::skills;
using namespace d2r::rva;

struct Monster {
	alignas(8) uint8_t unit[0x1c0] = {}, data[0xb0] = {}, ai[0x80] = {};
	Monster(bool server = true) {
		put(unit, UnitType, UnitMonster);
		put(unit, UnitData, data);
		put(unit, UnitOwnerType, -1);
		put(unit, UnitOwnerId, UINT32_MAX);
		put(unit, UnitExtendedFlags, server ? ServerUnit : 0);
		put(data, MonDataAi, ai);
		put(ai, AiOwnerType, -1);
		put(ai, AiOwnerId, UINT32_MAX);
	}
	template <typename T> static void put(uint8_t* dst, int at, T value) { std::memcpy(dst + at, &value, sizeof(value)); }
	auto get() const -> const Unit* { return reinterpret_cast<const Unit*>(unit); }
	void fieldOwner(int type, uint32_t id) { put(unit, UnitOwnerType, type); put(unit, UnitOwnerId, id); }
	void aiOwner(int type, uint32_t id) { put(ai, AiOwnerType, type); put(ai, AiOwnerId, id); }
};

auto ClientOwner(uint32_t pet) noexcept -> uint32_t { return pet == 17 ? 7 : UINT32_MAX; }

int main() {
	int failed = 0, checked = 0;
	const auto check = [&](const char* name, bool ok) { ++checked; if (!ok) { ++failed; std::printf("FAIL %s\n", name); } };
	Monster server, client(false), parent, child;
	check("null and unowned", !OwnedByPlayer(nullptr) && !OwnedByPlayer(server.get()));
	for (uint32_t id : {0u, UINT32_MAX}) {
		server.fieldOwner(0, id); server.aiOwner(0, id);
		check("unset player IDs", !OwnedByPlayer(server.get()));
	}
	server.fieldOwner(0, 7);
	check("unit-field owner", OwnedByPlayer(server.get()));
	server.fieldOwner(-1, UINT32_MAX); server.aiOwner(0, 7);
	check("AI-only player owner (also retained on dead corpses)", OwnedByPlayer(server.get()));
	Monster::put(server.unit, UnitMode, MonModeDead);
	check("dead AI-only corpse", OwnedByPlayer(server.get()));
	server.aiOwner(UnitMonster, 7);
	check("monster owner is not a direct player", !OwnedByPlayer(server.get()));
	Monster::put(client.data, MonDataAi, uint64_t{0xffffffffffff0000});
	Monster::put(client.unit, UnitGame, uint64_t{123});
	check("exact client sentinel with nonzero serial", MonsterAiOwner(client.get()) == nullptr && !OwnedByPlayer(client.get()));
	client.fieldOwner(0, 7);
	check("client unit-field player", OwnedByPlayer(client.get()));
	client.fieldOwner(UnitMonster, 9);
	parent.aiOwner(0, 7); child.aiOwner(UnitMonster, 9);
	int lookups = 0;
	const auto find = [&](const Unit*, uint32_t id) -> const Unit* { ++lookups; return id == 9 ? parent.get() : nullptr; };
	check("nested AI-only ownership", PlayerOwnedMonster(child.get(), find) && lookups == 1);
	check("client nested unit ownership", PlayerOwnedMonster(client.get(), find));
	parent.fieldOwner(0, 7); parent.aiOwner(-1, UINT32_MAX); child.fieldOwner(UnitMonster, 9);
	check("nested unit-field ownership", PlayerOwnedMonster(child.get(), find));
	child.fieldOwner(UnitMonster, 10); child.aiOwner(UnitMonster, 9);
	check("missing unit owner falls back to AI", PlayerOwnedMonster(child.get(), find));
	parent.fieldOwner(UnitMonster, 9); parent.aiOwner(UnitMonster, 9);
	lookups = 0;
	check("cycle is bounded", !PlayerOwnedMonster(child.get(), find) && lookups <= 4);
	client.fieldOwner(-1, UINT32_MAX); Monster::put(client.unit, UnitIdField, 17u);
	check("roster-only client pet (live golem/skeleton layout)", OwnedByPlayer(client.get(), &ClientOwner));
	Monster::put(client.unit, UnitIdField, 18u);
	check("unrostered client monster", !OwnedByPlayer(client.get(), &ClientOwner));
	Monster clientParent(false);
	Monster::put(clientParent.data, MonDataAi, uint64_t{0xffffffffffff0000});
	Monster::put(clientParent.unit, UnitIdField, 17u);
	client.fieldOwner(UnitMonster, 9);
	check("nested client roster owner", PlayerOwnedMonster(client.get(), [&](const Unit*, uint32_t) { return clientParent.get(); }, &ClientOwner));
	server.fieldOwner(-1, UINT32_MAX); server.aiOwner(-1, UINT32_MAX); Monster::put(server.unit, UnitIdField, 17u);
	check("server never consults client roster", !OwnedByPlayer(server.get(), &ClientOwner));
	Monster::put(server.unit, UnitData, static_cast<uint8_t*>(nullptr));
	check("missing monster data", MonsterAiOwner(server.get()) == nullptr);
	Monster::put(parent.data, MonDataAi, static_cast<uint8_t*>(nullptr));
	check("missing AI block", MonsterAiOwner(parent.get()) == nullptr);
	Monster::put(child.unit, UnitType, 3);
	Monster::put(child.unit, UnitData, reinterpret_cast<uint8_t*>(1));
	check("nonmonster never reads AI", MonsterAiOwner(child.get()) == nullptr && !PlayerOwnedMonster(child.get(), find));
	std::printf("ownership: %d checked, %d failed\n", checked, failed);
	return failed != 0;
}
