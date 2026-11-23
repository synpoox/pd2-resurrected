// PD2's shorter player sequences: Jab 13/16/18 frames, Inferno 8, Arctic Blast 8, Blade Fury 11 (#2028).
//
// 1.13c's sequence lookup (D2Common +0x2e840) reads seqnum n's weapon-class rows from the table at +0x9cf40; PD2 sends
// all 7 callers to its copy (ProjectDiablo.dll 0x102fc5f0, PD2.gpr 0x102fc5e0), which answers seqnum 1, 6, 18 and 23
// from its own rows (0x1049a468, 0x1049a660, 0x1049a5b8, 0x1049a510), and also writes them into the table.
// D2R's lookup (93847 0x3cb890) reads `[imagebase + seqnum*8 + 0x2386650]` for a player: 14 rows of 24 bytes
// {records, frames, frames, 0x100 speed, 0} per sequence, a record being 6 bytes {0, 0, mode, frame, 0, event}, as
// 1.13c's (whose rows are 12 bytes). It is the table's only reader, so the rows' first 16 bytes are pointed at PD2's
// records below. Jab's weapon class 0 is the same in both games, its classes 3-13 empty.
//
// Two D2R start steps jump a held Inferno or Arctic Blast to frame 8, past the end of PD2's 8 frames:
// - srvst 11's state step (0x551b00) re-times the caster's events from frame 8 (0x4914e0) on the channel's next start.
//   1.13c has no such jump, and PD2 skips 1.13c's whole step there for srvdo 182 (#2024), so it is skipped.
// - cltst 15 (0x220be0) sets the frame to 8 on the next start; 1.13c's sets the skill's seqinput - 1, 2 with PD2's 3.
// Both only touch a unit that plays one of PD2's records here, so monster Inferno stays D2R's.
// Shared code: the server and a joined client's process patch the same table.
#include "common.h"
#include "../item_rules.h"
#include "registry.h"

#include "hot_log.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Game;
using d2r::Unit;

#define R(mode, frame, event) 0, 0, mode, frame, 0, event

const uint8_t OldJab1[] = {R(7, 5, 0), R(7, 6, 3), R(7, 8, 0), R(7, 9, 1), R(7, 10, 0), R(7, 11, 0), R(7, 13, 0), R(8, 6, 3), R(8, 8, 0), R(8, 9, 1), R(7, 10, 0), R(7, 11, 0), R(7, 13, 0), R(8, 6, 3), R(8, 8, 0), R(8, 9, 1), R(8, 10, 0), R(8, 13, 0)};
uint8_t Pd2Jab1[] = {R(7, 1, 0), R(7, 2, 0), R(7, 4, 0), R(7, 6, 3), R(7, 8, 0), R(7, 9, 1), R(7, 12, 0), R(8, 6, 3), R(8, 8, 0), R(8, 9, 1), R(8, 12, 0), R(7, 6, 3), R(7, 8, 0), R(7, 9, 1), R(7, 10, 0), R(7, 12, 0)};
const uint8_t OldJab2[] = {R(7, 2, 0), R(7, 7, 3), R(7, 9, 0), R(7, 10, 1), R(7, 12, 0), R(7, 13, 0), R(7, 15, 0), R(8, 4, 0), R(8, 6, 3), R(8, 9, 0), R(8, 10, 1), R(7, 12, 0), R(7, 13, 0), R(7, 15, 0), R(8, 4, 0), R(8, 6, 3), R(8, 9, 0), R(8, 10, 1), R(8, 11, 0), R(8, 13, 0), R(8, 15, 0)};
uint8_t Pd2Jab2[] = {R(7, 0, 0), R(7, 1, 0), R(7, 2, 0), R(7, 4, 0), R(7, 7, 3), R(7, 9, 0), R(7, 11, 1), R(7, 14, 0), R(8, 6, 3), R(8, 9, 0), R(8, 10, 1), R(8, 14, 0), R(7, 7, 3), R(7, 9, 0), R(7, 11, 1), R(7, 12, 0), R(7, 14, 0), R(7, 16, 0)};
const uint8_t OldInferno[] = {R(10, 0, 0), R(10, 1, 0), R(10, 2, 0), R(10, 3, 0), R(10, 4, 0), R(10, 5, 0), R(10, 6, 0), R(10, 7, 0), R(10, 8, 0), R(10, 9, 0), R(10, 9, 1), R(10, 10, 0), R(10, 11, 0), R(10, 12, 0), R(10, 13, 0)};
uint8_t Pd2Inferno[] = {R(10, 1, 0), R(10, 7, 0), R(10, 9, 0), R(10, 9, 1), R(10, 10, 0), R(10, 11, 0), R(10, 12, 0), R(10, 13, 0)};
const uint8_t OldArcticBlast[] = {R(13, 0, 0), R(13, 1, 0), R(13, 2, 0), R(13, 3, 0), R(13, 4, 0), R(13, 5, 0), R(13, 6, 0), R(13, 7, 0), R(13, 8, 0), R(13, 9, 0), R(13, 9, 1), R(13, 10, 0), R(13, 11, 0), R(13, 12, 0), R(13, 13, 0)};
uint8_t Pd2ArcticBlast[] = {R(13, 1, 0), R(13, 4, 0), R(13, 9, 0), R(13, 9, 1), R(13, 10, 0), R(13, 11, 0), R(13, 12, 0), R(13, 13, 0)};
const uint8_t OldBladeFury[] = {R(10, 0, 0), R(10, 1, 0), R(10, 2, 0), R(10, 3, 0), R(10, 4, 0), R(10, 5, 0), R(10, 6, 0), R(10, 7, 0), R(10, 8, 0), R(10, 9, 0), R(10, 9, 0), R(10, 9, 0), R(10, 10, 1), R(10, 11, 0), R(10, 12, 0), R(10, 13, 0), R(10, 14, 0), R(10, 15, 0), R(10, 16, 0)};
uint8_t Pd2BladeFury[] = {R(10, 4, 0), R(10, 8, 0), R(10, 10, 0), R(10, 10, 1), R(10, 10, 0), R(10, 11, 0), R(10, 12, 0), R(10, 13, 0), R(10, 14, 0), R(10, 15, 0), R(10, 16, 0)};

