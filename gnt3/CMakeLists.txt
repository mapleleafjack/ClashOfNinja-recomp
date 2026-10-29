cmake_minimum_required(VERSION 3.20)
project(gnt3 C CXX)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# The runtime and the recompiler are shared with the other games' projects.
add_subdirectory(../naruto/gcn-recomp gcn-recomp)
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
