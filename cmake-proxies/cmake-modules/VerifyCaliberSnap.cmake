# SPDX-License-Identifier: GPL-2.0-or-later
execute_process(
   COMMAND "${GIT_EXECUTABLE}" -C "${CALIBER_SOURCE}" rev-parse HEAD
   RESULT_VARIABLE result OUTPUT_VARIABLE revision ERROR_VARIABLE error
   OUTPUT_STRIP_TRAILING_WHITESPACE )
if( NOT result EQUAL 0 OR NOT revision STREQUAL EXPECTED_REVISION )
   message( FATAL_ERROR "Caliber source must be a Git checkout at ${EXPECTED_REVISION}; got '${revision}'. ${error}" )
endif()
execute_process(
   COMMAND "${GIT_EXECUTABLE}" -C "${CALIBER_SOURCE}" status --porcelain --untracked-files=all --
      Cargo.toml Cargo.lock include/caliber.h crates/caliber-core crates/caliber-ffi
   RESULT_VARIABLE result OUTPUT_VARIABLE changes ERROR_VARIABLE error )
if( NOT result EQUAL 0 OR changes )
   message( FATAL_ERROR "Caliber build inputs must be clean at ${EXPECTED_REVISION}: ${changes} ${error}" )
endif()
