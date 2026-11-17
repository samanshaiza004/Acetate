#[[
Experimental headless backend for the Alicorn-owned host window spike.
Opt-in with -Daudacity_host_spike=ON; see docs/research/host-spike.md.
It builds one shared library, with a plain C ABI, over existing Audacity
libraries. It does not change the Audacity application target.
]]

add_library( acetate-host-backend SHARED
   backend/AcetateBackend.cpp
   backend/AcetateBackend.h
   "${CMAKE_SOURCE_DIR}/tests/MockedPrefs.cpp"
   "${CMAKE_SOURCE_DIR}/tests/MockedPrefs.h"
)

target_include_directories( acetate-host-backend
   PRIVATE
      "${CMAKE_SOURCE_DIR}/tests"
      "${CMAKE_CURRENT_SOURCE_DIR}/backend"
)

target_link_libraries( acetate-host-backend
   PRIVATE
      lib-project
      lib-project-file-io
      lib-wave-track
      lib-audio-io
      lib-stretching-sequence
      lib-preferences-interface
      lib-file-formats
      lib-sqlite-helpers
      SndFile::sndfile
)

if( TARGET portaudio )
   target_link_libraries( acetate-host-backend PRIVATE portaudio )
endif()

set_target_properties( acetate-host-backend
   PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/host-spike/lib"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/host-spike/lib"
      BUILD_RPATH "${_DESTDIR}/${_PKGLIB}"
      CXX_VISIBILITY_PRESET hidden
      # The host executable finds this library through its own rpath, not
      # through an application bundle.
      INSTALL_NAME_DIR "@rpath"
      BUILD_WITH_INSTALL_NAME_DIR ON
)

if( APPLE )
   # Audacity's libraries record @loader_path/../Frameworks as their install
   # path, so the spike library sits in a sibling directory whose Frameworks
   # entry points at the real library directory instead of joining the bundle.
   add_custom_command( TARGET acetate-host-backend POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E create_symlink
         "${_DESTDIR}/${_PKGLIB}" "${CMAKE_BINARY_DIR}/host-spike/Frameworks"
      VERBATIM
   )
endif()
