cmake_minimum_required(VERSION 3.21)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(instaseek
  VERSION 1.0.0
  DESCRIPTION "InstaSeek - instant file name and file content search for Windows"
  LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "InstaSeek is a Windows-only program: it reads the NTFS MFT and USN journal through Win32 APIs.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR
    "InstaSeek must be built as a 64-bit (x64) program, but the active compiler produces "
    "${CMAKE_SIZEOF_VOID_P}-byte pointers, i.e. a 32-bit (x86) build.\n"
    "How to fix:\n"
    "  1. Delete the build directory (build/<preset>), CMake caches the compiler choice.\n"
    "  2. Use an x64 environment: \"x64 Native Tools Command Prompt for VS 2022\", or\n"
    "     Developer PowerShell started with: Enter-VsDevShell ... -DevCmdArguments '-arch=x64 -host_arch=x64',\n"
    "     or run vcvars64.bat from <VS>\\VC\\Auxiliary\\Build before cmake.\n"
    "  3. Or use the Visual Studio generator preset, which forces x64: cmake --preset vs2022")
endif()

if(DEFINED VCPKG_TARGET_TRIPLET AND NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static")
  message(WARNING
    "VCPKG_TARGET_TRIPLET is '${VCPKG_TARGET_TRIPLET}'. InstaSeek is designed for 'x64-windows-static' "
    "(static CRT, single portable .exe). Other triplets may produce DLL dependencies or CRT mismatches.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(INSTASEEK_BUILD_TESTS "Build the unit tests" ON)

find_package(ftxui CONFIG QUIET)
find_package(re2 CONFIG QUIET)
if(NOT ftxui_FOUND OR NOT re2_FOUND)
  message(FATAL_ERROR
    "Dependencies not found (ftxui: '${ftxui_FOUND}', re2: '${re2_FOUND}').\n"
    "Configure through a preset so the vcpkg toolchain is used:\n"
    "  cmake --preset release\n"
    "The presets expect the VCPKG_ROOT environment variable to point to your vcpkg clone.\n"
    "Manual alternative: -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake "
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static")
endif()

set(INSTASEEK_MSVC_CXX_FLAGS
  /W4 /permissive- /utf-8 /sdl /guard:cf /EHsc /Zc:__cplusplus /Zc:inline /external:W0)
set(INSTASEEK_MSVC_LINK_FLAGS
  /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /guard:cf /CETCOMPAT)

function(instaseek_apply_build_policy target)
  target_compile_definitions(${target} PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00
    INSTASEEK_VERSION="${PROJECT_VERSION}")
  if(MSVC)
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${INSTASEEK_MSVC_CXX_FLAGS}>")
    target_link_options(${target} PRIVATE ${INSTASEEK_MSVC_LINK_FLAGS})
  endif()
endfunction()

add_library(instaseek_core STATIC
  src/core/palette.cpp
  src/core/utf8.cpp
  src/core/keyboard_layout.cpp
  src/core/file_kind.cpp
  src/core/format.cpp
  src/core/encoding.cpp
  src/core/text_matcher.cpp
  src/core/chunk_search.cpp
  src/core/file_index.cpp
  src/core/name_query.cpp
  src/core/line_index.cpp
  src/core/job_runner.cpp
  src/core/line_editor.cpp
)
target_include_directories(instaseek_core PUBLIC src)
target_link_libraries(instaseek_core PUBLIC re2::re2)
instaseek_apply_build_policy(instaseek_core)

add_library(instaseek_platform STATIC
  src/platform/win.cpp
  src/platform/mapped_file.cpp
  src/platform/ntfs_volume.cpp
  src/platform/directory_walker.cpp
  src/platform/index_service.cpp
  src/platform/content_scanner.cpp
  src/platform/document.cpp
  src/platform/clipboard.cpp
  src/platform/shell.cpp
)
target_link_libraries(instaseek_platform PUBLIC instaseek_core advapi32 shell32 user32 ole32 uuid)
instaseek_apply_build_policy(instaseek_platform)

add_executable(instaseek
  src/main.cpp
  src/ui/app.cpp
  src/ui/theme.cpp
  src/ui/widgets.cpp
  src/ui/splash.cpp
  src/ui/controls.cpp
  src/ui/result_list.cpp
  src/ui/files_page.cpp
  src/ui/content_page.cpp
  src/ui/viewer_page.cpp
  src/ui/text_field.cpp
  src/ui/context_menu.cpp
  src/instaseek.rc
  src/instaseek.manifest
)
target_link_libraries(instaseek PRIVATE
  instaseek_platform
  ftxui::component ftxui::dom ftxui::screen)
instaseek_apply_build_policy(instaseek)
if(MSVC)
  target_link_options(instaseek PRIVATE /MANIFESTUAC:NO)
endif()

if(INSTASEEK_BUILD_TESTS)
  enable_testing()
  add_executable(instaseek_tests
    tests/test_harness.cpp
    tests/palette_tests.cpp
    tests/utf8_tests.cpp
    tests/keyboard_layout_tests.cpp
    tests/file_kind_tests.cpp
    tests/format_tests.cpp
    tests/encoding_tests.cpp
    tests/matcher_tests.cpp
    tests/chunk_search_tests.cpp
    tests/file_index_tests.cpp
    tests/name_query_tests.cpp
    tests/line_index_tests.cpp
    tests/line_editor_tests.cpp
  )
  target_include_directories(instaseek_tests PRIVATE tests)
  target_link_libraries(instaseek_tests PRIVATE instaseek_core)
  instaseek_apply_build_policy(instaseek_tests)
  add_test(NAME instaseek_tests COMMAND instaseek_tests)
endif()
