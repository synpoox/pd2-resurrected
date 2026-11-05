# PD2 Resurrected's D2RLoader plugins (ADR 0004): one project, a static library of shared code (per-build RVAs,
# helpers) linked into each plugin DLL. Target: D2R 3.3 build 93847 + D2RLoader 1.3.1, PluginSDK in third_party/.
#
# Build:  powershell -ExecutionPolicy Bypass -File build.ps1
# Output: build/Release/core-rules.dll -> tools/install.ps1 copies it to <game>/d2rloader/plugins/.
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

add_library(core_common STATIC
	common/d2r.cpp
	common/log.cpp
	common/off_switch.cpp
)
target_include_directories(core_common PUBLIC common)
target_link_libraries(core_common PUBLIC D2RLPlugin::D2RLPlugin)
target_compile_features(core_common PUBLIC cxx_std_20)
core_options(core_common)

# Bump with every release: Shared plugins must match between host and client.
set(CORE_RULES_VERSION "0.1.0" CACHE STRING "core-rules PluginInfo::version")
add_library(core_rules SHARED
	core-rules/plugin.cpp
	core-rules/skill_functions.cpp
	core-rules/missile_functions.cpp
	core-rules/item_rules.cpp
	core-rules/merc_rules.cpp
	core-rules/plugin.rc  # ABI manifest resource: D2RLoader rejects a plugin DLL without it
)
target_link_libraries(core_rules PRIVATE core_common)
target_compile_definitions(core_rules PRIVATE CORE_PLUGIN_NAME="core-rules" CORE_PLUGIN_VERSION="${CORE_RULES_VERSION}")
set_target_properties(core_rules PROPERTIES OUTPUT_NAME "core-rules")
core_options(core_rules)
