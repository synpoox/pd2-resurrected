// Crash guard: if the game faults, say so and say why, in the plugin's own log. A vectored exception handler sees the
// fault before D2R or D2RLoader handle it, so it cannot know yet whether the fault is fatal: it logs a FAULT line, and
// if the game is still running a few seconds later a second line says so. That line is a hint, not proof: a crash
// reporter can keep a dying game running that long. After a crash, start with the last FAULT line.
//
// The handler never handles the exception and never logs. The faulting thread may hold the loader's log lock or have
// almost no stack left, so it only copies the facts, wakes a writer thread and waits a bounded time for it to finish.
// Repeats of a fault (same address, same target) are counted and logged again at most once a minute, so harmless repeats
// cannot flood the log or crowd out the real crash, and a fault the game survived earlier can still show up if it later
// ends the game. This header also holds the image line for the load log and status command.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace crash_guard {

inline constexpr int SeenSlots = 32;
inline constexpr DWORD WriterWaitMs = 500;     // how long a faulting thread waits for its line
inline constexpr DWORD StillRunningAfterMs = 3000;  // quiet this long after a fault: say the game is still running
inline constexpr ULONGLONG RepeatLogMs = 60000;  // the same fault logs again after this long

struct Fault {
	DWORD code;
	DWORD thread;
	uintptr_t rip;
	uintptr_t self;  // address inside this plugin: rip, or the first one on the stack; 0 when there is none
	ULONG_PTR kind;  // access violation: 0 read, 1 write, 8 execute
	ULONG_PTR at;
};

inline char g_version[32] = "?";
inline uintptr_t g_exeBase = 0;
inline uint32_t g_stamp = 0;
inline uintptr_t g_selfBase = 0;
inline uintptr_t g_selfEnd = 0;
inline void* g_handler = nullptr;
inline HANDLE g_thread = nullptr;
inline HANDLE g_wake = nullptr;
inline HANDLE g_done = nullptr;
inline HANDLE g_stop = nullptr;
inline std::atomic<DWORD> g_writerId{0};
inline std::atomic_flag g_busy = ATOMIC_FLAG_INIT;  // owns g_fault and g_seen; the writer clears it after its line
inline Fault g_fault{};
inline uint64_t g_seen[SeenSlots] = {};
inline ULONGLONG g_seenAt[SeenSlots] = {};  // when each g_seen fault last logged
inline int g_seenNext = 0;
inline std::atomic<uint32_t> g_repeats{0};

inline auto Fatal(DWORD code) noexcept -> bool {
	return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_PRIV_INSTRUCTION ||
	       code == EXCEPTION_IN_PAGE_ERROR || code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_STACK_OVERFLOW;
}

inline auto InSelf(uintptr_t a) noexcept -> bool {
	return a >= g_selfBase && a < g_selfEnd;
}

// Lowest stack slot that holds an address inside this plugin, 0 when there is none.
inline auto SelfOnStack(uintptr_t rsp) noexcept -> uintptr_t {
	const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
	const auto top = reinterpret_cast<uintptr_t>(tib->StackBase);
	const auto low = reinterpret_cast<uintptr_t>(tib->StackLimit);
	if (rsp < low || rsp >= top) {
		return 0;
	}
	const uintptr_t end = top - rsp > 0x4000 ? rsp + 0x4000 : top;
	for (uintptr_t p = rsp & ~uintptr_t{7}; p + 8 <= end; p += 8) {
		const uintptr_t value = *reinterpret_cast<const uintptr_t*>(p);
		if (InSelf(value)) {
			return value;
		}
	}
	return 0;
}

inline auto Why(const Fault& f, char* out, size_t size) noexcept -> const char* {
	if (f.code == EXCEPTION_ACCESS_VIOLATION || f.code == EXCEPTION_IN_PAGE_ERROR) {
		if (f.kind == 8) {
			std::snprintf(out, size, "tried to execute non-code memory at 0x%llx (a bad function address)", static_cast<unsigned long long>(f.at));
		} else {
			std::snprintf(out, size, "%s of bad memory at 0x%llx%s", f.kind == 1 ? "write" : "read", static_cast<unsigned long long>(f.at),
			              f.at < 0x10000 ? " (a null pointer plus a field offset)" : " (a wrong pointer or field offset)");
		}
	} else if (f.code == EXCEPTION_ILLEGAL_INSTRUCTION || f.code == EXCEPTION_PRIV_INSTRUCTION) {
		std::snprintf(out, size, "bad instruction (execution jumped into the middle of code or data)");
	} else if (f.code == EXCEPTION_STACK_OVERFLOW) {
		std::snprintf(out, size, "stack overflow (runaway recursion)");
	} else {
		std::snprintf(out, size, "integer division by zero");
	}
	return out;
}

inline void Report(const Fault& f) noexcept {
	const char* blame = InSelf(f.rip)  ? "this plugin's own code faulted"
	                    : f.self != 0  ? "this plugin is on the stack: likely involved"
	                                   : "this plugin is not on the stack: probably not the cause";
	char why[160];
	plog::Error("FAULT %s: 0x%08lx at 0x%llx (image +0x%llx, plugin +0x%llx), thread %lu | why: %s | blame: %s | image stamp 0x%08x",
	            g_version, static_cast<unsigned long>(f.code), static_cast<unsigned long long>(f.rip),
	            static_cast<unsigned long long>(f.rip - g_exeBase), static_cast<unsigned long long>(f.self != 0 ? f.self - g_selfBase : 0),
	            static_cast<unsigned long>(f.thread), Why(f, why, sizeof(why)), blame, g_stamp);
}

