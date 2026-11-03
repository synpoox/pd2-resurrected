// The game functions core plugins call, bound from the per-build header. Calling convention: plain Windows x64.
// Unit/Game are opaque; read fields with At<T>.
//
// Build check (ADR 0004): Bind() refuses an image whose PE stamp is not rva::ImageStamp, and the plugin then leaves
// every engine part off. Table writes also carry an expected value (empty slots) as a second line of defence.
#pragma once

#include "rvas_93847.h"

#include <cstdint>

namespace d2r {

struct Game;
struct Unit;

using GetSkillRecordFn = uint8_t* (*)(uint32_t dataFlag, int32_t skillId) noexcept;
using EvalSkillCalcFn = int32_t (*)(uint32_t dataFlag, Unit* unit, int32_t calc, int32_t skillId, int32_t level) noexcept;
using SkillTargetXYFn = int32_t (*)(Game* game, Unit* unit, int32_t* x, int32_t* y) noexcept;
using CreateMissileFn = Unit* (*)(Game* game, MissileParams* params) noexcept;
using MissileCountFn = int32_t (*)(uint32_t dataFlag) noexcept;
using SetUnitSkillFlagFn = void (*)(Unit* unit, int32_t on) noexcept;
using PickSkillMissileFn = int32_t (*)(Unit* unit, int32_t skillId) noexcept;
using UnitDataFlagFn = uint8_t (*)(Unit* unit) noexcept;
using ClientSkillTargetXYFn = int32_t (*)(Unit* unit, int32_t* x, int32_t* y) noexcept;
using ClientCreateMissileFn = Unit* (*)(MissileParams* params, int32_t zero) noexcept;

struct Api {
	GetSkillRecordFn getSkillRecord;
	EvalSkillCalcFn evalSkillCalc;
	SkillTargetXYFn skillTargetXY;
	CreateMissileFn createMissile;
	MissileCountFn missileCount;
	SetUnitSkillFlagFn setUnitSkillFlag;
	PickSkillMissileFn pickSkillMissile;
	UnitDataFlagFn unitDataFlag;
	ClientSkillTargetXYFn clientSkillTargetXY;
	ClientCreateMissileFn clientCreateMissile;
};
extern Api api;
// TimeDateStamp of the PE image at exeBase (0 when there is none).
auto ImageStampOf(uint64_t exeBase) noexcept -> uint32_t;
// Binds api when the image is the one rvas_*.h describes; false (api left null) on any other image.
auto Bind(uint64_t exeBase) noexcept -> bool;

template <typename T>
inline auto At(const void* p, int offset) noexcept -> T {
	return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(p) + offset);
}

inline auto DataFlag(Game* game) noexcept -> uint32_t {
	return At<uint8_t>(game, rva::GameDataFlag);
}

// A unit's position in subtiles, read the way rva::SkillTargetXY reads it.
inline auto UnitXY(Unit* unit, int32_t* x, int32_t* y) noexcept -> bool {
	const int32_t type = At<int32_t>(unit, rva::UnitType);
	const uint8_t* path = At<const uint8_t*>(unit, rva::UnitPath);
	if (path == nullptr) {
		return false;
	}
	if (type == 2 || type == 4 || type == 5) {
		*x = At<int32_t>(path, 0x10);
		*y = At<int32_t>(path, 0x14);
	} else {
		*x = At<uint16_t>(path, 0x2);
		*y = At<uint16_t>(path, 0x6);
	}
	return true;
}

// Evaluate one of the skill record's calc columns (field = rva::SkCalc1, ...) for this unit and level.
inline auto EvalCalc(Game* game, Unit* unit, const uint8_t* skill, int field, int32_t skillId, int32_t level) noexcept
	-> int32_t {
	return api.evalSkillCalc(DataFlag(game), unit, At<int32_t>(skill, field), skillId, level);
}

inline auto ValidMissile(uint32_t dataFlag, int32_t missile) noexcept -> bool {
	return missile >= 0 && missile < api.missileCount(dataFlag);
}

}  // namespace d2r
