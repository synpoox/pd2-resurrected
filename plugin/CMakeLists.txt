# PD2 Resurrected's D2RLoader plugins (ADR 0004): one project, d2rAI-toolkit's plugin_common static library plus our
# engine facts (common/pd2_engine.h) in each plugin DLL. Target: D2R 3.3 build 93847 + D2RLoader 1.3.1, PluginSDK from the toolkit.
#
# Build:  powershell -ExecutionPolicy Bypass -File build.ps1   (the toolkit's plugin-template build.ps1, see there)
# Output: build/Release/core-rules.dll, core-events.dll and core-ui.dll -> tools/install.ps1 copies them to <game>/d2rloader/plugins/.
cmake_minimum_required(VERSION 3.29)
project(pd2_resurrected_plugins LANGUAGES CXX RC)

function(core_options target)
	if(MSVC)
		target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive- /EHsc /utf-8>)
		set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")  # static CRT: no VC++ runtime
	endif()
endfunction()

# Shared code: d2rAI-toolkit's plugin_common (log, off switch, crash and image guards, d2r::api, shared RVAs), plus
# our own engine facts in common/pd2_engine.h. The toolkit comes from its finder (docs/toolkit-finder.md there):
# D2R_TOOLKIT, else a walk up from here (so worktrees find it), checked against ../toolkit.version. plugin_common
# also adds the PluginSDK from the toolkit's third_party/PluginSDK.
if(D2R_TOOLKIT)  # -DD2R_TOOLKIT=<checkout> works as the environment variable does
	set(ENV{D2R_TOOLKIT} "${D2R_TOOLKIT}")
endif()
if(DEFINED ENV{D2R_TOOLKIT})
	list(APPEND CMAKE_MODULE_PATH "$ENV{D2R_TOOLKIT}/cmake")
else()
	set(_d "${CMAKE_CURRENT_SOURCE_DIR}")
	while(NOT EXISTS "${_d}/d2rAI-toolkit/cmake/FindD2rToolkit.cmake")
		get_filename_component(_p "${_d}" DIRECTORY)
		if(_p STREQUAL _d)
			message(FATAL_ERROR "d2rAI-toolkit not found beside any parent folder: set D2R_TOOLKIT")
		endif()
		set(_d "${_p}")
	endwhile()
	list(APPEND CMAKE_MODULE_PATH "${_d}/d2rAI-toolkit/cmake")
endif()
# build.ps1 passes the template's variables; this project sets each plugin's own id, version and mod in modkit_add_plugin
set(_modkit_template_vars "${MODKIT_PLUGIN_NAME}${MODKIT_MOD_NAME}${MODKIT_SOUND_ROWS}${MODKIT_TOOLKIT_DIR}")
find_package(D2rToolkit REQUIRED)  # sets D2R_TOOLKIT_DIR; a toolkit.version mismatch is a warning (fatal when D2R_TOOLKIT_STRICT=1)
add_subdirectory("${D2R_TOOLKIT_DIR}/shared/plugin-common" plugin-common)
message(STATUS "plugin_common from ${D2R_TOOLKIT_DIR}")
include(ModkitPlugin)  # modkit_add_plugin(): one plugin DLL per call, linked to plugin_common, /W4, static CRT

add_library(core_common INTERFACE)
target_include_directories(core_common INTERFACE common)
target_link_libraries(core_common INTERFACE plugin_common)