#undef R

constexpr uint32_t RecordSize = 6;
constexpr uint32_t RowSize = 24;  // {records, frames, frames, speed, 0}
constexpr uint32_t Classes = 14;

struct Sequence {
	const char* name;
	uint32_t seqnum;
	uint32_t firstClass, lastClass;
	const uint8_t* old;
	uint32_t oldFrames;
	uint8_t* pd2;
	uint32_t pd2Frames;
	bool channel;  // Inferno, Arctic Blast: the start steps' frame jumps
};

#define SEQ(name, seqnum, first, last, old, pd2, channel) \
	{name, seqnum, first, last, old, sizeof(old) / RecordSize, pd2, sizeof(pd2) / RecordSize, channel}
const Sequence Sequences[] = {
    SEQ("Jab", 1, 1, 1, OldJab1, Pd2Jab1, false),
    SEQ("Jab", 1, 2, 2, OldJab2, Pd2Jab2, false),
    SEQ("Inferno", 6, 0, Classes - 1, OldInferno, Pd2Inferno, true),
    SEQ("Arctic Blast", 18, 0, Classes - 1, OldArcticBlast, Pd2ArcticBlast, true),
    SEQ("Blade Fury", 23, 0, Classes - 1, OldBladeFury, Pd2BladeFury, false),
};
#undef SEQ

