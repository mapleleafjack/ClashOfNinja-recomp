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
  # The launcher's window class, split by what each part of it does: the shell and its
  # input, the drawing, the file picker, the jobs it runs, the games it lists, the mods it
  # applies, and its own updates. The window draws through ui.cpp, which holds the widgets
  # and the hit list, so nothing it paints has to be an App.
  src/app.cpp
  src/app_geckos.cpp
  src/app_games.cpp
  src/app_jobs.cpp
  src/app_mods.cpp
  src/app_paint.cpp
  src/app_picker.cpp
  src/app_update.cpp
  src/catalog.cpp
  src/codes.cpp
  src/covers.cpp
  src/dialog.cpp
  src/devices.cpp
  src/disc.cpp
  src/font.cpp
  src/gecko.cpp
  src/git.cpp
  src/image.cpp
  src/painter.cpp
  src/picker.cpp
  src/jobs.cpp
  src/mods.cpp
  src/patches.cpp
  # The delta reader is the runtime's own, so a patch the launcher says fits is read
  # exactly as the game will read it.
  ../gcn-recomp/runtime/hw/patch.cpp
  src/project.cpp
  src/project_templates.cpp
  src/settings.cpp
  src/tools.cpp
  src/tools_package.cpp
  src/ui.cpp
  src/update.cpp
  src/util.cpp
  src/window.cpp
)

if(APPLE)
  # The folder and image chooser is NSOpenPanel, which needs AppKit and so one
  # Objective-C++ source in an otherwise C++ application.
  set(CMAKE_OBJCXX_STANDARD 20)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
  set(CMAKE_OBJCXX_EXTENSIONS OFF)
  enable_language(OBJCXX)
  list(APPEND NLA_SOURCES src/dialog_mac.mm)
  find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
endif()

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
  "${CMAKE_CURRENT_SOURCE_DIR}/../gcn-recomp/runtime/hw"
  "${CMAKE_CURRENT_BINARY_DIR}"
)

# The tool archive the launcher can fetch for itself (src/tools_package.cpp). A release
# passes the URL, digest and size that `build-release.ps1 -Split` printed; a plain
# configure leaves them empty, and then the launcher only ever looks for tools that are
# already installed.
set(NLA_TOOLS_URL "" CACHE STRING "URL of this platform's build-tools archive")
set(NLA_TOOLS_SHA256 "" CACHE STRING "SHA-256 of that archive")
set(NLA_TOOLS_BYTES 0 CACHE STRING "Size of that archive in bytes")
# Windows and Linux fetch it; a macOS release carries the tools inside the bundle instead.
if(WIN32 OR (UNIX AND NOT APPLE))
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

# NLA_VERSION is the version of the copy of the game's sources a macOS bundle lays down,
# so an updated launcher replaces its own sources and nothing else.
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
  if(APPLE)
    target_link_libraries(NarutoLauncher PRIVATE ${COCOA_FRAMEWORK})
  endif()
endif()

