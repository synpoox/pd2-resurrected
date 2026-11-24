// Rooms: our map, uber and Zhar portals count as portals when their room is freed, as in PD2 (#2020).
//
// PD2 patches two sites of 1.13c D2Game with a call to installed 0x10371480 (PD2.gpr 0x10371410), a class test: 59, 60,
// 100 and PD2 raw objects 584-587 (xactportal_red, _ghostly, _gold, the `empty` link marker) and 595-599
// (xactportal_grey, _green, _purple, zharportal_blue, _red) are portals. Our objects.txt rows are PD2's + 3: 587-590 and
// 598-602.
//  - +0x209b0 (room free, per unit), site +0x20a08: 1.13c keeps 59 and 60 (not 100) always: its events go, unit flag
//    0x100 is set, the room record is saved (+0x204e0) and the unit is kept, not freed. Any other object is saved only
//    when the room asks for it and its objects.txt flags allow, then freed. PD2's site also takes 100.
//  - +0x204e0 (the room record), site +0x206e2: a portal (59, 60, 100) stores its unit id (record +0x20), any other
//    object a state byte (+0x24), so the kept portal comes back as the same unit and its partner's link holds.
// D2R 93847: the room free is 0x504260, its test `lea eax,[r14-0x3b]; cmp eax,1; jbe` at 0x5042ed (59, 60); the record
// is 0x5045d0, its test `lea eax,[rcx-0x3b]; cmp eax,1; jbe; cmp ecx,0x64; je` at 0x50498b (59, 60, 100). Without
// this a linked portal of ours whose room is freed is freed with it (or saved without its id), and its partner's link
// breaks. Each 13-byte test becomes a call to a relay stub (`test al,al` on IsPortalClass) and a jne to the stock
// target. Every volatile register is dead at both sites (the next instructions or calls set them). D2RCore lists the
// two functions' entries in its RVA table; both sites are mid-function. No SDK service reaches either test.
#include "common.h"

#include "hot_log.h"
#include "log.h"
#include "sig.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

// mov ecx,[rbx+4]; lea eax,[rcx-0x3b]; cmp eax,1; jbe +0x11; cmp ecx,0x64; je +0xc; movzx eax,byte [rbx+0xc8]
constexpr const char* RecordTest = "8b 4b 04 8d 41 c5 83 f8 01 76 11 83 f9 64 74 0c 0f b6 83 c8 00 00 00";
constexpr int RecordTestAt = 3;
constexpr uint8_t RecordStock[] = {0x8d, 0x41, 0xc5, 0x83, 0xf8, 0x01, 0x76, 0x11, 0x83, 0xf9, 0x64, 0x74, 0x0c};
// lea eax,[r14-0x3b]; cmp eax,1; jbe +0x9c; movzx ecx,byte [rdi+0x106]; mov edx,r14d; call
constexpr const char* FreeTest = "41 8d 46 c5 83 f8 01 0f 86 9c 00 00 00 0f b6 8f 06 01 00 00 41 8b d6 e8";
constexpr uint8_t FreeStock[] = {0x41, 0x8d, 0x46, 0xc5, 0x83, 0xf8, 0x01, 0x0f, 0x86, 0x9c, 0x00, 0x00, 0x00};


auto IsPortalClass(int32_t id) noexcept -> bool {
	return id == 59 || id == 60 || id == 100 || (id >= 587 && id <= 590) || (id >= 598 && id <= 602);
}
static_assert(sizeof(RecordStock) == 13 && sizeof(FreeStock) == 13);

auto KeepAsPortal(int32_t id) noexcept -> bool {
	const bool portal = IsPortalClass(id);
	return portal;
}

// A page within rel32 reach of the image (as item_rules.cpp's AllocateRelay), from the far end of the window.
auto AllocateRelay(uint64_t image) noexcept -> uint8_t* {
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	const uint64_t step = info.dwAllocationGranularity;
	const uint64_t reach = 0x70000000ull;
	const uint64_t low = image > reach ? image - reach : step;
	for (uint64_t at = (low + step - 1) & ~(step - 1); at < image; at += step) {
		if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
			return static_cast<uint8_t*>(p);
		}
	}
	return nullptr;
}

