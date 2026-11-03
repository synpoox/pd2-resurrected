// Binds d2r.h's function table to the running image.
#include "d2r.h"

#define NOMINMAX
#include <windows.h>

namespace d2r {

Api api {};
uint64_t imageBase = 0;

namespace {
template <typename Fn>
void BindOne(Fn& slot, uint64_t exeBase, uint64_t offset) noexcept {
	slot = reinterpret_cast<Fn>(exeBase + offset);
}
}  // namespace

auto ImageStampOf(uint64_t exeBase) noexcept -> uint32_t {
	if (exeBase == 0) {
		return 0;
	}
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exeBase);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
		return 0;
	}
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(exeBase + static_cast<uint64_t>(dos->e_lfanew));
	return nt->Signature == IMAGE_NT_SIGNATURE ? nt->FileHeader.TimeDateStamp : 0;
}

auto Bind(uint64_t exeBase) noexcept -> bool {
	if (ImageStampOf(exeBase) != rva::ImageStamp) {
		return false;
	}
	imageBase = exeBase;
	BindOne(api.getSkillRecord, exeBase, rva::GetSkillRecord);
	BindOne(api.evalSkillCalc, exeBase, rva::EvalSkillCalc);
	BindOne(api.skillTargetXY, exeBase, rva::SkillTargetXY);
	BindOne(api.createMissile, exeBase, rva::CreateMissile);
	BindOne(api.missileCount, exeBase, rva::MissileCount);
	BindOne(api.setUnitSkillFlag, exeBase, rva::SetUnitSkillFlag);
	BindOne(api.pickSkillMissile, exeBase, rva::PickSkillMissile);
	BindOne(api.unitDataFlag, exeBase, rva::UnitDataFlag);
	BindOne(api.clientSkillTargetXY, exeBase, rva::ClientSkillTargetXY);
	BindOne(api.clientCreateMissile, exeBase, rva::ClientCreateMissile);
	return true;
}

}  // namespace d2r
