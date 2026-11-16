# PD2 Resurrected's D2RLoader plugins (ADR 0004): one project, d2rAI-toolkit's plugin_common static library plus our
# engine facts (common/pd2_engine.h) in each plugin DLL. Target: D2R 3.3 build 93847 + D2RLoader 1.3.1, PluginSDK in third_party/.
#
# Build:  powershell -ExecutionPolicy Bypass -File build.ps1
# Output: build/Release/core-rules.dll, core-events.dll and core-ui.dll -> tools/install.ps1 copies them to <game>/d2rloader/plugins/.
cmake_minimum_required(VERSION 3.29)
project(pd2_resurrected_plugins LANGUAGES CXX RC)

set(CORE_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/PluginSDK" CACHE PATH "D2RLoader PluginSDK directory")
if(NOT EXISTS "${CORE_SDK_DIR}/CMakeLists.txt")
	message(FATAL_ERROR "PluginSDK not found at ${CORE_SDK_DIR}: run `git submodule update --init`")
endif()

set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${CORE_SDK_DIR} PluginSDK EXCLUDE_FROM_ALL)

function(core_options target)
	if(MSVC)
		target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive- /EHsc /utf-8>)
		set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")  # static CRT: no VC++ runtime
	endif()
endfunction()

# Shared code: d2rAI-toolkit's plugin_common (log, off switch, crash and image guards, d2r::api, shared RVAs), plus
# our own engine facts in common/pd2_engine.h. Toolkit: D2R_TOOLKIT, else the nearest one beside a parent folder (so
# worktrees find it), as tools/install.ps1 and pd2_ids.toolkit() look.
set(D2R_TOOLKIT "$ENV{D2R_TOOLKIT}" CACHE PATH "d2rAI-toolkit checkout")
if(NOT D2R_TOOLKIT)
	set(dir "${CMAKE_CURRENT_SOURCE_DIR}")
	while(NOT EXISTS "${dir}/d2rAI-toolkit/shared/plugin-common/CMakeLists.txt")
		get_filename_component(parent "${dir}" DIRECTORY)
		if(parent STREQUAL dir)
			message(FATAL_ERROR "d2rAI-toolkit not found beside any parent folder: pass -DD2R_TOOLKIT=<checkout>")
		endif()
		set(dir "${parent}")
	endwhile()
	set(D2R_TOOLKIT "${dir}/d2rAI-toolkit" CACHE PATH "d2rAI-toolkit checkout" FORCE)
endif()
add_subdirectory("${D2R_TOOLKIT}/shared/plugin-common" plugin-common)  # uses our PluginSDK, added above
message(STATUS "plugin_common from ${D2R_TOOLKIT}")

add_library(core_common INTERFACE)
target_include_directories(core_common INTERFACE common)
target_link_libraries(core_common INTERFACE plugin_common)

