// Monster life per extra player: +70% each (70, 140 ... 490% for players 2-8), as in PD2 (#2025).
//
// PD2 patches 1.13c D2Game's life-per-player table `+0xfb614` slots 2-8 (50, 100 ... 350 -> 70, 140 ... 490). D2R's
// reader is 93847 0x543100: `cmp ecx,9; jge ...; lea rcx,[rip+table]; mov eax,[rcx+rax*4]` over int32 {0, 0, 50 ...
// 350} at 0x1d3d4a8 (players 2-8 at +8..+0x20), else (n-2)*50. Its only caller (0x581c73) scales a monster's life by
// pct/100; nothing else reads the table (the XP table is separate and stays as is). So the table is patched in place.
// No SDK service reaches this table: a byte patch, the table found through the reader's rip-relative operand.
#include "common.h"

#include "log.h"
#include "sig.h"

#include <cstdio>
#include <cstring>

namespace {

// cmp ecx,9; jge +0xe; movsxd rax,ecx; lea rcx,[rip+table]; mov eax,[rcx+rax*4]; ret; lea eax,[rcx-2]; imul eax,eax,50; ret
constexpr const char* LifeFn = "83 f9 09 7d 0e 48 63 c1 48 8d 0d ?? ?? ?? ?? 8b 04 81 c3 8d 41 fe 6b c0 32 c3";
constexpr int LifeFnDisp = 11;  // the lea's disp32; the instruction ends 4 bytes on
constexpr int Player2 = 2;      // the table's first slot that changes
constexpr int Players = 7;      // players 2-8

}  // namespace

auto InstallMonsterLifePerPlayer(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	uint64_t rva = 0;
	bool on = false;
	if (sigscan::ScanUnique(context->exeBase, LifeFn, LifeFnDisp, &rva) == 1) {
		const auto disp = *reinterpret_cast<const int32_t*>(context->exeBase + rva);
		const uint64_t table = rva + 4 + static_cast<int64_t>(disp);
		int32_t stock[Players], pd2[Players];
		for (int i = 0; i < Players; ++i) {
			stock[i] = 50 * (i + 1);
			pd2[i] = 70 * (i + 1);
		}
		on = context->PatchBytes(table + Player2 * sizeof(int32_t), stock, sizeof(stock), pd2, sizeof(pd2));
	}
	plog::Info("monster-life: pd2 player bonus %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; monster-life: pd2 player bonus %s", on ? "on" : "FAILED");
	}
	return on;
}
