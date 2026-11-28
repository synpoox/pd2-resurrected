// Void knights fire their own DoomKnightMissile variant (#2231).
// PD2 srvdo 148 (PD2.gpr 0x103b0f00) and cltdo 94 (0x103ab180) fire the skill's missile + a variant: 0 / 1 / 2 / 3 for
// the poison, fire, cold and phys void knights, any other monster its S3 component byte, as 1.13c. D2R's srvdo 148
// (93847 0x57a5d0) and cltdo 94 (0x222dd0) read only that byte (MonsterData +0x12), and merged monstats2 gives each
// knight one S3 entry (pos, fir, col or unh), so the byte is 0 and all four fire poison. No table can force a component
// index (D2R rolls it over the S3v list), and no SDK service runs a skill's do function, so both slots are wrapped: the
// knight's byte is set to its variant around D2R's own function, then put back (it also picks the S3 art).
#include "common.h"
#include "registry.h"

#include "log.h"
#include "sig.h"

#include <cstdio>
#include <cstring>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;
using SrvDoFn = int64_t (*)(Game*, Unit*, int32_t, int32_t) noexcept;
using CltDoFn = int64_t (*)(Unit*, int32_t, int32_t) noexcept;

constexpr int SlotSrvDo = 148, SlotCltDo = 94;
constexpr int MonComponentS3 = 0x12;  // MonsterData's S3 byte, both functions' variant
// merged monstats ids of voidKnightPoison, voidKnightFire, voidKnightCold, voidKnightPhys (PD2 968, 969, 1106, 1107);
// tools/tests/test_void_knight_ids.py checks them by name
constexpr int32_t KnightPoison = 985, KnightFire = 986, KnightCold = 1123, KnightPhys = 1124;

constexpr auto Variant(int32_t classId) noexcept -> int {
	switch (classId) {
	case KnightPoison: return 0;
	case KnightFire: return 1;
	case KnightCold: return 2;
	case KnightPhys: return 3;
	default: return -1;  // keep the component byte
	}
}
static_assert(Variant(KnightPoison) == 0 && Variant(KnightFire) == 1 && Variant(KnightCold) == 2);
static_assert(Variant(KnightPhys) == 3 && Variant(987) == -1);  // voidKnightCorridor keeps its byte

// cltdo's dispatch: movsx rax,word [rbx+0x14c] (cltdofunc) / lea r9,[cltdo table] / mov r9,[r9+rax*8] / test r9,r9 / jz
constexpr const char* CltDoTablePattern = "48 0f bf 83 4c 01 00 00 4c 8d 0d ?? ?? ?? ?? 4d 8b 0c c1 4d 85 c9 0f 84";
constexpr int CltDoTableLea = 8;

SrvDoFn g_srvDo = nullptr;
CltDoFn g_cltDo = nullptr;

// The knight's S3 byte, or null for any other unit
auto KnightByte(Unit* unit, int* variant) noexcept -> uint8_t* {
	if (unit == nullptr || At<int32_t>(unit, d2r::rva::UnitType) != d2r::rva::UnitMonster) {
		return nullptr;
	}
	*variant = Variant(At<int32_t>(unit, d2r::rva::UnitClassId));
	auto* data = At<uint8_t*>(unit, d2r::rva::UnitData);
	return *variant >= 0 && data != nullptr ? data + MonComponentS3 : nullptr;
}

template <typename Call>
auto WithVariant(Unit* unit, Call call) noexcept -> int64_t {
	int variant = -1;
	uint8_t* s3 = KnightByte(unit, &variant);
	if (s3 == nullptr) {
		return call();
	}
	const uint8_t own = *s3;
	*s3 = static_cast<uint8_t>(variant);
	const int64_t ran = call();
	*s3 = own;
	return ran;
}

auto SrvDo(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	return WithVariant(unit, [&] { return g_srvDo(game, unit, skillId, level); });
}

auto CltDo(Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	return WithVariant(unit, [&] { return g_cltDo(unit, skillId, level); });
}

template <typename Fn>
auto WrapSlot(const D2RL::PluginContext* context, uint64_t slotRva, Fn& own, Fn function) noexcept -> bool {
	uint64_t current = 0;
	std::memcpy(&current, reinterpret_cast<const void*>(context->exeBase + slotRva), sizeof(current));
	own = reinterpret_cast<Fn>(current);
	return current != 0 && context->PatchWriteU64(slotRva, &current, sizeof(current), reinterpret_cast<uint64_t>(function));
}

auto CltDoTableRva(uint64_t exeBase, uint64_t* table) noexcept -> bool {
	uint64_t at = 0;
	if (sigscan::ScanUnique(exeBase, CltDoTablePattern, CltDoTableLea, &at) != 1) {
		return false;
	}
	int32_t rel = 0;
	std::memcpy(&rel, reinterpret_cast<const void*>(exeBase + at + 3), sizeof(rel));
	*table = at + 7 + static_cast<int64_t>(rel);
	return true;
}

}  // namespace

auto InstallVoidKnightMissile(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	uint64_t cltTable = 0;
	const bool server = d2r::sig::SrvDoTable.state == sigscan::State::Ok &&
	                    WrapSlot(context, d2r::sig::SrvDoTable.rva + uint64_t {SlotSrvDo} * 8, g_srvDo, &SrvDo);
	const bool client = CltDoTableRva(context->exeBase, &cltTable) &&
	                    WrapSlot(context, cltTable + uint64_t {SlotCltDo} * 8, g_cltDo, &CltDo);
	const char* state = server && client ? "on" : server ? "server only" : client ? "client only" : "FAILED";
	plog::Info("void-knight-missile: variant by class %s", state);
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; void-knight-missile: %s", state);
	}
	return server && client;
}

PD2R_RULE(InstallVoidKnightMissile, 460)
