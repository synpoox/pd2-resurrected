// Self-registering rules, slot functions and routes of plugin/core-rules/skill/*.cpp (#2060, #2044).
// New rule: one file skill/<rule>.cpp that defines `auto Install<Rule>(context, report, reportSize) noexcept -> bool`
// and ends with `PD2R_RULE(Install<Rule>, <order>)` (a unique order, e.g. the next free multiple of 10).
// No other file changes: CMake globs skill/*.cpp and InstallSkillFunctions runs the registry sorted by (order, name).
#pragma once

#include <D2RLPlugin/api.h>

#include "pd2_engine.h"
#include "skill_router.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pd2r {

using RuleInstallFn = bool (*)(const D2RL::PluginContext*, char*, size_t) noexcept;
// A rule that installs several parts: returns how many worked, out of `tried` (PD2R_RULE_N).
using RuleInstallNFn = int (*)(const D2RL::PluginContext*, char*, size_t) noexcept;

struct RuleEntry {
	const char* name;
	int order;
	RuleInstallFn install;
	RuleInstallNFn installN;
	int tried;
};

// Function-local static: the registrars run during static init, in any order across files.
inline auto Rules() noexcept -> std::vector<RuleEntry>& {
	static std::vector<RuleEntry> rules;
	return rules;
}

struct RuleRegistrar {
	RuleRegistrar(const char* name, int order, RuleInstallFn install) noexcept {
		Rules().push_back({name, order, install, nullptr, 1});
	}
	RuleRegistrar(const char* name, int order, RuleInstallNFn install, int tried) noexcept {
		Rules().push_back({name, order, nullptr, install, tried});
	}
};

namespace skills {

struct Table {
	const char* name;  // the skills.txt column
	uint64_t rva;
	int slots;
	int firstFree;
};
constexpr Table SrvDo {"srvdofunc", d2r::rva::SrvDoTable, d2r::rva::SrvDoSlots, d2r::rva::SrvDoFirstFree};
constexpr Table CltDo {"cltdofunc", d2r::rva::CltDoTable, d2r::rva::CltDoSlots, d2r::rva::CltDoFirstFree};
constexpr Table SrvSt {"srvstfunc", d2r::rva::SrvStTable, d2r::rva::SrvStSlots, d2r::rva::SrvStFirstFree};
constexpr Table CltSt {"cltstfunc", d2r::rva::CltStTable, d2r::rva::CltStSlots, d2r::rva::CltStFirstFree};
constexpr Table MissileSrvDo {"pSrvDoFunc", d2r::rva::MissileSrvDoTable, d2r::rva::MissileSrvDoSlots,
                              d2r::rva::MissileSrvDoFirstFree};
constexpr Table MissileSrvHit {"pSrvHitFunc", d2r::rva::MissileSrvHitTable, d2r::rva::MissileSrvHitSlots,
                               d2r::rva::MissileSrvHitFirstFree};
constexpr Table MissileCltDo {"pCltDoFunc", d2r::rva::MissileCltDoTable, d2r::rva::MissileCltDoSlots,
                              d2r::rva::MissileCltDoFirstFree};
constexpr Table MissileSrvDmg {"pSrvDmgFunc", d2r::rva::MissileSrvDmgTable, d2r::rva::MissileSrvDmgSlots,
                               d2r::rva::MissileSrvDmgFirstFree};
constexpr Table MissileCltHit {"pCltHitFunc", d2r::rva::MissileCltHitTable, d2r::rva::MissileCltHitSlots,
                               d2r::rva::MissileCltHitFirstFree};

// A skills.txt function slot (PD2R_SLOT): written by InstallSkillFunctions in `order`, the report's order.
struct SlotEntry {
	int order;
	const Table* table;
	int slot;  // the skills.txt value
	const void* function;
	const char* name;
};
inline auto Slots() noexcept -> std::vector<SlotEntry>& {
	static std::vector<SlotEntry> slots;
	return slots;
}
struct SlotRegistrar {
	SlotRegistrar(int order, const Table& table, int slot, const void* function, const char* name) noexcept {
		Slots().push_back({order, &table, slot, function, name});
	}
};

// A srvdofunc 190 route: skill id -> do function (`order` unique).

struct RouteEntry {
	int order;
	int32_t skillId;
	SrvDoFn function;
};
inline auto Routes() noexcept -> std::vector<RouteEntry>& {
	static std::vector<RouteEntry> routes;
	return routes;
}
struct RouteRegistrar {
	RouteRegistrar(int order, int32_t skillId, SrvDoFn function) noexcept {
		Routes().push_back({order, skillId, function});
	}
};

}  // namespace skills
}  // namespace pd2r

#define PD2R_CAT2(a, b) a##b
#define PD2R_CAT(a, b) PD2R_CAT2(a, b)

// Bottom of a rule file, after the Install function. The extern declaration lets the macro name the global function.
#define PD2R_RULE(fn, order) \
	auto fn(const D2RL::PluginContext*, char*, size_t) noexcept -> bool; \
	namespace { \
	const pd2r::RuleRegistrar pd2rRegistrar_##fn {#fn, order, &fn}; \
	}
// A rule that installs several parts: `tried` of them, the function returns how many worked.
#define PD2R_RULE_N(fn, order, tried) \
	auto fn(const D2RL::PluginContext*, char*, size_t) noexcept -> int; \
	namespace { \
	const pd2r::RuleRegistrar pd2rRegistrar_##fn {#fn, order, &fn, tried}; \
	}
// A skills.txt function slot: `table` is a pd2r::skills::Table name, `fn` a cast function pointer.
#define PD2R_SLOT(order, table, slot, fn, label) \
	namespace { \
	const pd2r::skills::SlotRegistrar PD2R_CAT(pd2rSlot_, __COUNTER__) {order, pd2r::skills::table, slot, fn, label}; \
	}
// A srvdofunc 190 route: skill id -> do function (`order` unique).
#define PD2R_ROUTE(order, skillId, fn) \
	namespace { \
	const pd2r::skills::RouteRegistrar PD2R_CAT(pd2rRoute_, __COUNTER__) {order, skillId, fn}; \
	}
