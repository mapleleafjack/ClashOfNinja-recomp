cmake_minimum_required(VERSION 3.20)

# A tagged release passes the tag as the version, so the bundle reports it, and CI passes
# 0.0.0 for a branch build. A plain configure is a development build too: 0.0.0 keeps it
# from offering to replace itself with the published release.
if(NOT NLA_VERSION)
  set(NLA_VERSION "0.0.0")
endif()
project(NarutoLauncher VERSION ${NLA_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(WIN32)
  enable_language(RC)
endif()

find_package(Threads REQUIRED)

# SDL2 gives the launcher a window, an event loop, a clipboard and drag and drop on every
# desktop. The game needs it anyway, so it is not a new dependency for the project.
find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND AND WIN32)
  # gcn-recomp builds SDL2 from source once per machine; reuse that copy if it is there.
  file(GLOB _sdl2_prefixes LIST_DIRECTORIES true "$ENV{LOCALAPPDATA}/gcn-recomp/SDL2-*")
  # A prefix is built beside its work directory, which holds no installed files.
  list(FILTER _sdl2_prefixes EXCLUDE REGEX "-work$")
  foreach(_prefix IN LISTS _sdl2_prefixes)
    if(NOT SDL2_FOUND)
      find_package(SDL2 CONFIG QUIET PATHS "${_prefix}" NO_DEFAULT_PATH)
      set(SDL2_DIR "${_prefix}" CACHE PATH "Directory holding SDL2Config.cmake" FORCE)
    endif()
  endforeach()
endif()
if(NOT SDL2_FOUND)
  message(FATAL_ERROR
    "SDL2 was not found. Install it (macOS: brew install sdl2, Debian/Ubuntu: "
    "sudo apt install libsdl2-dev) or configure with -DSDL2_DIR=<dir holding SDL2Config.cmake>.")
endif()

if(WIN32 AND TARGET SDL2::SDL2-static)
  set(NLA_SDL2 SDL2::SDL2-static)
elseif(APPLE AND TARGET SDL2::SDL2-static)
  # A bundle that carries SDL2 needs no rpath fix-up, so prefer the static library when
  # the package provides one.
  set(NLA_SDL2 SDL2::SDL2-static)
elseif(TARGET SDL2::SDL2)
  if(APPLE)
    message(WARNING
      "SDL2 is being linked dynamically, so this .app only runs where SDL2 is installed. "
      "Build it statically for a release: launcher/packaging/build-sdl2.ps1, then configure "
      "with -DSDL2_DIR=<prefix>/lib/cmake/SDL2.")
  endif()
  set(NLA_SDL2 SDL2::SDL2)
else()
  set(NLA_SDL2 SDL2::SDL2-static)
endif()

set(NLA_SOURCES
  src/main.cpp
  src/app.cpp
  src/disc.cpp
  src/font.cpp
  src/git.cpp
  src/image.cpp
  src/painter.cpp
  src/jobs.cpp
  src/project.cpp
  src/settings.cpp
  src/shortcut.cpp
  src/tools.cpp
  src/tools_package.cpp
  src/update.cpp
  src/util.cpp
  src/window.cpp
)

if(WIN32)
  add_executable(NarutoLauncher WIN32 ${NLA_SOURCES})
elseif(APPLE)
  add_executable(NarutoLauncher MACOSX_BUNDLE ${NLA_SOURCES})
else()
  add_executable(NarutoLauncher ${NLA_SOURCES})
endif()

# The mark next to the title, so a single executable still shows it.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/logo.png" _logo_hex HEX)
string(LENGTH "${_logo_hex}" _logo_hex_length)
math(EXPR _logo_bytes "${_logo_hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _logo_array "${_logo_hex}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/logo_data.h"
  "// Generated from src/logo.png by CMake. Do not edit.\n"
  "static const unsigned char kLogoPng[] = {${_logo_array}};\n"
  "static const unsigned long kLogoPngSize = ${_logo_bytes};\n")

target_include_directories(NarutoLauncher PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_BINARY_DIR}"
)

# The tool archive the launcher can fetch for itself (src/tools_package.cpp). A release
# passes the URL, digest and size that `build-release.ps1 -Split` printed; a plain
# configure leaves them empty, and then the launcher only ever looks for tools that are
# already installed.
set(NLA_TOOLS_URL "" CACHE STRING "URL of the Windows build-tools archive")
set(NLA_TOOLS_SHA256 "" CACHE STRING "SHA-256 of that archive")
set(NLA_TOOLS_BYTES 0 CACHE STRING "Size of that archive in bytes")
if(WIN32)
  if(NLA_TOOLS_BYTES STREQUAL "")
    set(_nla_tools_bytes 0)
  else()
    set(_nla_tools_bytes ${NLA_TOOLS_BYTES})
  endif()
  target_compile_definitions(NarutoLauncher PRIVATE
    NLA_TOOLS_URL="${NLA_TOOLS_URL}"
    NLA_TOOLS_SHA256="${NLA_TOOLS_SHA256}"
    NLA_TOOLS_BYTES=${_nla_tools_bytes})
endif()

# The tag the release was built from, which the update check compares against, and the
# commit, which tells Settings whether the project folder's source is older than this.
set(NLA_SOURCE "")
set(NLA_SOURCE_TIME 0)
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    OUTPUT_VARIABLE _nla_commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} log -1 --format=%ct HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    OUTPUT_VARIABLE _nla_time
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(_nla_commit)
    set(NLA_SOURCE "${_nla_commit}")
  endif()
  if(_nla_time MATCHES "^[0-9]+$")
    set(NLA_SOURCE_TIME "${_nla_time}")
  endif()
endif()