if(APPLE)
  # Finder, the Dock and Launchpad take the icon from the bundle, so the .icns is copied
  # into Resources and named in Info.plist. It is the same mark the window draws; remake
  # it with iconutil from src/logo.png at 16, 32, 64, 128, 256, 512 and 1024 pixels.
  set(NLA_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/AppIcon.icns")
  set_source_files_properties("${NLA_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  target_sources(NarutoLauncher PRIVATE "${NLA_ICON}")

  set_target_properties(NarutoLauncher PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Naruto Launcher"
    MACOSX_BUNDLE_GUI_IDENTIFIER "local.naruto.launcher"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
    MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
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
# The same panel as a 2560x1417 window draws it: the whole interface zooms with the window.
add_test(NAME render-settings-large
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/render-settings-large.bmp" settings 2560x1417)
add_test(NAME render-picker
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/render-picker.bmp" picker)
# The click mode paints once to build the hit list, clicks a point, then paints the
# result, so it covers hit testing and activation as well as drawing.
add_test(NAME click-settings
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/click-settings.bmp" click:990,42)
add_test(NAME click-details
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/click-details.bmp" click:530,665)
add_test(NAME render-players
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/render-players.bmp" players)
# Picks the first controller for player 1: opens its menu, then takes item 2.
add_test(NAME click-players
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/click-players.bmp"
                 "players-click:739,358;739,471")
# The Mods panel, and checking its first row and pressing Apply: the rows are drawn from
# stand-ins in a render, so the drawing and the click path are the same on every machine.
add_test(NAME render-mods
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/render-mods.bmp" mods)
add_test(NAME click-mods
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/click-mods.bmp"
                 "mods-click:255,350;671,548")
# The Mods button belongs to the game's page: it opens that game's panel, and Escape closes
# it back to the page rather than to Settings.
add_test(NAME mods-from-game
         COMMAND $<TARGET_FILE:NarutoLauncher> --render
                 "${CMAKE_CURRENT_BINARY_DIR}/mods-from-game.bmp" mods-from-game)
# The Geckos panel: one code switched on by itself, a block pasted into the box, and the
# panel the game page opens. The rows are stand-ins in a render, so these are the same on
# every machine.
add_test(NAME render-geckos
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/render-geckos.bmp" geckos)
add_test(NAME click-geckos
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/click-geckos.bmp"
                 "geckos-click:300,340;691,577")
# The box takes the click and the text the window hands it, which is the path a paste takes.
add_test(NAME geckos-paste
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/geckos-paste.bmp" geckos-paste)
add_test(NAME geckos-from-game
         COMMAND $<TARGET_FILE:NarutoLauncher> --render
                 "${CMAKE_CURRENT_BINARY_DIR}/geckos-from-game.bmp" geckos-from-game)
add_test(NAME render-update
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/render-update.bmp" update)
add_test(NAME render-update-banner
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/render-update-banner.bmp" update-banner)
# Clicks the Update button in the header, which must open the prompt.
add_test(NAME click-update
         COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/click-update.bmp" update-click:871,41)
# Clicks a cover: the page must leave the game it is on for the one clicked, whether or not
# that game's folder is set up yet, so choosing a game cannot land on another one.
foreach(game_index RANGE 0 4)
  add_test(NAME click-cover-${game_index}
           COMMAND $<TARGET_FILE:NarutoLauncher> --render "${CMAKE_CURRENT_BINARY_DIR}/click-cover-${game_index}.bmp" cover-click:${game_index})
endforeach()

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

# The mod loader's offline half: reading a mod.txt and laying its files over an extracted
# disc's manifest. Needs no game and no network.
add_executable(mods_test
  tests/mods_test.cpp
  src/catalog.cpp
  src/codes.cpp
  src/gecko.cpp
  src/mods.cpp
  src/util.cpp
  ../gcn-recomp/runtime/hw/patch.cpp
)
target_include_directories(mods_test PRIVATE src ../gcn-recomp/runtime/hw)
if(WIN32)
  target_compile_definitions(mods_test PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(mods_test PRIVATE shell32 ole32 advapi32 user32)
  if(MINGW)
    target_link_options(mods_test PRIVATE -static -municode)
  endif()
else()
  target_link_libraries(mods_test PRIVATE Threads::Threads)
endif()
add_test(NAME mods-logic COMMAND $<TARGET_FILE:mods_test>)

# The Gecko codes' half: codetypes become the operations the runtime runs and the words
# recompilation applies, and what cannot be done is said rather than dropped.
add_executable(gecko_test
  tests/gecko_test.cpp
  src/gecko.cpp
  src/util.cpp
)
target_include_directories(gecko_test PRIVATE src)
if(WIN32)
  target_compile_definitions(gecko_test PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(gecko_test PRIVATE shell32 ole32 advapi32 user32)
  if(MINGW)
    target_link_options(gecko_test PRIVATE -static -municode)
  endif()
else()
  target_link_libraries(gecko_test PRIVATE Threads::Threads)
endif()
add_test(NAME gecko-logic COMMAND $<TARGET_FILE:gecko_test>)

# Which game a project folder answers with: a game the launcher knows is the game asked
# for even before its folder is written, so the cover grid cannot silently land on another
# game. Needs no disc and no network.
# What the Activity panel keeps: every rule for splitting and trimming a job's output, which
# used to live inside the window where nothing could reach it.
add_executable(activity_test
  tests/activity_test.cpp
  src/jobs.cpp
  src/util.cpp
)
target_include_directories(activity_test PRIVATE src)
if(WIN32)
  target_compile_definitions(activity_test PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(activity_test PRIVATE shell32 ole32 advapi32 user32)
  if(MINGW)
    target_link_options(activity_test PRIVATE -static -municode)
  endif()
else()
  target_link_libraries(activity_test PRIVATE Threads::Threads)
endif()
add_test(NAME activity-logic COMMAND $<TARGET_FILE:activity_test>)

# What the launcher's own picker lists: the order, and what it leaves out. Needs no window,
# only a folder made for the purpose.
# What the box art cache keeps: the stand-in tile it draws, and that a game's folder is read
# once including when the answer is "no art here".
add_executable(covers_test
  tests/covers_test.cpp
  src/covers.cpp
  src/image.cpp
  src/painter.cpp
  src/font.cpp
  src/util.cpp
)
target_include_directories(covers_test PRIVATE src)
if(WIN32)
  target_compile_definitions(covers_test PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(covers_test PRIVATE shell32 ole32 advapi32 user32)
  if(MINGW)
    target_link_options(covers_test PRIVATE -static -municode)
  endif()
else()
  target_link_libraries(covers_test PRIVATE Threads::Threads)
endif()
add_test(NAME covers-logic COMMAND $<TARGET_FILE:covers_test>)

add_executable(picker_test
  tests/picker_test.cpp
  src/picker.cpp
  src/painter.cpp
  src/font.cpp
  src/util.cpp
)
target_include_directories(picker_test PRIVATE src)
if(WIN32)
  target_compile_definitions(picker_test PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(picker_test PRIVATE shell32 ole32 advapi32 user32)
  if(MINGW)
    target_link_options(picker_test PRIVATE -static -municode)
  endif()
else()
  target_link_libraries(picker_test PRIVATE Threads::Threads)
endif()
add_test(NAME picker-logic COMMAND $<TARGET_FILE:picker_test>)

add_executable(project_test
  tests/project_test.cpp
  src/project.cpp
  src/project_templates.cpp
  src/util.cpp
)
target_include_directories(project_test PRIVATE src)
if(WIN32)
  target_compile_definitions(project_test PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(project_test PRIVATE shell32 ole32 advapi32 user32)
  if(MINGW)
    target_link_options(project_test PRIVATE -static -municode)
  endif()
else()
  target_link_libraries(project_test PRIVATE Threads::Threads)
endif()
add_test(NAME project-logic COMMAND $<TARGET_FILE:project_test>)

# Which disc a boot accepts: a patch is a disc of its own and may rename the ID it carries,
# so the check is made against the tree the patch decodes from. The runtime has no test
# project of its own, and this one needs nothing but its header.
add_executable(boot_test
  tests/boot_test.cpp
)
target_include_directories(boot_test PRIVATE ../gcn-recomp/runtime)
if(WIN32)
  target_compile_definitions(boot_test PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  if(MINGW)
    target_link_options(boot_test PRIVATE -static -municode)
  endif()
endif()
add_test(NAME boot-logic COMMAND $<TARGET_FILE:boot_test>)

# The recompiler's side of the same file: the words a mod's codes write into code are laid
# over the DOL before it is translated, and words for data are left to the runtime.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME mods-patches
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../gcn-recomp/recomp/modspatch.py)
endif()

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
