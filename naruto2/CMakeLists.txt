cmake_minimum_required(VERSION 3.20)
project(naruto2 C CXX)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# The runtime and the recompiler are shared with the first game's project.
add_subdirectory(../naruto/gcn-recomp gcn-recomp)
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
