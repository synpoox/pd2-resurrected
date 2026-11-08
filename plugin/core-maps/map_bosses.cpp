// The map boss swap (#614). PD2's maps put their boss on a placeholder preset, lesserhydra1 (and lesserhydra2/3 for a
// few levels), and PD2's preset rewrite 0x1038bfc0 calls 0x1038b990, which rewrites the preset's monster id by level
// before the preset spawns: Zhar and his mini-bosses, the Library map's doomknight3LibraryBoss and about 40 more.
// 0x1038bfc0's own branches do the same for Ureh (levels 195, 196) and the Imperial Palace's boss room (194); they are
// here too. A rewritten preset then spawns as any monster preset, and slot 5's extra boss (map_stats.cpp) sees the
// boss's id. Uber Tristram (185) rolls its primes on the placeholders instead: each prime once per game.
// No PluginSDK service spawns or reports room presets: this runs from map_stats.cpp's preset spawn hook.
// Ids are the port's (PD2 levels -> merge/ids-levels.tsv, PD2 monsters by monstats Id); PD2's in the comments.
#include "map_bosses.h"

#include "log.h"

#include <intrin.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace {

using d2r::At;
using d2r::Game;

// lesserhydra1-3 (PD2 0x2e2-0x2e4) and monhydra1 (PD2 0x312)
constexpr int32_t PlaceholderA = 755, PlaceholderB = 756, PlaceholderC = 757, PlaceholderMon = 803;
constexpr int32_t Drop = -1;  // the preset does not spawn (0x1038b990 returns 0)

struct Swap {
	int16_t level;
	int16_t placeholder;
	int16_t boss;  // or Drop
};

