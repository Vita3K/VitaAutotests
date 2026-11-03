cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	if(NOT DEFINED ENV{VITASDK})
		message(FATAL_ERROR "VITASDK is not set")
	endif()
	set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")
endif()

project(vita-autotests C ASM)

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

# Adds a file the tests need to the vpk, at dest inside it
function(vita_test_file source dest)
	set_property(GLOBAL APPEND PROPERTY VITA_TEST_FILES ${source} ${dest})
endfunction()

# Builds every test added so far into one vpk with the given title ID. Each launch runs one test.
function(vita_tests_vpk title_id)
	get_property(sources GLOBAL PROPERTY VITA_TEST_SOURCES)
	get_property(names GLOBAL PROPERTY VITA_TEST_NAMES)
	get_property(files GLOBAL PROPERTY VITA_TEST_FILES)
	set(table "")
	foreach(name ${names})
		string(APPEND table "TEST(${name})\n")
	endforeach()
	file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tests.inc CONTENT "${table}")

	# The shader binaries the render tests draw with, as arrays named <name>_gxp in shaders.c
	set(shaders "#include \"render.h\"\n")
	foreach(name clear_v clear_f color_v color_f texture_v texture_f)
		set(gxp ${PROJECT_SOURCE_DIR}/common/shaders/${name}.gxp)
		set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${gxp})
		file(READ ${gxp} hex HEX)
		string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")
		string(APPEND shaders "static const unsigned char ${name}_data[] __attribute__((aligned(16))) = { ${bytes} };\n")
		string(APPEND shaders "const SceGxmProgram *const ${name}_gxp = (const SceGxmProgram *)${name}_data;\n")
	endforeach()
	file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders.c CONTENT "${shaders}" @ONLY)

	add_executable(vita-autotests ${PROJECT_SOURCE_DIR}/common/main.c ${PROJECT_SOURCE_DIR}/common/capture.c
		${PROJECT_SOURCE_DIR}/common/render.c ${CMAKE_CURRENT_BINARY_DIR}/shaders.c ${sources})
	target_include_directories(vita-autotests PRIVATE ${PROJECT_SOURCE_DIR}/common ${CMAKE_CURRENT_BINARY_DIR})
	# Not SceProcessmgr_stub: a Vita refuses to install a vpk that imports from it, with C2-12845-0
	target_link_libraries(vita-autotests SceLibKernel_stub SceKernelThreadMgr_stub SceIofilemgr_stub SceAppMgr_stub SceSysmem_stub SceFiber_stub SceSysmodule_stub SceRtc_stub SceAudio_stub SceDisplay_stub SceGxm_stub SceAppUtil_stub SceFios2Kernel_stub)
	# Room for the import tables vita-elf-create adds after the code, which outgrow the page padding
	target_link_options(vita-autotests PRIVATE -Wl,--defsym,__sce_headroom=0x4000)
	vita_create_self(vita-autotests.self vita-autotests)
	# Save data calls need a quota, without one every slot ID fails the same way
	set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d SAVEDATA_MAX_SIZE=1024")
	vita_create_vpk(vita-autotests.vpk ${title_id} vita-autotests.self NAME vita-autotests FILE ${files})
endfunction()

add_subdirectory(tests)
add_subdirectory(usb)