target_compile_definitions(NarutoLauncher PRIVATE
  SDL_MAIN_HANDLED NLA_VERSION="${PROJECT_VERSION}" NLA_SOURCE="${NLA_SOURCE}"
  NLA_SOURCE_TIME=${NLA_SOURCE_TIME})

if(WIN32)
  configure_file(src/launcher.rc.in ${CMAKE_CURRENT_BINARY_DIR}/launcher.rc @ONLY)
  target_sources(NarutoLauncher PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/launcher.rc)

  target_compile_definitions(NarutoLauncher PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
  )

  if(MINGW)
    target_compile_options(NarutoLauncher PRIVATE -Wall -Wextra -Wno-unused-parameter
      -finput-charset=UTF-8 -fexec-charset=UTF-8)
    # SDL2's prefix is built with debug info, which would take the debug sections and
    # 43k symbols of it into the release: -s keeps them out and halves the exe.
    target_link_options(NarutoLauncher PRIVATE -static -static-libgcc -static-libstdc++
      -municode -s)

    # MinGW's driver adds lib/default-manifest.o to every link, and the linker refuses
    # a second manifest. Ship ours under that name and let -B prefer it.
    set(MANIFEST_RC ${CMAKE_CURRENT_BINARY_DIR}/manifest.rc)
    file(WRITE ${MANIFEST_RC}
         "1 RT_MANIFEST \"${CMAKE_CURRENT_SOURCE_DIR}/app.manifest\"\n")
    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/default-manifest.o
      COMMAND ${CMAKE_RC_COMPILER} -O coff -i ${MANIFEST_RC}
              -o ${CMAKE_CURRENT_BINARY_DIR}/default-manifest.o
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/app.manifest ${MANIFEST_RC})
    add_custom_target(NarutoManifest DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/default-manifest.o)
    add_dependencies(NarutoLauncher NarutoManifest)
    target_link_options(NarutoLauncher PRIVATE -B${CMAKE_CURRENT_BINARY_DIR})
  else()
    target_sources(NarutoLauncher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app.manifest)
    target_compile_options(NarutoLauncher PRIVATE /W4 /permissive- /utf-8)
  endif()

  target_link_libraries(NarutoLauncher PRIVATE
    ${NLA_SDL2}
    dwmapi comctl32 shell32 shlwapi ole32 uuid user32 gdi32 advapi32 propsys
    winhttp bcrypt
  )
else()
  target_compile_options(NarutoLauncher PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_link_libraries(NarutoLauncher PRIVATE ${NLA_SDL2} Threads::Threads)
endif()

if(APPLE)
  set_target_properties(NarutoLauncher PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Naruto Launcher"
    MACOSX_BUNDLE_GUI_IDENTIFIER "local.naruto.launcher"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
  )
endif()

set_target_properties(NarutoLauncher PROPERTIES OUTPUT_NAME "NarutoLauncher")

# The render mode draws a frame without a window, so the drawing can be checked on a
# machine with no desktop. Needs one usable system font.
enable_testing()
add_test(NAME render-main
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/render-main.bmp")
add_test(NAME render-settings
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/render-settings.bmp" settings)
add_test(NAME render-picker
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/render-picker.bmp" picker)
# The click mode paints once to build the hit list, clicks a point, then paints the
# result, so it covers hit testing and activation as well as drawing.
add_test(NAME click-settings
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/click-settings.bmp" click:990,42)
add_test(NAME click-details
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/click-details.bmp" click:530,765)
add_test(NAME click-choose-image
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/click-choose-image.bmp" settings-click:789,280)
add_test(NAME render-players
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/render-players.bmp" players)
# Picks a pad for the second controller on the panel: opens its menu, then takes item 2.
add_test(NAME click-players
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/click-players.bmp"
                 "players-click:735,433;690,510")
add_test(NAME render-update
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/render-update.bmp" update)
add_test(NAME render-update-banner
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/render-update-banner.bmp" update-banner)
# Clicks the Update button in the header, which must open the prompt.
add_test(NAME click-update
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/click-update.bmp" update-click:871,41)

# The update check's offline half: version comparison, reading a release out of GitHub's
# JSON, and the helper that swaps a downloaded version in. It runs the helper for real
# against a stand-in install, so the swap is covered without a network.
add_executable(update_test
  tests/update_test.cpp
  src/git.cpp
  src/jobs.cpp
  src/tools.cpp
  src/update.cpp
  src/util.cpp
)
target_include_directories(update_test PRIVATE src)
if(WIN32)
  target_compile_definitions(update_test PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(update_test PRIVATE shell32 ole32 advapi32 user32)
  if(MINGW)
    target_link_options(update_test PRIVATE -static -municode)
  endif()
else()
  target_link_libraries(update_test PRIVATE Threads::Threads)
endif()
add_test(NAME update-logic COMMAND $<TARGET_FILE:update_test>)

install(TARGETS NarutoLauncher
  BUNDLE DESTINATION .
  RUNTIME DESTINATION .
)

# A developer tool that runs the launcher's extractor from a console, so its output can
# be compared with tools/extract_disc.py. Build it with: ninja -C build extract_test
add_executable(extract_test EXCLUDE_FROM_ALL
  tests/extract_test.cpp
  src/disc.cpp
  src/util.cpp
)
target_include_directories(extract_test PRIVATE src)
if(WIN32)
  target_compile_definitions(extract_test PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(extract_test PRIVATE shell32 ole32 advapi32 user32)
  if(MINGW)
    target_link_options(extract_test PRIVATE -static -municode)
  endif()
else()
  # disc.cpp hashes with util.cpp's SHA-1, which is byte-order independent.
  target_link_libraries(extract_test PRIVATE Threads::Threads)
endif()