// D2R's lookup: lea r15,[imagebase] / mov rcx,rsi / mov r14,[r15+rbp*8+table] / mov rax,[r14] / movzx ebx,byte [rax+2]
constexpr const char* TablePattern = "4c 8d 3d ?? ?? ?? ?? 48 8b ce 4d 8b b4 ef ?? ?? ?? ?? 49 8b 06 0f b6 58 02";
constexpr uint32_t TableDispAt = 14;
// SetSeq (0x34d900): its frames call after `mov [rbx+0x40],rax` (the records); `mov [rbx+0x48],eax` stores them
constexpr const char* FramesCallPattern = "48 83 7b 40 00 74 3a 48 8b cb e8 ?? ?? ?? ?? 89 43 48 48 8b cb 33 c0 c7 43 54 00 01 00 00";
constexpr uint32_t FramesCallAt = 10;
// srvst 11's state step: lea r8d,[rbp+8] / mov rdx,rdi / mov rcx,rsi / call 0x4914e0 / mov eax,r12d / jmp +2 / xor eax,eax
constexpr const char* RetimeCallPattern = "44 8d 45 08 48 8b d7 48 8b ce e8 ?? ?? ?? ?? 41 8b c4 eb 02 33 c0";
constexpr uint32_t RetimeCallAt = 10;
// cltst 15, state on: mov edx,8 / call 0x34d990 (frame) / mov edx,5 / mov rcx,rdi / call 0x34d8c0 / mov edx,7 / ...
constexpr const char* ClientFrameCallPattern = "ba 08 00 00 00 e8 ?? ?? ?? ?? ba 05 00 00 00 48 8b cf e8 ?? ?? ?? ?? ba 07 00 00 00 48 8b cb e8";
constexpr uint32_t ClientFrameCallAt = 5;
constexpr int32_t Pd2ClientFrame = 2;  // PD2's seqinput 3 (Inferno, Arctic Blast) - 1, as 1.13c's cltst 15 reads it

using FramesFn = int32_t (*)(Unit*) noexcept;
using RetimeFn = void (*)(Game*, Unit*, int32_t) noexcept;
using SetFrameFn = void (*)(Unit*, int32_t) noexcept;
FramesFn g_frames = nullptr;
RetimeFn g_retime = nullptr;
SetFrameFn g_setFrame = nullptr;
std::atomic<uint32_t> g_starts {0};
std::atomic<uint32_t> g_jumps {0};

auto Pd2Sequence(const Unit* unit) noexcept -> const Sequence* {
	if (unit == nullptr) {
		return nullptr;
	}
	const auto* records = d2r::At<const uint8_t*>(unit, d2r::rva::UnitSeqAnim);
	for (const Sequence& seq : Sequences) {
		if (records == seq.pd2) {
			return &seq;
		}
	}
	return nullptr;
}

auto SequenceFrames(Unit* unit) noexcept -> int32_t {
	const int32_t frames = g_frames(unit);
	const Sequence* seq = Pd2Sequence(unit);
	if (seq != nullptr && pd2::HotSample(g_starts)) {
		plog::Info("skill-sequences: unit type %d %s sequence %d frames, %u so far", d2r::At<int32_t>(unit, d2r::rva::UnitType),
		           seq->name, frames >> 8, g_starts.load());
	}
	return frames;
}

auto ChannelRetime(Game* game, Unit* unit, int32_t frame) noexcept -> void {
	const Sequence* seq = Pd2Sequence(unit);
	if (seq == nullptr || !seq->channel) {
		g_retime(game, unit, frame);
		return;
	}
	if (pd2::HotSample(g_jumps)) {
		plog::Info("skill-sequences: server %s next start: no jump to frame %d, %u so far", seq->name, frame, g_jumps.load());
	}
}

auto ClientChannelFrame(Unit* unit, int32_t frame) noexcept -> void {
	const Sequence* seq = Pd2Sequence(unit);
	const bool pd2 = seq != nullptr && seq->channel;
	if (pd2 && pd2::HotSample(g_jumps)) {
		plog::Info("skill-sequences: client %s next start: frame %d, not %d, %u so far", seq->name, Pd2ClientFrame, frame,
		           g_jumps.load());
	}
	g_setFrame(unit, pd2 ? Pd2ClientFrame : frame);
}

auto ImageSize(uint64_t base) noexcept -> uint64_t {
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	return reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew)->OptionalHeader.SizeOfImage;
}

// The table's RVA from D2R's lookup, 0 when the code differs. r15 must be the image base for the disp to be an RVA.
auto TableRva(const D2RL::PluginContext* context) noexcept -> uint64_t {
	uint64_t rva = 0;
	if (sigscan::ScanUnique(context->exeBase, TablePattern, 0, &rva) != 1) {
		return 0;
	}
	int32_t lea = 0;
	int32_t table = 0;
	std::memcpy(&lea, reinterpret_cast<const void*>(context->exeBase + rva + 3), sizeof(lea));
	std::memcpy(&table, reinterpret_cast<const void*>(context->exeBase + rva + TableDispAt), sizeof(table));
	const bool imageBase = static_cast<int64_t>(rva) + 7 + lea == 0;
	return imageBase && table > 0 && static_cast<uint64_t>(table) + 24 * 8 <= ImageSize(context->exeBase) ? table : 0;
}

