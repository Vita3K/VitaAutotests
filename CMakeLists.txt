cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	if(NOT DEFINED ENV{VITASDK})
		message(FATAL_ERROR "VITASDK is not set")
	endif()
	set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")
endif()

project(vita-autotests C)

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -Werror)

# Adds a test. Its sources are built into the single test vpk, with their main renamed to test_<name>.
# The test passes when it exits with 0 and prints what <name>.expected, next to its first source, holds.
function(vita_test name)
	foreach(source ${ARGN})
		get_filename_component(path ${source} ABSOLUTE)
		set_source_files_properties(${path} PROPERTIES COMPILE_DEFINITIONS "main=test_${name}")
		set_property(GLOBAL APPEND PROPERTY VITA_TEST_SOURCES ${path})
	endforeach()
	set_property(GLOBAL APPEND PROPERTY VITA_TEST_NAMES ${name})
endfunction()

# Builds every test added so far into one vpk with the given title ID. Each launch runs one test.
function(vita_tests_vpk title_id)
	get_property(sources GLOBAL PROPERTY VITA_TEST_SOURCES)
	get_property(names GLOBAL PROPERTY VITA_TEST_NAMES)
	set(table "")
	foreach(name ${names})
		string(APPEND table "TEST(${name})\n")
	endforeach()
	file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tests.inc CONTENT "${table}")

	add_executable(vita-autotests ${PROJECT_SOURCE_DIR}/common/main.c ${sources})
	target_include_directories(vita-autotests PRIVATE ${PROJECT_SOURCE_DIR}/common ${CMAKE_CURRENT_BINARY_DIR})
	target_link_libraries(vita-autotests SceLibKernel_stub SceKernelThreadMgr_stub SceIofilemgr_stub SceAppMgr_stub)
	vita_create_self(vita-autotests.self vita-autotests)
	vita_create_vpk(vita-autotests.vpk ${title_id} vita-autotests.self NAME vita-autotests)
endfunction()

add_subdirectory(tests)
