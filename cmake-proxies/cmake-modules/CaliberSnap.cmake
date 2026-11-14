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
if( NOT AUDACITY_CALIBER_CARGO OR NOT AUDACITY_CALIBER_RUSTC )
   message( FATAL_ERROR "Caliber Snap requires cargo and rustc on PATH" )
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
find_package( Threads REQUIRED )
add_library( audacity-caliber-snap STATIC "${AUDACITY_CALIBER_ROOT}/src/caliber/SnapBridge.cpp" )
target_compile_features( audacity-caliber-snap PUBLIC cxx_std_17 )
target_include_directories( audacity-caliber-snap PUBLIC "${AUDACITY_CALIBER_ROOT}/src/caliber" )
target_link_libraries( audacity-caliber-snap PUBLIC audacity-caliber-ffi Threads::Threads ${CMAKE_DL_LIBS} )
if( CMAKE_SYSTEM_NAME STREQUAL "Linux" )
   target_link_libraries( audacity-caliber-snap PUBLIC m )
endif()
