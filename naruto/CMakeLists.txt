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
