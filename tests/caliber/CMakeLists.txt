# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required( VERSION 3.15 )
project( AudacityCaliberFocusedTests LANGUAGES CXX )
include( "${CMAKE_CURRENT_LIST_DIR}/../../cmake-proxies/cmake-modules/CaliberSnap.cmake" )
enable_testing()
add_executable( snap-bridge-test SnapBridgeTest.cpp )
target_link_libraries( snap-bridge-test PRIVATE audacity-caliber-snap )
add_test( NAME snap-bridge-test COMMAND snap-bridge-test )
