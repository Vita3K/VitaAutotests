# The Vita side of the USB link that run_tests.py runs the tests over: a kernel plugin that frees the
# USB port and turns MTP off, and a SceShell plugin that opens the USB serial port, takes commands
# from it and launches the test app. Both are safe modules, since SceShell doesn't load an unsafe one.

add_executable(vita-autotests-usb kernel.c)
target_link_libraries(vita-autotests-usb taihenForKernel_stub taihenModuleUtils_stub SceIofilemgrForDriver_stub)
target_link_options(vita-autotests-usb PRIVATE -nostdlib)
vita_create_self(vita-autotests-usb.skprx vita-autotests-usb CONFIG kernel_exports.yml)

# Stubs for the shell plugin to call the kernel plugin's syscall. vita_create_stubs needs make, so
# the generated stubs are built here instead.
set(stubs_dir ${CMAKE_CURRENT_BINARY_DIR}/stubs)
set(stub_sources ${stubs_dir}/vita_autotests_usb_VitaAutotestsUsb_vitaAutotestsUsbStopUdc.S)
add_custom_command(
	OUTPUT ${stub_sources}
	COMMAND ${VITA_ELF_EXPORT} kernel $<TARGET_FILE:vita-autotests-usb> ${CMAKE_CURRENT_SOURCE_DIR}/kernel_exports.yml ${CMAKE_CURRENT_BINARY_DIR}/stubs.yml
	COMMAND ${VITA_LIBS_GEN} -c ${CMAKE_CURRENT_BINARY_DIR}/stubs.yml ${stubs_dir}
	DEPENDS vita-autotests-usb kernel_exports.yml
	COMMENT "Generating stubs for vita-autotests-usb"
)
set_source_files_properties(${stub_sources} PROPERTIES COMPILE_DEFINITIONS GEN_WEAK_EXPORTS=0)
add_library(vita-autotests-usb-stubs STATIC ${stub_sources})

add_executable(vita-autotests-shell shell.c)
target_link_libraries(vita-autotests-shell vita-autotests-usb-stubs SceUsbSerial_stub SceLibKernel_stub SceKernelThreadMgr_stub SceIofilemgr_stub SceAppMgr_stub)
target_link_options(vita-autotests-shell PRIVATE -nostdlib)
vita_create_self(vita-autotests-shell.suprx vita-autotests-shell CONFIG shell_exports.yml)
