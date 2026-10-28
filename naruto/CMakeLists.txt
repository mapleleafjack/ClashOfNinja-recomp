cmake_minimum_required(VERSION 3.20)
project(naruto C CXX)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_subdirectory(gcn-recomp)
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
