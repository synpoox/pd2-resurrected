#include "controller_split.h"

#include "pad_modifier.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>

#include <string>

namespace controller_split {

namespace {

// shortcut: XInput pad 0 and Controller QoL's default trigger threshold (30), so a DualShock the game reads through
// another API gets no pad split; upgrade with the game's own input layer if players report it.
constexpr BYTE TriggerThreshold = 30;

auto ModifierMask() noexcept -> uint32_t;

// <game>/d2rloader/config/controller-qol-updates.toml, beside our own d2rloader/plugins folder
auto ConfigText() noexcept -> std::string {
	HMODULE self = nullptr;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        reinterpret_cast<LPCSTR>(&ModifierMask), &self)) {
		return {};
	}
	char path[MAX_PATH];
	const DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) {
		return {};
	}
	std::string file(path, n);
	for (int i = 0; i < 2; ++i) {  // drop core-ui.dll, then plugins
		const size_t slash = file.find_last_of("\\/");
		if (slash == std::string::npos) {
			return {};
		}
		file.resize(slash);
	}
	file += "\\config\\controller-qol-updates.toml";
	const HANDLE h = CreateFileA(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		return {};
	}
	char buf[16384];  // its config is about 4 KB; a bigger file is read as far as this
	DWORD got = 0;
	if (!ReadFile(h, buf, sizeof buf, &got, nullptr)) {
		got = 0;
	}
	CloseHandle(h);
	return std::string(buf, got);
}

auto ModifierMask() noexcept -> uint32_t {
	return pd2::pad_modifier::FromConfig(ConfigText());
}

}  // namespace

auto ModifierHeld() noexcept -> bool {
	using namespace pd2::pad_modifier;
	XINPUT_STATE state {};
	if (XInputGetState(0, &state) != ERROR_SUCCESS) {
		return false;
	}
	uint32_t held = state.Gamepad.wButtons & (LB | RB | L3 | R3);
	held |= state.Gamepad.bLeftTrigger > TriggerThreshold ? LT : 0;
	held |= state.Gamepad.bRightTrigger > TriggerThreshold ? RT : 0;
	if (held == 0) {
		return false;  // no candidate button down: skip the config read
	}
	return (held & ModifierMask()) != 0;
}

}  // namespace controller_split