// rsp is 16-aligned at both sites, so the stub's own call is aligned after `sub rsp,0x28`.
auto WriteStub(uint8_t* at, bool fromR14) noexcept -> void {
	static constexpr uint8_t MoveR14[] = {0x44, 0x89, 0xf1};  // mov ecx,r14d
	static constexpr uint8_t Head[] = {0x48, 0x83, 0xec, 0x28, 0x48, 0xb8};  // sub rsp,0x28; mov rax,imm64
	static constexpr uint8_t Tail[] = {0xff, 0xd0, 0x48, 0x83, 0xc4, 0x28, 0x84, 0xc0, 0xc3};  // call rax; add; test al,al; ret
	const uint64_t entry = reinterpret_cast<uint64_t>(&KeepAsPortal);
	if (fromR14) {
		std::memcpy(at, MoveR14, sizeof(MoveR14));
		at += sizeof(MoveR14);
	}
	std::memcpy(at, Head, sizeof(Head));
	std::memcpy(at + sizeof(Head), &entry, 8);
	std::memcpy(at + sizeof(Head) + 8, Tail, sizeof(Tail));
}

// `call stub` then the jne, nop-padded to the 13 stock bytes; false when the stub is out of rel32 reach.
auto SiteBytes(uint64_t site, const uint8_t* stub, const uint8_t* jne, size_t jneSize, uint8_t (&out)[13]) noexcept -> bool {
	const int64_t reach = reinterpret_cast<int64_t>(stub) - static_cast<int64_t>(site + 5);
	const int32_t rel = static_cast<int32_t>(reach);
	std::memset(out, 0x90, sizeof(out));
	out[0] = 0xe8;
	std::memcpy(out + 1, &rel, 4);
	std::memcpy(out + 5, jne, jneSize);
	return reach == rel;
}

auto Install(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t record = 0;
	uint64_t unload = 0;
	if (sigscan::ScanUnique(context->exeBase, RecordTest, RecordTestAt, &record) != 1 ||
	    sigscan::ScanUnique(context->exeBase, FreeTest, 0, &unload) != 1) {
		return false;
	}
	uint8_t* relay = AllocateRelay(context->exeBase);
	if (relay == nullptr) {
		return false;
	}
	WriteStub(relay, false);
	WriteStub(relay + 0x20, true);
	static constexpr uint8_t RecordJne[] = {0x75, 0x12};  // to the stock jbe/je target, record +0x19
	static constexpr uint8_t FreeJne[] = {0x0f, 0x85, 0x9e, 0x00, 0x00, 0x00};  // to the stock jbe target, +0xa9
	uint8_t recordBytes[13];
	uint8_t freeBytes[13];
	DWORD old;
	if (!SiteBytes(context->exeBase + record, relay, RecordJne, sizeof(RecordJne), recordBytes) ||
	    !SiteBytes(context->exeBase + unload, relay + 0x20, FreeJne, sizeof(FreeJne), freeBytes) ||
	    VirtualProtect(relay, 0x1000, PAGE_EXECUTE_READ, &old) == 0) {
		VirtualFree(relay, 0, MEM_RELEASE);
		return false;
	}
	if (!context->PatchBytes(record, RecordStock, sizeof(RecordStock), recordBytes, sizeof(recordBytes))) {
		VirtualFree(relay, 0, MEM_RELEASE);
		return false;
	}
	if (!context->PatchBytes(unload, FreeStock, sizeof(FreeStock), freeBytes, sizeof(freeBytes))) {
		// both or neither: the record alone would save an id for a portal the free still drops; keep the page if the undo fails
		if (context->PatchBytes(record, recordBytes, sizeof(recordBytes), RecordStock, sizeof(RecordStock))) {
			VirtualFree(relay, 0, MEM_RELEASE);
		}
		return false;
	}
	return true;
}

}  // namespace

auto InstallPortalRoomClasses(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool on = Install(context);
	plog::Info("portal-rooms: pd2 portal objects on room free %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; portal-rooms: pd2 portal objects on room free %s",
		              on ? "on" : "FAILED");
	}
	return on;
}

#include "registry.h"
PD2R_RULE(InstallPortalRoomClasses, 130)