# Bump with every release: Shared plugins must match between host and client.
set(CORE_RULES_VERSION "0.2.0" CACHE STRING "core-rules PluginInfo::version")
modkit_add_plugin(core-rules VERSION "${CORE_RULES_VERSION}" SOURCES
	core-rules/plugin.cpp
	core-rules/skill_functions.cpp
	core-rules/missile_functions.cpp
	core-rules/item_rules.cpp
	core-rules/merc_rules.cpp
	core-rules/socket_rules.cpp
	core-rules/cube_rules.cpp
	core-rules/cube_stacks.cpp
	core-rules/almanac.cpp
	core-rules/shrine_rules.cpp
	core-rules/loot_rules.cpp
	"${D2R_TOOLKIT_DIR}/plugin-template/src/skill_router.cpp"  # the toolkit's router: slot 190 dispatches this mod's skill routes
	core-events/arena_spots.cpp  # Royal Fire's ring centre reads the arena's centre spot (#962)
	core-rules/plugin.rc  # ABI manifest resource: D2RLoader rejects a plugin DLL without it
)
# The per-encounter placement data (#947), one file per encounter: built into every plugin that reads placements.h.
file(GLOB CORE_PLACEMENT_SOURCES CONFIGURE_DEPENDS placements/*.cpp)
target_sources(plugin_core_rules PRIVATE ${CORE_PLACEMENT_SOURCES})
target_include_directories(plugin_core_rules PRIVATE "${D2R_TOOLKIT_DIR}/plugin-template/src")
target_link_libraries(plugin_core_rules PRIVATE core_common)

set(CORE_EVENTS_VERSION "0.2.0" CACHE STRING "core-events PluginInfo::version")
modkit_add_plugin(core-events VERSION "${CORE_EVENTS_VERSION}" SOURCES
	core-events/plugin.cpp
	core-events/cube_ops.cpp
	core-events/map_open.cpp
	core-events/map_events.cpp
	core-events/entity_pool.cpp
	core-events/key_drops.cpp
	core-events/lucion.cpp
	core-events/altar_panel.cpp
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
	core-events/part_registry.cpp
	core-events/parts_shared.cpp
	core-events/uber_scaling.cpp
	core-events/uber_drops.cpp
	core-events/corruption_roll.cpp
	core-events/open_gate.cpp
	core-events/tc_roll.cpp
	core-events/treasure_fallen.cpp
	core-events/uber_deaths.cpp
	core-events/uber_entry.cpp
	core-events/arena_spots.cpp
	core-events/test_kill.cpp
	core-events/test_item.cpp
	core-events/plugin.rc
)
# Each lane registers its parts from its own folder (core-events/lane-a .. lane-d, #943): new files there need no edit here.
file(GLOB CORE_EVENTS_LANE_SOURCES CONFIGURE_DEPENDS core-events/lane-*/*.cpp)
target_sources(plugin_core_events PRIVATE ${CORE_EVENTS_LANE_SOURCES})
target_sources(plugin_core_events PRIVATE ${CORE_PLACEMENT_SOURCES})
target_include_directories(plugin_core_events PRIVATE core-events)
target_link_libraries(plugin_core_events PRIVATE core_common)

# Client-only: not Shared, so its version need not match between host and client.
set(CORE_UI_VERSION "0.2.0" CACHE STRING "core-ui PluginInfo::version")
modkit_add_plugin(core-ui VERSION "${CORE_UI_VERSION}" SOURCES
	core-ui/plugin.cpp
	core-ui/item_pictures.cpp
	core-ui/item_quantity.cpp
	core-ui/stack_split.cpp
	core-ui/controller_split.cpp
	core-ui/plugin.rc
)
target_link_libraries(plugin_core_ui PRIVATE core_common xinput)

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
# Rathma's clone split (#645): plugin/common/rathma_split.h has no engine types.
add_executable(rathma_split_check EXCLUDE_FROM_ALL tests/rathma_split_check.cpp)
target_include_directories(rathma_split_check PRIVATE common)
target_compile_features(rathma_split_check PRIVATE cxx_std_20)
core_options(rathma_split_check)

# Map stacking's stat compare (#1326): plugin/common/map_stack.h has no engine types.
add_executable(map_stack_check EXCLUDE_FROM_ALL tests/map_stack_check.cpp)
target_include_directories(map_stack_check PRIVATE common)
target_compile_features(map_stack_check PRIVATE cxx_std_20)
core_options(map_stack_check)

# Map boss loot by map tier (#1423): plugin/common/map_boss_tier.h has no engine types.
add_executable(map_boss_tier_check EXCLUDE_FROM_ALL tests/map_boss_tier_check.cpp)
target_include_directories(map_boss_tier_check PRIVATE common)
target_compile_features(map_boss_tier_check PRIVATE cxx_std_20)
core_options(map_boss_tier_check)

add_executable(test_cmd_check EXCLUDE_FROM_ALL tests/test_cmd_check.cpp)
target_include_directories(test_cmd_check PRIVATE common)
target_compile_features(test_cmd_check PRIVATE cxx_std_20)
core_options(test_cmd_check)

# Controller QoL modifier setting (#1442): plugin/common/pad_modifier.h has no engine types.
add_executable(pad_modifier_check EXCLUDE_FROM_ALL tests/pad_modifier_check.cpp)
target_include_directories(pad_modifier_check PRIVATE common)
target_compile_features(pad_modifier_check PRIVATE cxx_std_20)
core_options(pad_modifier_check)

# Shrine rules (#1143): plugin/common/shrine_math.h has no engine types.
add_executable(shrine_rules_check EXCLUDE_FROM_ALL tests/shrine_rules_check.cpp)
target_include_directories(shrine_rules_check PRIVATE common)
target_compile_features(shrine_rules_check PRIVATE cxx_std_20)
core_options(shrine_rules_check)
# core-events' parts with `uses` (#1081): the gate that installs them on any image their signature entries resolved on.
add_executable(parts_uses_check EXCLUDE_FROM_ALL tests/parts_uses_check.cpp core-events/part_registry.cpp)
target_include_directories(parts_uses_check PRIVATE core-events)
target_link_libraries(parts_uses_check PRIVATE core_common)
core_options(parts_uses_check)
