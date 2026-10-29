cmake_minimum_required(VERSION 3.20)
project(gnt4 C CXX)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# The runtime and the recompiler are shared with the other games' projects.
add_subdirectory(../naruto/gcn-recomp gcn-recomp)
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
