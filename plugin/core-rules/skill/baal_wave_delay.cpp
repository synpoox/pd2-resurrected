// Baal's throne: a wave comes 125 frames after Baal's taunt, as in PD2 (#2014).
//
// PD2's one patch is 1.13c D2Game +0xa8534 (`add ecx, 0xfa` -> 0x7d) in the BaalThrone AI (monai 134, +0xa8430). D2R's
// AI is 93847 0x5c7075; its `add ecx, 0xfa` at 0x5c71e6 sits in the stage's first branch (the AI's flag 1 not yet
// set): it plays the taunt (sound 0x11d), sets flag 1 and stores `frame + 250` as the next think frame ([ai+0x4c]).
// The next think with flag 1 spawns the stage's wave, adds 1 to the stage ([ai+0x44]) and waits 100 frames (0x64, left
// as is, as PD2 leaves its own). So the 250 is the taunt-to-wave delay of every stage, the first wave's included.
// No SDK service reaches a monster AI's timers: a byte patch of the imm32's low byte, found by signature.
#include "common.h"

#include "log.h"
#include "sig.h"

#include <cstdio>
#include <cstring>

namespace {

// mov ecx,[rsi+0x170] (game frame); mov rax,[rbx]; add ecx,0xfa; mov [rax+0x4c],ecx
constexpr const char* TauntDelay = "8b 8e 70 01 00 00 48 8b 03 81 c1 fa 00 00 00 89 48 4c";
constexpr int TauntDelayAt = 11;  // the imm32's low byte

}  // namespace

auto InstallBaalWaveDelay(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	uint64_t rva = 0;
	static constexpr uint8_t Stock[] = {0xfa};
	static constexpr uint8_t Pd2[] = {0x7d};
	const bool on = sigscan::ScanUnique(context->exeBase, TauntDelay, TauntDelayAt, &rva) == 1 &&
	                context->PatchBytes(rva, Stock, sizeof(Stock), Pd2, sizeof(Pd2));
	plog::Info("baal-waves: pd2 delay %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; baal-waves: pd2 delay %s", on ? "on" : "FAILED");
	}
	return on;
}
