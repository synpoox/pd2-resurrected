// A quest drop stops for everyone once the quest is done in the game (#2023).
//
// PD2: the monster TC drop (1.13c D2Game +0x75680) tests the killer's quest record before it drops the quest TC:
// D2Common #10174 (record, monstats Quest, flag) for flags 15, 1 and the monster's QuestFlag (monstats +0x9e Quest,
// +0x9f QuestFlag); any set skips the quest TC. PD2 sends the first call (+0x75850, flag 15) through its thunk
// 0x103a44d0 (installed) to 0x10373fe0 (PD2.gpr 0x10373f70) with the game and the monstats row: 1 when the killer's
// record has flag 15, or the game's quest record (game +0x10f4 -> +0xc) has flag 15, flag 1 or the QuestFlag. With no
// game record it falls back to the killer's flag 15. So the farm of a quest drop by players who have not done the
// quest stops once anyone in the game has.
//
// D2R: MonsterTcDrop (93847 0x447b80) tests only the killer's record, flags 15, 1, 0 and QuestFlag (QuestFlag
// 0x325c50 at 0x447d77, 0x447d91, 0x447da8, 0x447dc4; game in r14, monstats in rsi). The game's record is the game's
// quest control (+0x21d8) +0x10, as D2R's own game-flag test 0x514fd0 reads it. The calls for flags 15, 1 and QuestFlag
// are redirected to the rule through a stub that passes r14 (the game) as a 4th argument: each also reads the game's
// record for its own flag. The flag 0 call (D2R's own) stays stock, as PD2 has no game test for it. Not a hook on
// MonsterTcDrop's entry: Cartographer hooks it (event 6b drop).
// Drops run on the host (server), so TCP/IP clients get the same rule.
#include "../item_rules.h"
#include "registry.h"

#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

// MonsterTcDrop's killer quest test: movzx ecx,[r14+0x104] (difficulty) / mov rbx,[rax+rcx*8+0x40] (the record) /
// test / je / then QuestFlag(rbx, monstats [rsi+0xf0], 15 | 1 | 0 | monstats [rsi+0xf1]), each followed by cmp eax,1 / je
constexpr const char* QuestTestPattern =
    "41 0f b6 8e 04 01 00 00 48 8b 5c c8 40 48 85 db 74 ?? 0f b6 96 f0 00 00 00 41 b8 0f 00 00 00 48 8b cb e8 ?? ?? ?? ?? "
    "83 f8 01 74 ?? 0f b6 96 f0 00 00 00 41 b8 01 00 00 00 48 8b cb e8 ?? ?? ?? ?? 83 f8 01 74 ?? 0f b6 96 f0 00 00 00 "
    "45 33 c0 48 8b cb e8 ?? ?? ?? ?? 83 f8 01 74 ?? 44 0f b6 86 f1 00 00 00 48 8b cb 0f b6 96 f0 00 00 00 e8 ?? ?? ?? ?? "
    "83 f8 01";
constexpr uint32_t Flag15CallAt = 34, Flag1CallAt = 60, QuestFlagCallAt = 111;  // the flag 0 call (+83) stays stock
constexpr int GameQuestControl = 0x21d8;  // quest control (1.13c game +0x10f4)
constexpr int QuestControlRecord = 0x10;  // the game's quest record (1.13c +0xc)

using QuestFlagFn = int32_t (*)(void* record, int32_t quest, int32_t flag) noexcept;
QuestFlagFn g_questFlag = nullptr;

auto GameRecord(const uint8_t* game) noexcept -> void* {
	const auto* control = game != nullptr ? *reinterpret_cast<uint8_t* const*>(game + GameQuestControl) : nullptr;
	return control != nullptr ? *reinterpret_cast<void* const*>(control + QuestControlRecord) : nullptr;
}

// QuestFlag at the three call sites, the game from the stub. 1 skips the quest TC, as the site's cmp eax,1 reads it.
auto QuestDone(void* record, int32_t quest, int32_t flag, const uint8_t* game) noexcept -> int32_t {
	const int32_t killer = g_questFlag(record, quest, flag);
	if (killer == 1) {
		return 1;
	}
	void* const gameRecord = GameRecord(game);
	if (gameRecord == nullptr || g_questFlag(gameRecord, quest, flag) == 0) {
		return killer;
	}
	return 1;
}

// mov r9,r14 (the game) / mov rax,&QuestDone / jmp rax
auto MakeStub() noexcept -> uint64_t {
	uint8_t code[15] = {0x4d, 0x8b, 0xce, 0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
	const uint64_t entry = reinterpret_cast<uint64_t>(&QuestDone);
	std::memcpy(code + 5, &entry, 8);
	void* page = VirtualAlloc(nullptr, sizeof(code), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	DWORD old;
	if (page == nullptr) {
		return 0;
	}
	std::memcpy(page, code, sizeof(code));
	if (VirtualProtect(page, sizeof(code), PAGE_EXECUTE_READ, &old) == 0) {
		VirtualFree(page, 0, MEM_RELEASE);
		return 0;
	}
	return reinterpret_cast<uint64_t>(page);
}

}  // namespace

auto InstallQuestDropGame(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const uint64_t stub = MakeStub();
	uint64_t original = 0;
	// shortcut: a failure after the first redirect leaves the earlier sites on; each still behaves as stock or as PD2
	const bool on = stub != 0 &&
	                RedirectCall(context, QuestTestPattern, Flag15CallAt, stub, reinterpret_cast<uint64_t*>(&g_questFlag)) &&
	                RedirectCall(context, QuestTestPattern, Flag1CallAt, stub, &original) &&
	                RedirectCall(context, QuestTestPattern, QuestFlagCallAt, stub, &original);
	plog::Info("quest-drop: game quest record check %s", on ? "on" : "FAILED (signature, bytes or stub)");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; quest-drop game check%s", on ? "" : " FAILED");
	}
	return on;
}

PD2R_RULE(InstallQuestDropGame, 190)
