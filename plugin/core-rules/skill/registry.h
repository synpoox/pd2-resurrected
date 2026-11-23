// Self-registering rules of plugin/core-rules/skill/*.cpp (#2060).
// New rule: one file skill/<rule>.cpp that defines `auto Install<Rule>(context, report, reportSize) noexcept -> bool`
// and ends with `PD2R_RULE(Install<Rule>, <order>)` (a unique order, e.g. the next free multiple of 10).
// No other file changes: CMake globs skill/*.cpp and InstallSkillFunctions runs the registry sorted by (order, name).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <vector>

namespace pd2r {

using RuleInstallFn = bool (*)(const D2RL::PluginContext*, char*, size_t) noexcept;

struct RuleEntry {
	const char* name;
	int order;
	RuleInstallFn install;
};

// Function-local static: the registrars run during static init, in any order across files.
inline auto Rules() noexcept -> std::vector<RuleEntry>& {
	static std::vector<RuleEntry> rules;
	return rules;
}

struct RuleRegistrar {
	RuleRegistrar(const char* name, int order, RuleInstallFn install) noexcept {
		Rules().push_back({name, order, install});
	}
};

}  // namespace pd2r

// Bottom of a rule file, after the Install function. The extern declaration lets the macro name the global function.
#define PD2R_RULE(fn, order)                                                                                  \
	auto fn(const D2RL::PluginContext*, char*, size_t) noexcept -> bool;                                      \
	namespace {                                                                                               \
	const pd2r::RuleRegistrar pd2rRegistrar_##fn {#fn, order, &fn};                                           \
	}
