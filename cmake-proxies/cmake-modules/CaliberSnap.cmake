# SPDX-License-Identifier: GPL-2.0-or-later
# Shared by Audacity and the standalone C ABI bridge tests.
include_guard( GLOBAL )
include( "${CMAKE_CURRENT_LIST_DIR}/CaliberSnapPin.cmake" )
get_filename_component( AUDACITY_CALIBER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE )
set( AUDACITY_CALIBER_SOURCE "" CACHE PATH "Clean pinned Caliber source checkout" )
if( NOT EXISTS "${AUDACITY_CALIBER_SOURCE}/include/caliber.h" )
   message( FATAL_ERROR "Set AUDACITY_CALIBER_SOURCE to the pinned Caliber Git checkout" )
endif()
if( WIN32 OR CMAKE_CROSSCOMPILING OR ( NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" ) )
   message( FATAL_ERROR "Experimental Caliber Snap currently supports native macOS and Linux builds only" )
endif()
find_package( Git REQUIRED )
find_program( AUDACITY_CALIBER_CARGO cargo )
find_program( AUDACITY_CALIBER_RUSTC rustc )
find_program( AUDACITY_CALIBER_ODIN odin )
if( NOT AUDACITY_CALIBER_CARGO OR NOT AUDACITY_CALIBER_RUSTC OR NOT AUDACITY_CALIBER_ODIN )
   message( FATAL_ERROR "Caliber Snap requires cargo, rustc, and odin on PATH" )
endif()
if( NOT CMAKE_SIZEOF_VOID_P EQUAL 8 )
   message( FATAL_ERROR "Caliber Snap currently requires a 64-bit native build" )
endif()
execute_process( COMMAND "${AUDACITY_CALIBER_RUSTC}" --version OUTPUT_VARIABLE rust_version )
if( NOT rust_version MATCHES "rustc ([0-9]+\\.[0-9]+\\.[0-9]+)" OR CMAKE_MATCH_1 VERSION_LESS "1.85.0" )
   message( FATAL_ERROR "Caliber requires Rust 1.85 or later (edition 2024): ${rust_version}" )
endif()
if( APPLE AND CMAKE_OSX_ARCHITECTURES )
   execute_process( COMMAND "${AUDACITY_CALIBER_RUSTC}" -vV OUTPUT_VARIABLE rust_info )
   if( CMAKE_OSX_ARCHITECTURES STREQUAL "arm64" )
      set( expected_rust_arch "aarch64" )
   elseif( CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64" )
      set( expected_rust_arch "x86_64" )
   else()
      message( FATAL_ERROR "Caliber Snap does not yet support universal macOS builds" )
   endif()
   if( NOT rust_info MATCHES "host: ${expected_rust_arch}-apple-darwin" )
      message( FATAL_ERROR "Rust host does not match CMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}" )
   endif()
endif()
if( APPLE )
   if( CMAKE_OSX_ARCHITECTURES STREQUAL "arm64" OR
       ( NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$" ) )
      set( AUDACITY_CALIBER_ODIN_TARGET darwin_arm64 )
   elseif( CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64" OR
           ( NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" ) )
      set( AUDACITY_CALIBER_ODIN_TARGET darwin_amd64 )
   else()
      message( FATAL_ERROR "Caliber Snap cannot determine a supported native Odin macOS target" )
   endif()
else()
   if( CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$" )
      set( AUDACITY_CALIBER_ODIN_TARGET linux_arm64 )
   elseif( CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" )
      set( AUDACITY_CALIBER_ODIN_TARGET linux_amd64 )
   else()
      message( FATAL_ERROR "Caliber Snap cannot determine a supported native Odin Linux target" )
   endif()
endif()
set( caliber_verify_command "${CMAKE_COMMAND}"
   "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}" "-DCALIBER_SOURCE=${AUDACITY_CALIBER_SOURCE}"
   "-DEXPECTED_REVISION=${AUDACITY_CALIBER_REVISION}"
   -P "${CMAKE_CURRENT_LIST_DIR}/VerifyCaliberSnap.cmake" )
execute_process( COMMAND ${caliber_verify_command} RESULT_VARIABLE verify_result )
if( NOT verify_result EQUAL 0 )
   message( FATAL_ERROR "Caliber source verification failed" )
endif()
set( caliber_target_dir "${CMAKE_BINARY_DIR}/caliber-target" )
set( caliber_archive "${caliber_target_dir}/release/${CMAKE_STATIC_LIBRARY_PREFIX}caliber_ffi${CMAKE_STATIC_LIBRARY_SUFFIX}" )
# Always run cargo's incremental check and recheck the source pin. No network.
add_custom_target( audacity-caliber-cargo
   COMMAND ${caliber_verify_command}
   COMMAND "${CMAKE_COMMAND}" -E env "RUSTC=${AUDACITY_CALIBER_RUSTC}"
      "${AUDACITY_CALIBER_CARGO}" build --manifest-path "${AUDACITY_CALIBER_SOURCE}/Cargo.toml"
      --package caliber-ffi --release --locked --offline --target-dir "${caliber_target_dir}"
   BYPRODUCTS "${caliber_archive}"
   VERBATIM )
add_library( audacity-caliber-ffi STATIC IMPORTED GLOBAL )
set_target_properties( audacity-caliber-ffi PROPERTIES IMPORTED_LOCATION "${caliber_archive}"
   INTERFACE_INCLUDE_DIRECTORIES "${AUDACITY_CALIBER_SOURCE}/include" )
add_dependencies( audacity-caliber-ffi audacity-caliber-cargo )
set( caliber_odin_dir "${CMAKE_BINARY_DIR}/caliber-odin" )
set( caliber_odin_archive "${caliber_odin_dir}/${CMAKE_STATIC_LIBRARY_PREFIX}audacity_odin_backend${CMAKE_STATIC_LIBRARY_SUFFIX}" )
add_custom_command(
   OUTPUT "${caliber_odin_archive}"
   COMMAND "${CMAKE_COMMAND}" -E make_directory "${caliber_odin_dir}"
   COMMAND "${AUDACITY_CALIBER_ODIN}" build "${AUDACITY_CALIBER_ROOT}/src/caliber"
      -build-mode:lib -no-entry-point "-target:${AUDACITY_CALIBER_ODIN_TARGET}"
      "-out:${caliber_odin_archive}"
   DEPENDS
      "${AUDACITY_CALIBER_ROOT}/src/caliber/OdinSnapBackend.odin"
      "${AUDACITY_CALIBER_ROOT}/src/caliber/OdinBackend.odin"
      "${AUDACITY_CALIBER_ROOT}/src/caliber/OdinSnapBackend.h"
      "${AUDACITY_CALIBER_ROOT}/src/caliber/OdinSelectionTimeBackend.h"
      "${AUDACITY_CALIBER_ROOT}/src/caliber/OdinBackend.h"
      "${AUDACITY_CALIBER_SOURCE}/include/caliber.h"
   WORKING_DIRECTORY "${AUDACITY_CALIBER_ROOT}"
   VERBATIM )
add_custom_target( audacity-caliber-odin-build DEPENDS "${caliber_odin_archive}" )
add_library( audacity-caliber-odin STATIC IMPORTED GLOBAL )
set_target_properties( audacity-caliber-odin PROPERTIES IMPORTED_LOCATION "${caliber_odin_archive}" )
add_dependencies( audacity-caliber-odin audacity-caliber-odin-build )
find_package( Threads REQUIRED )
add_library( audacity-caliber-snap STATIC "${AUDACITY_CALIBER_ROOT}/src/caliber/SnapBridge.cpp" )
target_compile_features( audacity-caliber-snap PUBLIC cxx_std_17 )
target_include_directories( audacity-caliber-snap PUBLIC "${AUDACITY_CALIBER_ROOT}/src/caliber" )
target_link_libraries( audacity-caliber-snap PUBLIC audacity-caliber-odin audacity-caliber-ffi Threads::Threads ${CMAKE_DL_LIBS} )
if( CMAKE_SYSTEM_NAME STREQUAL "Linux" )
   target_link_libraries( audacity-caliber-snap PUBLIC m )
endif()
