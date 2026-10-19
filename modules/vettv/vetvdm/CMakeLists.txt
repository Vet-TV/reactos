# SPDX-License-Identifier: GPL-2.0-or-later

# Path-resolution core for vetvdm.exe.
#
# This directory is intentionally not pulled in by modules/CMakeLists.txt.
# Wiring it into the top-level ReactOS build (add_subdirectory(vettv/vetvdm))
# is Build & Kernel's job, once vetvdm.exe is ready to join the boot image.
# Do not add that call from here, and do not install the unit test on the CD.

add_library(vetvdm_paths STATIC vetvdm_paths.c)
target_compile_definitions(vetvdm_paths PRIVATE UNICODE _UNICODE)
target_include_directories(vetvdm_paths PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(vetvdm_paths_test vetvdm_paths_test.c)
set_module_type(vetvdm_paths_test win32cui UNICODE)
target_link_libraries(vetvdm_paths_test vetvdm_paths)
add_importlibs(vetvdm_paths_test shell32 msvcrt kernel32)
# Host-side unit test. No add_cd_file: it does not belong on the boot image.
