// Generated from the four game projects: the files the launcher writes when it makes
// a game folder, so a player needs no checkout. The symbol maps are not here -- dtk
// makes those from the player's own disc.
#include "project_templates.h"

#include <array>

namespace nla {
namespace {

const char knaruto_CMakeLists_txt[] =
    R"NLA(cmake_minimum_required(VERSION 3.20)
project(naruto C CXX)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_subdirectory(../gcn-recomp gcn-recomp)
gcn_enable_languages()

gcn_add_game(
  NAME naruto
  TITLE "Naruto: Clash of Ninja"
  LABEL "Naruto: Clash of Ninja Recomp"
  DISC_ID GNREDA
  DOL_SHA1 19251f7f15f8b6f7e0cd1c753409151ec49d9af2
  SYMBOLS ${CMAKE_SOURCE_DIR}/analysis/symbols.txt
  TABLES ${CMAKE_SOURCE_DIR}/recomp
  FPS 60)

# gcn-recomp links the MinGW runtime dynamically, so a built naruto.exe only starts when
# libgcc_s_seh-1.dll, libstdc++-6.dll and libwinpthread-1.dll are on PATH — which they are
# under the launcher, and not when the exe is run on its own. Static, as the launcher
# already builds itself.
if(MINGW)
  foreach(game naruto naruto_bench)
    if(TARGET ${game})
      target_link_options(${game} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
  endforeach()
endif()
)NLA";

const char knaruto_recomp_hle_txt[] =
    R"NLA(# functions whose body is replaced by a runtime implementation (hle_<name>)
OSSaveContext
OSLoadContext
OSReport
OSPanic
OSResetSystem
)NLA";

const char knaruto_recomp_idle_txt[] =
    R"NLA(# Backward branches where the guest is only waiting for an interrupt, so the runtime may
# skip guest time to the next event instead of iterating.
800E954C SelectThread
)NLA";

const char knaruto_recomp_names_txt[] =
    R"NLA(# hand-named functions: ADDR name
800E4184 OSSaveContext
800E48E4 OSPanic)NLA";

const char knaruto_recomp_patches_txt[] =
    R"NLA(# ADDR  <C statement> replaces the instruction at ADDR
)NLA";

const char knaruto_recomp_special_calls_txt[] =
    R"NLA(# call sites to these get setjmp treatment (context resume points)
# (dtk reports OSSaveContext at 0x800E4184; this is the function address, as in the
# reference projects, not the address of the bl that calls it)
800E4184 OSSaveContext
)NLA";

const char knaruto2_CMakeLists_txt[] =
    R"NLA(cmake_minimum_required(VERSION 3.20)
project(naruto2 C CXX)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# The runtime and the recompiler are shared with the first game's project.
add_subdirectory(../gcn-recomp gcn-recomp)
gcn_enable_languages()

gcn_add_game(
  NAME naruto2
  TITLE "Naruto: Clash of Ninja 2"
  LABEL "Naruto: Clash of Ninja 2 Recomp"
  DISC_ID GNUEDA
  DOL_SHA1 2440b77fad91e1892d1769ff8650852244e64415
  SYMBOLS ${CMAKE_SOURCE_DIR}/analysis/symbols.txt
  TABLES ${CMAKE_SOURCE_DIR}/recomp
  FPS 60)

# gcn-recomp links the MinGW runtime dynamically, so a built naruto2.exe only starts when
# libgcc_s_seh-1.dll, libstdc++-6.dll and libwinpthread-1.dll are on PATH — which they are
# under the launcher, and not when the exe is run on its own. Static, as the launcher
# already builds itself.
if(MINGW)
  foreach(game naruto2 naruto2_bench)
    if(TARGET ${game})
      target_link_options(${game} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
  endforeach()
endif()
)NLA";

const char knaruto2_recomp_hle_txt[] =
    R"NLA(# functions whose body is replaced by a runtime implementation (hle_<name>)
OSSaveContext
OSLoadContext
OSReport
OSPanic
OSResetSystem
)NLA";

const char knaruto2_recomp_idle_txt[] =
    R"NLA(# Backward branches where the guest is only waiting for an interrupt, so the runtime may
