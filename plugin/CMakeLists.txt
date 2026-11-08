# PD2 Resurrected's D2RLoader plugins (ADR 0004): one project, d2rAI-toolkit's plugin_common static library plus our
# engine facts (common/pd2_engine.h) in each plugin DLL. Target: D2R 3.3 build 93847 + D2RLoader 1.3.1, PluginSDK in third_party/.
#
# Build:  powershell -ExecutionPolicy Bypass -File build.ps1
# Output: build/Release/core-rules.dll, core-maps.dll and core-ui.dll -> tools/install.ps1 copies them to <game>/d2rloader/plugins/.
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
set(CORE_RULES_VERSION "0.1.0" CACHE STRING "core-rules PluginInfo::version")
add_library(core_rules SHARED
	core-rules/plugin.cpp
	core-rules/skill_functions.cpp
	core-rules/missile_functions.cpp
	core-rules/item_rules.cpp
	core-rules/merc_rules.cpp
	core-rules/cube_rules.cpp
	core-rules/cube_stacks.cpp
	core-rules/loot_rules.cpp
	core-rules/plugin.rc  # ABI manifest resource: D2RLoader rejects a plugin DLL without it
)
target_link_libraries(core_rules PRIVATE core_common)
target_compile_definitions(core_rules PRIVATE CORE_PLUGIN_NAME="core-rules" CORE_PLUGIN_VERSION="${CORE_RULES_VERSION}")
set_target_properties(core_rules PROPERTIES OUTPUT_NAME "core-rules")
core_options(core_rules)

set(CORE_MAPS_VERSION "0.1.0" CACHE STRING "core-maps PluginInfo::version")
add_library(core_maps SHARED
	core-maps/plugin.cpp
	core-maps/cube_ops.cpp
	core-maps/map_open.cpp
	core-maps/map_stats.cpp
	core-maps/map_events.cpp
	core-maps/entity_pool.cpp
	core-maps/key_drops.cpp
	core-maps/lucion.cpp
	core-maps/prime_minions.cpp
	core-maps/lucion_chests.cpp
	core-maps/rathma.cpp
	core-maps/uber_tristram.cpp
	core-maps/unique_maps.cpp
	core-maps/zhar.cpp
	core-maps/lucion_ai.cpp
	core-maps/level_fx.cpp
	core-maps/rathma_ai.cpp
	core-maps/rathma_life.cpp
	core-maps/dclone_ai.cpp
	core-maps/ai_hooks.cpp
	core-maps/event_portals.cpp
	core-maps/uber_scaling.cpp
	core-maps/uber_drops.cpp
	core-maps/plugin.rc
)
target_link_libraries(core_maps PRIVATE core_common)
target_compile_definitions(core_maps PRIVATE CORE_PLUGIN_NAME="core-maps" CORE_PLUGIN_VERSION="${CORE_MAPS_VERSION}")
set_target_properties(core_maps PROPERTIES OUTPUT_NAME "core-maps")
core_options(core_maps)

# Client-only: not Shared, so its version need not match between host and client.
set(CORE_UI_VERSION "0.1.0" CACHE STRING "core-ui PluginInfo::version")
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