// 0x1038b990's switch, by PD2 level (port level): PD2 monster -> port monster
constexpr Swap Swaps[] = {
	{140, PlaceholderA, 900},   // 139 Siege Map: 883 SiegeBoss
	{142, PlaceholderA, 901},   // 141 Sewers: 884 SewerBoss
	{143, PlaceholderA, 817},   // 142 Arcane: 800 ArcaneBoss
	{144, PlaceholderA, 767},   // 143 Kurast: 750 unravelerboss
	{145, PlaceholderA, 772},   // 144 River Of Blood: 755 baalminionboss
	{145, PlaceholderB, 770},   //   and 753 griswoldmap
	{146, PlaceholderA, 763},   // 145 Lava: 746 megademonboss
	{147, PlaceholderA, 878},   // 146 Ice Map: 861 IceBoss
	{148, PlaceholderA, 981},   // 147 Desert Map: 964 MarketBoss
	{149, PlaceholderA, 896},   // 148 Jungle Map: 879 torajanBoss
	{150, PlaceholderA, 826},   // 149 Fortress Map: 809 BastionBoss
	{151, PlaceholderA, 899},   // 150 Throne Map: 882 ThroneBoss
	{152, PlaceholderA, 887},   // 151 Tomb Map: 870 TombBoss
	{153, PlaceholderA, 760},   // 152 Monastery Map: 743 willowispboss
	{154, PlaceholderA, 764},   // 153 Monastery Map Basement: 747 fingermageboss
	{155, PlaceholderA, 843},   // 154 Graveyard Map: 826 CowBoss
	{156, PlaceholderA, 910},   // 155 Caldeum Map: 893 act2hireTraitorBoss
	{157, PlaceholderA, 911},   // 156 Pandemonium Map: 894 archerBoss
	{158, PlaceholderA, 932},   // 158 Spider Map: 915 spiderboss
	{159, PlaceholderA, 980},   // 160 Frozen Forest Map: 963 CanyonBoss
	{164, PlaceholderA, 764},   // 165 Hole of Terror: 747 fingermageboss
	{165, PlaceholderA, 1014},  // 167 Library Map: 997 doomknight3LibraryBoss
	{166, PlaceholderA, 1013},  // 169 Westmarch Map: 996 westmarchMapBoss
	{167, PlaceholderA, 1015},  // 170 Crypts Map: 998 leoricMapBoss
	{169, PlaceholderA, 1017},  // 172 Black Abyss Map: 1000 siegebeastMapBoss
	{170, PlaceholderA, 1043},  // 174 Ruined Cistern Map: 1026 lernaeanhydra2
	{171, PlaceholderA, 1042},  // 175 Ashen Plains Map: 1025 AshenBoss
	{172, PlaceholderA, 1075},  // 176 Zhar Library: 1058 ZharTheMad
	{173, PlaceholderA, 1079},  // 177 Zhar Rivers: 1062 ZharMiniBossBaboon
	{174, PlaceholderA, 1080},  // 178 Zhar Ice: 1063 ZharMiniBossCantor
	{175, PlaceholderA, 1078},  // 179 Zhar Kurast: 1061 ZharMiniBossBigHead
	{177, PlaceholderA, 1109},  // 181 Hellcaves: 1092 WarlordMiniBossShaman
	{178, PlaceholderA, 1077},  // 182 Hellcave Fortress: 1060 WarlordOfBlood
	{178, PlaceholderB, 1074},  //   1057 demonpillar
	{178, PlaceholderC, 1110},  //   1093 WarlordMiniBossDefiler
	{179, PlaceholderA, 1113},  // 183 Fallen Gardens: 1096 Rakanoth
	{179, 908, Drop},           //   891 dcloneskmage_pois does not spawn
	{180, PlaceholderA, 1111},  // 184 Diamond Gate: 1094 Iskatu
	{183, PlaceholderA, 1120},  // 187 Kanemith Dungeon: 1103 KanemithBoss
	// 192 Imperial Palace Map: 1148 ImperialPalaceMiniBoss. 0x1038bfc0 takes it before 0x1038b990 (whose case is dead
	// code): it spawns him on a free spot (0x1038a0d0), keeps him in game +0x2600 and returns 0 (no extra boss; 1165 is
	// not one anyway). shortcut: here the preset spawns him
	{188, PlaceholderA, 1165},
	{189, PlaceholderA, 1166},  // 193 Halls of Torture Map: 1149 TortureHallsBoss
	{193, PlaceholderA, 1212},  // 197 Djinns Domain Map: 1195 DjinnLightBoss
	{194, PlaceholderA, 1213},  // 198 Djinns Domain Red Map: 1196 DjinnDarkBoss
	{195, PlaceholderA, 1212},  // 199 Djinns Domain Boss Map: 1195 DjinnLightBoss
	{196, PlaceholderA, 1211},  // 200 Na-Krul's Abyss Map: 1194 NaKrulBoss
	{197, PlaceholderA, 1209},  // 201 Kyovoshad Map: 1192 KyovoshadBoss
	{198, PlaceholderA, 1256},  // 207 Tomb Event Map: 1241 vilechild1Riverofblood
	{199, PlaceholderA, 1257},  // 208 Poisoned Well Map: 1242 DarkTempleBoss
	{200, PlaceholderA, 1255},  // 209 Dark Temple Map: 1240 succubus4Ureh
	{201, PlaceholderA, 1258},  // 210 Descent Map: 1243 TombEventBoss
	// 0x1038bfc0's branches before 0x1038b990: 195 Ureh City Map, 196 Nemyr Monastery Map
	{191, PlaceholderA, 1188},  // 1171 UrehHydra
	{191, PlaceholderB, 1189},  // 1172 UrehRanger
	{191, PlaceholderC, 1192},  // 1175 CityofUrehMiniBoss
	{192, PlaceholderA, 1191},  // 1174 CityofUrehBoss
};

// 194 Imperial Palace Boss Room: 0x1038bfc0 spawns these itself (0x1038a060, 0x1037cd00) and returns 0, so they get no
// extra boss. shortcut: PD2 also keeps them in game +0x2600-0x260c, which only ImperialPalaceBoss's death reads (to
// clear its own entry); the port keeps nothing
constexpr int32_t ImperialBossRoom = 190;
constexpr Swap ImperialSwaps[] = {
	{ImperialBossRoom, PlaceholderA, 1185},    // 1168 ImperialPalaceBossJerhyn
	{ImperialBossRoom, PlaceholderB, 1183},    // 1166 ImperialPalaceBoss
	{ImperialBossRoom, PlaceholderC, 1181},    // 1164 ImperialPalaceBossMinion
	{ImperialBossRoom, PlaceholderMon, 1181},  // 1164 ImperialPalaceBossMinion
};