# skip guest time to the next event instead of iterating.
# SelectThread starts at 800FA2E0; this is the two-instruction idle spin inside it, at the
# same offset as 800E954C in SelectThread of the first game.
800FA434 SelectThread
)NLA";

const char knaruto2_recomp_names_txt[] =
    R"NLA(# hand-named functions: ADDR name
800F506C OSSaveContext
800F57CC OSPanic)NLA";

const char knaruto2_recomp_patches_txt[] =
    R"NLA(# ADDR  <C statement> replaces the instruction at ADDR
)NLA";

const char knaruto2_recomp_special_calls_txt[] =
    R"NLA(# call sites to these get setjmp treatment (context resume points)
# (dtk leaves OSSaveContext unnamed; matched to the first game's function by its body.
# This is the function address, as in the reference projects, not the address of the bl
# that calls it)
800F506C OSSaveContext
)NLA";

const char kgnt3_CMakeLists_txt[] =
    R"NLA(cmake_minimum_required(VERSION 3.20)
project(gnt3 C CXX)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# The runtime and the recompiler are shared with the other games' projects.
add_subdirectory(../gcn-recomp gcn-recomp)
gcn_enable_languages()

gcn_add_game(
  NAME gnt3
  TITLE "Naruto: Gekitou Ninja Taisen! 3"
  LABEL "Naruto: Gekitou Ninja Taisen! 3 Recomp"
  DISC_ID G3NJDA
  DOL_SHA1 e8e0a27f6a4a075a11ef322cf6a182c0b87c8d51
  SYMBOLS ${CMAKE_SOURCE_DIR}/analysis/symbols.txt
  TABLES ${CMAKE_SOURCE_DIR}/recomp
  FPS 60)

# gcn-recomp links the MinGW runtime dynamically, so a built gnt3.exe only starts when
# libgcc_s_seh-1.dll, libstdc++-6.dll and libwinpthread-1.dll are on PATH — which they are
# under the launcher, and not when the exe is run on its own. Static, as the launcher
# already builds itself.
if(MINGW)
  foreach(game gnt3 gnt3_bench)
    if(TARGET ${game})
      target_link_options(${game} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
  endforeach()
endif()
)NLA";

const char kgnt3_recomp_hle_txt[] =
    R"NLA(# functions whose body is replaced by a runtime implementation (hle_<name>)
OSSaveContext
OSLoadContext
OSReport
OSPanic
OSResetSystem
)NLA";

const char kgnt3_recomp_idle_txt[] =
    R"NLA(# Backward branches where the guest is only waiting for an interrupt, so the runtime may
# skip guest time to the next event instead of iterating.
# SelectThread starts at 8013D9E8; this is the two-instruction idle spin inside it, at the
# same offset as 800FA434 in the second game and 800E954C in the first.
8013DB3C SelectThread
)NLA";

const char kgnt3_recomp_names_txt[] =
    R"NLA(# hand-named functions: ADDR name
801389F4 OSSaveContext
80139154 OSPanic)NLA";

const char kgnt3_recomp_patches_txt[] =
    R"NLA(# ADDR  <C statement> replaces the instruction at ADDR
)NLA";

const char kgnt3_recomp_special_calls_txt[] =
    R"NLA(# call sites to these get setjmp treatment (context resume points)
# (dtk leaves OSSaveContext unnamed; matched to the other games' function by its body.
# This is the function address, as in the reference projects, not the address of the bl
# that calls it)
801389F4 OSSaveContext
)NLA";

const char kgnt4_CMakeLists_txt[] =
    R"NLA(cmake_minimum_required(VERSION 3.20)
project(gnt4 C CXX)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# The runtime and the recompiler are shared with the other games' projects.
add_subdirectory(../gcn-recomp gcn-recomp)
gcn_enable_languages()

gcn_add_game(
  NAME gnt4
  TITLE "Naruto: Gekitou Ninja Taisen! 4"
  LABEL "Naruto: Gekitou Ninja Taisen! 4 Recomp"
  DISC_ID G4NJDA
  DOL_SHA1 0eb4cc3bd4c7363f8cb7baff79de7a964cbdeadb
  SYMBOLS ${CMAKE_SOURCE_DIR}/analysis/symbols.txt
  TABLES ${CMAKE_SOURCE_DIR}/recomp
  FPS 60)