inline auto WINAPI Writer(LPVOID) noexcept -> DWORD {
	g_writerId.store(GetCurrentThreadId());
	const HANDLE events[2] = {g_stop, g_wake};
	int unconfirmed = 0;
	for (;;) {
		const DWORD r = WaitForMultipleObjects(2, events, FALSE, unconfirmed > 0 ? StillRunningAfterMs : INFINITE);
		if (r == WAIT_OBJECT_0 + 1) {
			const Fault fault = g_fault;
			Report(fault);
			++unconfirmed;
			SetEvent(g_done);
			g_busy.clear();
		} else if (r == WAIT_TIMEOUT) {
			plog::Info("game still running %lu s after the %d FAULT line(s) above (%u repeats not logged)", StillRunningAfterMs / 1000, unconfirmed,
			           g_repeats.exchange(0));
			unconfirmed = 0;
		} else {
			return 0;
		}
	}
}

inline auto CALLBACK Handler(EXCEPTION_POINTERS* info) noexcept -> LONG {
	if (info == nullptr || info->ExceptionRecord == nullptr || info->ContextRecord == nullptr || !Fatal(info->ExceptionRecord->ExceptionCode) ||
	    GetCurrentThreadId() == g_writerId.load() || g_busy.test_and_set()) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	const EXCEPTION_RECORD* e = info->ExceptionRecord;
	const auto rip = static_cast<uintptr_t>(info->ContextRecord->Rip);
	const ULONG_PTR kind = e->NumberParameters > 0 ? e->ExceptionInformation[0] : 0;
	const ULONG_PTR at = e->NumberParameters > 1 ? e->ExceptionInformation[1] : 0;
	const uint64_t key = (static_cast<uint64_t>(rip) * 0x9E3779B97F4A7C15ULL ^ static_cast<uint64_t>(at)) | 1;
	const ULONGLONG now = GetTickCount64();
	int slot = -1;
	for (int i = 0; i < SeenSlots && slot < 0; ++i) {
		if (g_seen[i] == key) {
			slot = i;
		}
	}
	if (slot >= 0 && now - g_seenAt[slot] < RepeatLogMs) {
		g_repeats.fetch_add(1);
		g_busy.clear();
		return EXCEPTION_CONTINUE_SEARCH;
	}
	if (slot < 0) {
		slot = g_seenNext;
		g_seen[slot] = key;
		g_seenNext = (g_seenNext + 1) % SeenSlots;
	}
	g_seenAt[slot] = now;
	g_fault = Fault{e->ExceptionCode, GetCurrentThreadId(), rip, InSelf(rip) ? rip : SelfOnStack(static_cast<uintptr_t>(info->ContextRecord->Rsp)), kind, at};
	ResetEvent(g_done);
	SetEvent(g_wake);
	WaitForSingleObject(g_done, WriterWaitMs);
	return EXCEPTION_CONTINUE_SEARCH;
}

// TimeDateStamp of the PE image at `exeBase` (0 when there is none).
inline auto ImageStamp(uintptr_t exeBase) noexcept -> uint32_t {
	if (exeBase == 0) {
		return 0;
	}
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exeBase);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
		return 0;
	}
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(exeBase + static_cast<uintptr_t>(dos->e_lfanew));
	return nt->Signature == IMAGE_NT_SIGNATURE ? nt->FileHeader.TimeDateStamp : 0;
}

// The status / log line: "D2R 3.3.93847, image 0x6ab3782c".
inline void Describe(char* out, size_t size, const char* buildVersion) noexcept {
	std::snprintf(out, size, "D2R %s, image 0x%08x", buildVersion != nullptr ? buildVersion : "?", g_stamp);
}

inline void Remove() noexcept {
	if (g_handler != nullptr) {
		RemoveVectoredExceptionHandler(g_handler);
		g_handler = nullptr;
	}
	if (g_thread != nullptr) {
		SetEvent(g_stop);
		WaitForSingleObject(g_thread, 1000);
		CloseHandle(g_thread);
		g_thread = nullptr;
	}
	for (HANDLE* h : {&g_wake, &g_done, &g_stop}) {
		if (*h != nullptr) {
			CloseHandle(*h);
			*h = nullptr;
		}
	}
}

inline void Install(const char* version, uintptr_t exeBase) noexcept {
	if (g_handler != nullptr) {
		return;
	}
	std::snprintf(g_version, sizeof(g_version), "%s", version);
	g_exeBase = exeBase;
	g_stamp = ImageStamp(exeBase);
	HMODULE self = nullptr;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                       reinterpret_cast<LPCWSTR>(&Handler), &self) && self != nullptr) {
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(self);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<uintptr_t>(self) + static_cast<uintptr_t>(dos->e_lfanew));
		g_selfBase = reinterpret_cast<uintptr_t>(self);
		g_selfEnd = g_selfBase + nt->OptionalHeader.SizeOfImage;
	}
	g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	g_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (g_wake != nullptr && g_done != nullptr && g_stop != nullptr) {
		g_thread = CreateThread(nullptr, 64 * 1024, Writer, nullptr, 0, nullptr);
	}
	if (g_thread == nullptr) {
		plog::Warn("crash guard off: could not start its writer thread");
		Remove();
		return;
	}
	g_handler = AddVectoredExceptionHandler(1, Handler);
}

}  // namespace crash_guard
