cmake_minimum_required(VERSION 3.21)

project(EFSPresetConverter VERSION 1.0.0 LANGUAGES CXX)

if (PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(FATAL_ERROR "In-source builds are not allowed.")
endif ()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/version.rc @ONLY)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(Boost_USE_STATIC_LIBS ON)

if (NOT TARGET CommonLibF4)
	add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/CommonLibF4RD/CommonLibF4" CommonLibF4)
endif ()

find_package(fmt REQUIRED CONFIG)
find_package(spdlog REQUIRED CONFIG)

# Preset conversion with no game code, shared by the plugin and the command-line test tool.
add_library(EFSConvert STATIC src/Convert.cpp src/Convert.h)
target_compile_features(EFSConvert PUBLIC cxx_std_23)
target_include_directories(EFSConvert PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

option(EFS_AUTOTEST "Build the in-game self-test into the plugin (test builds only, never for release)" OFF)

set(SOURCES
	src/PCH.h
	src/main.cpp
	src/Plugin.cpp
	src/Plugin.h
	src/PresetHook.cpp
	src/PresetHook.h
)
if (EFS_AUTOTEST)
	list(APPEND SOURCES src/AutoTest.cpp src/AutoTest.h)
endif ()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/version.rc)

target_compile_definitions(${PROJECT_NAME} PRIVATE _UNICODE $<$<BOOL:${EFS_AUTOTEST}>:EFS_AUTOTEST>)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
# The library's src folder gives access to REL::RuntimeDatabase, which the plugin uses to check the database before loading it.
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/external/CommonLibF4RD/CommonLibF4/src)
target_link_libraries(${PROJECT_NAME} PRIVATE EFSConvert CommonLibF4::CommonLibF4 fmt::fmt spdlog::spdlog)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

add_executable(efs_convert_cli tests/ConvertCli.cpp)
target_link_libraries(efs_convert_cli PRIVATE EFSConvert)

if (MSVC)
	foreach (target IN ITEMS ${PROJECT_NAME} EFSConvert efs_convert_cli)
		target_compile_options(${target} PRIVATE
			/sdl /utf-8 /Zi /permissive- /Zc:preprocessor /W4 /WX /external:W0
			"$<$<CONFIG:RELEASE>:/Zc:inline;/JMC->"
		)
	endforeach ()
	target_link_options(${PROJECT_NAME} PRIVATE
		/WX
		"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
		"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
	)
endif ()