# gcn-recomp links the MinGW runtime dynamically, so a built gnt4.exe only starts when
# libgcc_s_seh-1.dll, libstdc++-6.dll and libwinpthread-1.dll are on PATH — which they are
# under the launcher, and not when the exe is run on its own. Static, as the launcher
# already builds itself.
if(MINGW)
  foreach(game gnt4 gnt4_bench)
    if(TARGET ${game})
      target_link_options(${game} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
  endforeach()
endif()
)NLA";

const char kgnt4_recomp_hle_txt[] =
    R"NLA(# functions whose body is replaced by a runtime implementation (hle_<name>)
OSSaveContext
OSLoadContext
OSReport
OSPanic
OSResetSystem
)NLA";

const char kgnt4_recomp_idle_txt[] =
    R"NLA(# Backward branches where the guest is only waiting for an interrupt, so the runtime may
# skip guest time to the next event instead of iterating.
# SelectThread starts at 8016C1AC; this is the two-instruction idle spin inside it, at the
# same offset as 800FA434 in the second game and 800E954C in the first.
8016C300 SelectThread
)NLA";

const char kgnt4_recomp_names_txt[] =
    R"NLA(# hand-named functions: ADDR name
801671B8 OSSaveContext
80167918 OSPanic)NLA";

const char kgnt4_recomp_patches_txt[] =
    R"NLA(# ADDR  <C statement> replaces the instruction at ADDR
)NLA";

const char kgnt4_recomp_special_calls_txt[] =
    R"NLA(# call sites to these get setjmp treatment (context resume points)
# (dtk leaves OSSaveContext unnamed; matched to the other games' function by its body.
# This is the function address, as in the reference projects, not the address of the bl
# that calls it)
801671B8 OSSaveContext
)NLA";

const std::array<GameTemplate, 4> kTemplates = {{
    {L"naruto",
     {{
         {L"CMakeLists.txt", knaruto_CMakeLists_txt},
         {L"recomp/hle.txt", knaruto_recomp_hle_txt},
         {L"recomp/idle.txt", knaruto_recomp_idle_txt},
         {L"recomp/names.txt", knaruto_recomp_names_txt},
         {L"recomp/patches.txt", knaruto_recomp_patches_txt},
         {L"recomp/special_calls.txt", knaruto_recomp_special_calls_txt},
     }}},
    {L"naruto2",
     {{
         {L"CMakeLists.txt", knaruto2_CMakeLists_txt},
         {L"recomp/hle.txt", knaruto2_recomp_hle_txt},
         {L"recomp/idle.txt", knaruto2_recomp_idle_txt},
         {L"recomp/names.txt", knaruto2_recomp_names_txt},
         {L"recomp/patches.txt", knaruto2_recomp_patches_txt},
         {L"recomp/special_calls.txt", knaruto2_recomp_special_calls_txt},
     }}},
    {L"gnt3",
     {{
         {L"CMakeLists.txt", kgnt3_CMakeLists_txt},
         {L"recomp/hle.txt", kgnt3_recomp_hle_txt},
         {L"recomp/idle.txt", kgnt3_recomp_idle_txt},
         {L"recomp/names.txt", kgnt3_recomp_names_txt},
         {L"recomp/patches.txt", kgnt3_recomp_patches_txt},
         {L"recomp/special_calls.txt", kgnt3_recomp_special_calls_txt},
     }}},
    {L"gnt4",
     {{
         {L"CMakeLists.txt", kgnt4_CMakeLists_txt},
         {L"recomp/hle.txt", kgnt4_recomp_hle_txt},
         {L"recomp/idle.txt", kgnt4_recomp_idle_txt},
         {L"recomp/names.txt", kgnt4_recomp_names_txt},
         {L"recomp/patches.txt", kgnt4_recomp_patches_txt},
         {L"recomp/special_calls.txt", kgnt4_recomp_special_calls_txt},
     }}},
}};

}  // namespace

const std::vector<GameTemplate>& game_templates() {
    static const std::vector<GameTemplate> templates(kTemplates.begin(), kTemplates.end());
    return templates;
}

}  // namespace nla
