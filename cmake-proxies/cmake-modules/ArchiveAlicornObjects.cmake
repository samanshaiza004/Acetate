# Archives the object files Odin emitted into a static library.
# Inputs: OBJECT_DIR, ARCHIVE, ARCHIVER.
file( GLOB alicorn_objects "${OBJECT_DIR}/*.obj" )
if( NOT alicorn_objects )
   message( FATAL_ERROR "No Odin object files found in ${OBJECT_DIR}" )
endif()
file( WRITE "${OBJECT_DIR}/objects.rsp" "" )
foreach( object IN LISTS alicorn_objects )
   file( APPEND "${OBJECT_DIR}/objects.rsp" "\"${object}\"\n" )
endforeach()
execute_process( COMMAND "${ARCHIVER}" /nologo "/OUT:${ARCHIVE}" "@${OBJECT_DIR}/objects.rsp"
   RESULT_VARIABLE archive_result )
if( NOT archive_result EQUAL 0 )
   message( FATAL_ERROR "Archiving Alicorn objects failed (${archive_result})" )
endif()
