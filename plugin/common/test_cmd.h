// The arguments of core-events' monster test commands (`kill <class>`, `kill unit <id>`, `life <pct> unit <id>`); no
// engine types, so tests/test_cmd_check.cpp runs on the host. `life <pct> <class>` is the older ported-boss command.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace pd2::test_cmd {

struct Cmd {
	enum class Kind { None, Kill, LifeUnit, Corrupt, Desecrate } kind = Kind::None;
	int32_t percent = 100;  // LifeUnit
	int32_t classId = 0;    // Kill by class; 0 when by unit
	uint32_t unitId = 0;    // Kill or LifeUnit by unit id; 0 when by class
};

// A positive number at `*p` (after spaces); advances `*p`. 0 when none.
inline auto Number(const char** p) noexcept -> long {
	while (**p == ' ') {
		++*p;
	}
	char* end = nullptr;
	const long v = std::strtol(*p, &end, 10);
	if (end == *p || v <= 0) {
		return 0;
	}
	*p = end;
	return v;
}

// True when `*p` holds the word `unit` (then skipped)
inline auto UnitWord(const char** p) noexcept -> bool {
	while (**p == ' ') {
		++*p;
	}
	if (std::strncmp(*p, "unit ", 5) != 0) {
		return false;
	}
	*p += 5;
	return true;
}

// `args` is the text after the plugin name. Cmd::Kind::None when it is neither command or is malformed.
inline auto Parse(const char* args) noexcept -> Cmd {
	Cmd out;
	if (args == nullptr) {
		return out;
	}
	const char* p = args;
	while (*p == ' ') {
		++p;
	}
	if (std::strcmp(p, "corrupt") == 0 || std::strcmp(p, "desecrate") == 0) {  // no arguments: the cursor item or inventory (0,0)
		out.kind = p[0] == 'c' ? Cmd::Kind::Corrupt : Cmd::Kind::Desecrate;
	} else if (std::strncmp(p, "kill ", 5) == 0) {
		p += 5;
		const bool byUnit = UnitWord(&p);
		const long n = Number(&p);
		while (*p == ' ') {
			++p;
		}
		if (n > 0 && *p == '\0') {
			out.kind = Cmd::Kind::Kill;
			if (byUnit) {
				out.unitId = static_cast<uint32_t>(n);
			} else {
				out.classId = static_cast<int32_t>(n);
			}
		}
	} else if (std::strncmp(p, "life ", 5) == 0) {
		p += 5;
		char* end = nullptr;
		const long percent = std::strtol(p, &end, 10);
		if (end == p || percent < 0 || percent > 100) {
			return out;
		}
		p = end;
		if (!UnitWord(&p)) {
			return out;  // `life <pct> <class>` stays the ported-boss command
		}
		const long id = Number(&p);
		while (*p == ' ') {
			++p;
		}
		if (id > 0 && *p == '\0') {
			out.kind = Cmd::Kind::LifeUnit;
			out.percent = static_cast<int32_t>(percent);
			out.unitId = static_cast<uint32_t>(id);
		}
	}
	return out;
}

}  // namespace pd2::test_cmd