struct Row {
	uint64_t rva;
	uint8_t stock[16];
	uint8_t pd2[16];
};

// Every row the rule writes, checked against 1.13c's records first, so nothing is written unless all of them match.
auto CollectRows(const D2RL::PluginContext* context, uint64_t table, Row* rows, size_t capacity) noexcept -> size_t {
	const uint64_t base = context->exeBase;
	const uint64_t size = ImageSize(base);
	size_t count = 0;
	for (const Sequence& seq : Sequences) {
		uint64_t entry = 0;
		std::memcpy(&entry, reinterpret_cast<const void*>(base + table + seq.seqnum * 8), sizeof(entry));
		if (entry < base || entry + Classes * RowSize > base + size) {
			return 0;
		}
		for (uint32_t c = seq.firstClass; c <= seq.lastClass; ++c) {
			const uint64_t at = entry + c * RowSize;
			uint64_t records = 0;
			uint32_t frames[2] = {};
			std::memcpy(&records, reinterpret_cast<const void*>(at), sizeof(records));
			std::memcpy(frames, reinterpret_cast<const void*>(at + 8), sizeof(frames));
			if (count == capacity || frames[0] != seq.oldFrames || frames[1] != seq.oldFrames || records < base ||
			    records + seq.oldFrames * RecordSize > base + size ||
			    std::memcmp(reinterpret_cast<const void*>(records), seq.old, seq.oldFrames * RecordSize) != 0) {
				return 0;
			}
			Row& row = rows[count++];
			row.rva = at - base;
			std::memcpy(row.stock, reinterpret_cast<const void*>(at), sizeof(row.stock));
			const uint64_t pd2 = reinterpret_cast<uint64_t>(seq.pd2);
			const uint32_t pd2Frames[2] = {seq.pd2Frames, seq.pd2Frames};
			std::memcpy(row.pd2, &pd2, sizeof(pd2));
			std::memcpy(row.pd2 + 8, pd2Frames, sizeof(pd2Frames));
		}
	}
	return count;
}

}  // namespace

auto InstallSkillSequences(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	// The jump redirects first: they only act on PD2's records, and PD2's 8 frames must not meet a jump to frame 8.
	const bool jumps = RedirectCall(context, RetimeCallPattern, RetimeCallAt, reinterpret_cast<uint64_t>(&ChannelRetime),
	                                reinterpret_cast<uint64_t*>(&g_retime)) &&
	                   RedirectCall(context, ClientFrameCallPattern, ClientFrameCallAt,
	                                reinterpret_cast<uint64_t>(&ClientChannelFrame), reinterpret_cast<uint64_t*>(&g_setFrame));
	Row rows[2 + 3 * Classes];
	const uint64_t table = jumps ? TableRva(context) : 0;
	const size_t count = table != 0 ? CollectRows(context, table, rows, sizeof(rows) / sizeof(rows[0])) : 0;
	bool on = count == sizeof(rows) / sizeof(rows[0]);
	for (size_t i = 0; on && i < count; ++i) {
		on = context->PatchBytes(rows[i].rva, rows[i].stock, sizeof(rows[i].stock), rows[i].pd2, sizeof(rows[i].pd2));
	}
	const bool logs = RedirectCall(context, FramesCallPattern, FramesCallAt, reinterpret_cast<uint64_t>(&SequenceFrames),
	                               reinterpret_cast<uint64_t*>(&g_frames));
	plog::Info("skill-sequences: PD2's Jab, Inferno, Arctic Blast, Blade Fury frames %s (%zu rows), channel start frames %s, "
	           "frames log %s",
	           on ? "on" : "FAILED", count, jumps ? "on" : "FAILED", logs ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; skill-sequences: PD2 frames %s, channel start frames %s",
		              on ? "on" : "FAILED", jumps ? "on" : "FAILED");
	}
	return on && jumps;
}

PD2R_RULE(InstallSkillSequences, 170)
