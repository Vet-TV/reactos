# SPDX-License-Identifier: GPL-2.0-or-later

# vetvdm.exe and its path-resolution library.
#
# This directory is intentionally not pulled in by modules/CMakeLists.txt.
# Wiring it into the top-level ReactOS build (add_subdirectory(vettv/vetvdm))
# is Build & Kernel's job. Do not add that call from here, and do not install
# the unit tests on the CD.

add_library(vetvdm_paths STATIC vetvdm_paths.c)
target_compile_definitions(vetvdm_paths PRIVATE
    UNICODE _UNICODE _WIN32_WINNT=0x0502 WINVER=0x0502)
target_include_directories(vetvdm_paths PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(vetvdm_core STATIC
    core/buf.c
    core/utf8.c
    core/sha256.c
    core/quote.c
    core/policy.c
    core/profile.c
    core/profile_json.c
    core/conf.c
    core/conf_emit.c
    core/dryrun.c
    core/log.c)
target_compile_definitions(vetvdm_core PRIVATE
    UNICODE _UNICODE _WIN32_WINNT=0x0502 WINVER=0x0502)
target_include_directories(vetvdm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_executable(vetvdm
    vetvdm/main.c
    vetvdm/hostutil.c
    vetvdm/host_route.c
    vetvdm/host_dosbox.c
    vetvdm/host_doscmd.c
    vetvdm/host_wine.c
    vetvdm/host_ntvdm.c
    vetvdm/host_log.c
    vetvdm/cmdline.c
    vetvdm/cmdline_norm.c
    vetvdm/cmdline_quote.c
    vetvdm/image.c
    vetvdm/route.c
    vetvdm/ask.c
    vetvdm/image_io.c
    core/policy_reg.c
    core/launch.c
    core/tempdir.c)
# Console subsystem: ReactOS cmd waits only for console programs.
set_module_type(vetvdm win32cui UNICODE)
target_compile_definitions(vetvdm PRIVATE _WIN32_WINNT=0x0502 WINVER=0x0502)
target_include_directories(vetvdm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/vetvdm
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(vetvdm vetvdm_core vetvdm_paths)
add_importlibs(vetvdm user32 advapi32 shell32 msvcrt kernel32)

add_executable(vetvdm_test
    core/tests/test_vetvdm.c
    core/tests/test_support.c
    core/tests/test_cmdline.c
    core/tests/test_core.c
    vetvdm/cmdline.c
    vetvdm/cmdline_norm.c
    vetvdm/cmdline_quote.c
    vetvdm/image.c
    vetvdm/route.c)
set_module_type(vetvdm_test win32cui UNICODE)
target_compile_definitions(vetvdm_test PRIVATE _WIN32_WINNT=0x0502 WINVER=0x0502)
target_include_directories(vetvdm_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/vetvdm
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(vetvdm_test vetvdm_core)
add_importlibs(vetvdm_test msvcrt kernel32)
# Host-side unit test. No add_cd_file: it does not belong on the boot image.

add_executable(vetvdm_paths_test vetvdm_paths_test.c)
set_module_type(vetvdm_paths_test win32cui UNICODE)
target_compile_definitions(vetvdm_paths_test PRIVATE _WIN32_WINNT=0x0502 WINVER=0x0502)
target_link_libraries(vetvdm_paths_test vetvdm_paths)
add_importlibs(vetvdm_paths_test shell32 msvcrt kernel32)
# Host-side unit test. No add_cd_file: it does not belong on the boot image.

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(_vetvdm_tgt vetvdm_paths vetvdm_core vetvdm vetvdm_test vetvdm_paths_test)
        target_compile_options(${_vetvdm_tgt} PRIVATE -std=gnu99)
    endforeach()
endif()