// 185 Uber Tristram (0xb9), with the game's area 0x29: lesserhydra1 rolls 1 in 20 for each prime not yet placed, in
// order, and lesserhydra2 takes the first one not placed; a placeholder that places none does not spawn.
constexpr int32_t UberTristram = 181;
struct Prime {
	int32_t classId;
	int32_t bit;
};
constexpr Prime Primes[] = {{704, 2}, {705, 4}, {709, 8}};  // ubermephisto, uberdiablo, uberbaal
// PD2 keeps the bits in game +0x2628, which its Tristram opening (0x10383060) sets to 0 after the warp. The port's
// +0x2628 ('UBRE', map_open.cpp) holds the other ubers' entries, so the bits get their own counter: a preset spawned
// during the opening's warp keeps its bit
constexpr uint32_t PrimesKey = 0x55425442;  // 'UBTB'

std::atomic<uint32_t> g_swapped {0};
std::atomic<uint32_t> g_dropped {0};
std::atomic<uint32_t> g_primes {0};

// PD2 rolls with the CRT's rand(), as map_events.cpp
auto Rand() noexcept -> int32_t {
	static thread_local const bool seeded = (std::srand(static_cast<unsigned>(__rdtsc())), true);
	(void)seeded;
	return std::rand();
}

auto Counter(Game* game, uint32_t key) noexcept -> int32_t* {
	return d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

template <size_t N>
auto Find(const Swap (&swaps)[N], int32_t level, int32_t id) noexcept -> const Swap* {
	for (const Swap& s : swaps) {
		if (s.level == level && s.placeholder == id) {
			return &s;
		}
	}
	return nullptr;
}

// The prime a Tristram placeholder becomes, or Drop. shortcut: PD2 also needs the game's area to be Uber Tristram's
// (game +0x1df4 == 0x29), which the port does not keep; level 181 opens only from the Pandemonium Talisman (or a
// `cheat goto`).
auto TristramPrime(Game* game, int32_t id) noexcept -> int32_t {
	if (id != PlaceholderA && id != PlaceholderB) {
		return 0;
	}
	int32_t* placed = Counter(game, PrimesKey);
	if (placed == nullptr) {
		return Drop;
	}
	for (const Prime& p : Primes) {
		if ((*placed & p.bit) == 0 && (id == PlaceholderB || Rand() % 20 < 1)) {
			*placed |= p.bit;
			return p.classId;
		}
	}
	return Drop;
}

}  // namespace

auto MapBossPreset(Game* game, void* room, uint8_t* preset, void (*spawn)(Game*, void*, const uint8_t*) noexcept) noexcept
	-> bool {
	using namespace d2r::rva;
	if (game == nullptr || room == nullptr || preset == nullptr || At<int32_t>(preset, PresetType) != UnitMonster) {
		return false;
	}
	const int32_t level = d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room);
	const int32_t id = At<int32_t>(preset, PresetId);
	const int32_t x = At<int32_t>(room, RoomX) + At<int32_t>(preset, PresetX);
	const int32_t y = At<int32_t>(room, RoomY) + At<int32_t>(preset, PresetY);
	int32_t boss = 0;
	bool spawnHere = false;
	if (level == UberTristram) {
		boss = TristramPrime(game, id);
		g_primes += boss > 0 ? 1 : 0;
	} else if (const Swap* s = Find(ImperialSwaps, level, id); s != nullptr) {
		boss = s->boss;
		spawnHere = true;
	} else if (const Swap* t = Find(Swaps, level, id); t != nullptr) {
		boss = t->boss;
	}
	if (boss == 0) {
		return false;
	}
	if (boss == Drop) {
		plog::Info("map bosses: level %d placeholder %d at (%d, %d) dropped (%u)", level, id, x, y, ++g_dropped);
		return true;
	}
	*reinterpret_cast<int32_t*>(preset + PresetId) = boss;
	plog::Info("map bosses: level %d placeholder %d at (%d, %d) became %d (%u)", level, id, x, y, boss, ++g_swapped);
	if (spawnHere) {
		spawn(game, room, preset);
		return true;
	}
	return false;
}

auto MapBossCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "map bosses: %u placeholders swapped (%u Tristram primes), %u dropped", g_swapped.load(),
	              g_primes.load(), g_dropped.load());
}