# Bump with every release: Shared plugins must match between host and client.
set(CORE_RULES_VERSION "0.2.0" CACHE STRING "core-rules PluginInfo::version")
add_library(core_rules SHARED
	core-rules/plugin.cpp
	core-rules/skill_functions.cpp
	core-rules/missile_functions.cpp
	core-rules/item_rules.cpp
	core-rules/merc_rules.cpp
	core-rules/socket_rules.cpp
	core-rules/cube_rules.cpp
	core-rules/cube_stacks.cpp
	core-rules/almanac.cpp
	core-rules/loot_rules.cpp
	core-rules/plugin.rc  # ABI manifest resource: D2RLoader rejects a plugin DLL without it
)
# The per-encounter placement data (#947), one file per encounter: built into every plugin that reads placements.h.
file(GLOB CORE_PLACEMENT_SOURCES CONFIGURE_DEPENDS placements/*.cpp)
target_sources(core_rules PRIVATE ${CORE_PLACEMENT_SOURCES})
target_link_libraries(core_rules PRIVATE core_common)
target_compile_definitions(core_rules PRIVATE CORE_PLUGIN_NAME="core-rules" CORE_PLUGIN_VERSION="${CORE_RULES_VERSION}")
set_target_properties(core_rules PROPERTIES OUTPUT_NAME "core-rules")
core_options(core_rules)

set(CORE_EVENTS_VERSION "0.2.0" CACHE STRING "core-events PluginInfo::version")
add_library(core_events SHARED
	core-events/plugin.cpp
	core-events/cube_ops.cpp
	core-events/map_open.cpp
	core-events/map_events.cpp
	core-events/entity_pool.cpp
	core-events/key_drops.cpp
	core-events/lucion.cpp
	core-events/prime_minions.cpp
	core-events/lucion_chests.cpp
	core-events/rathma.cpp
	core-events/uber_tristram.cpp
	core-events/lucion_ai.cpp
	core-events/level_fx.cpp
	core-events/rathma_ai.cpp
	core-events/guardian_ai.cpp
	core-events/rathma_life.cpp
	core-events/dclone_ai.cpp
	core-events/ai_hooks.cpp
	core-events/event_portals.cpp
	core-events/horazon.cpp
	core-events/nihlathak_ai.cpp
	core-events/parts.cpp
	core-events/parts_shared.cpp
	core-events/uber_scaling.cpp
	core-events/uber_drops.cpp
	core-events/corruption_roll.cpp
	core-events/open_gate.cpp
	core-events/tc_roll.cpp
	core-events/uber_deaths.cpp
	core-events/uber_entry.cpp
	core-events/arena_spots.cpp
	core-events/plugin.rc
)
# Each lane registers its parts from its own folder (core-events/lane-a .. lane-d, #943): new files there need no edit here.
file(GLOB CORE_EVENTS_LANE_SOURCES CONFIGURE_DEPENDS core-events/lane-*/*.cpp)
target_sources(core_events PRIVATE ${CORE_EVENTS_LANE_SOURCES})
target_sources(core_events PRIVATE ${CORE_PLACEMENT_SOURCES})
target_include_directories(core_events PRIVATE core-events)
target_link_libraries(core_events PRIVATE core_common)
target_compile_definitions(core_events PRIVATE CORE_PLUGIN_NAME="core-events" CORE_PLUGIN_VERSION="${CORE_EVENTS_VERSION}")
set_target_properties(core_events PROPERTIES OUTPUT_NAME "core-events")
core_options(core_events)

# Client-only: not Shared, so its version need not match between host and client.
set(CORE_UI_VERSION "0.2.0" CACHE STRING "core-ui PluginInfo::version")
add_library(core_ui SHARED
	core-ui/plugin.cpp
	core-ui/automap.cpp
	core-ui/item_pictures.cpp
	core-ui/item_quantity.cpp
	core-ui/level_palette.cpp
	core-ui/stack_split.cpp
	core-ui/plugin.rc
)
target_link_libraries(core_ui PRIVATE core_common)
target_compile_definitions(core_ui PRIVATE CORE_PLUGIN_NAME="core-ui" CORE_PLUGIN_VERSION="${CORE_UI_VERSION}")
set_target_properties(core_ui PROPERTIES OUTPUT_NAME "core-ui")
core_options(core_ui)

# Host self-check of common/event_link.h (#944): not shipped, not in `all`. With CARTOGRAPHER_SRC (a d2r-tools
# plugins/cartographer/src checkout, read-only) it also checks Cartographer's own stat code.
set(CARTOGRAPHER_SRC "" CACHE PATH "Cartographer src/ folder for event_link_check (optional)")
add_executable(event_link_check EXCLUDE_FROM_ALL tests/event_link_check.cpp)
target_include_directories(event_link_check PRIVATE common)
target_compile_features(event_link_check PRIVATE cxx_std_20)
if(CARTOGRAPHER_SRC)
	target_sources(event_link_check PRIVATE "${CARTOGRAPHER_SRC}/statplan/statplan.cpp")
	target_include_directories(event_link_check PRIVATE "${CARTOGRAPHER_SRC}/statplan")
	target_compile_definitions(event_link_check PRIVATE CARTOGRAPHER_CHECK)
endif()
core_options(event_link_check)
add_executable(event_roll_check EXCLUDE_FROM_ALL tests/event_roll_check.cpp)
target_include_directories(event_roll_check PRIVATE common)
target_compile_features(event_roll_check PRIVATE cxx_std_20)
core_options(event_roll_check)
# Multiple Shot's missile line (#1056), same pattern: plugin/common/line_spread.h has no engine types.
add_executable(line_spread_check EXCLUDE_FROM_ALL tests/line_spread_check.cpp)
target_include_directories(line_spread_check PRIVATE common)
target_compile_features(line_spread_check PRIVATE cxx_std_20)
core_options(line_spread_check)
# core-events' parts with `uses` (#1081): the gate that installs them on any image their signature entries resolved on.
add_executable(parts_uses_check EXCLUDE_FROM_ALL tests/parts_uses_check.cpp core-events/parts.cpp)
target_include_directories(parts_uses_check PRIVATE core-events)
target_link_libraries(parts_uses_check PRIVATE core_common)
core_options(parts_uses_check)
