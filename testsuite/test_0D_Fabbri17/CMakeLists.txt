SET(TESTNAME test_fabbri)
add_executable(${TESTNAME} main.cpp)

set_target_properties(${TESTNAME} PROPERTIES  OUTPUT "test_fabbri")

include_directories ("${PROJECT_SOURCE_DIR}/src")
include_directories ("${LIBMESH_DIR}/include")

target_link_libraries(${TESTNAME} beatit)
target_link_libraries(${TESTNAME} ${LIBMESH_LIB})


SET_TARGET_PROPERTIES(${TESTNAME} PROPERTIES LINKER_LANGUAGE CXX)

SET(OctaveFile "${CMAKE_CURRENT_BINARY_DIR}/plot_variables.m")
IF ( ${CMAKE_CURRENT_SOURCE_DIR}/plot_variables.m  IS_NEWER_THAN ${OctaveFile} )
     CONFIGURE_FILE( ${CMAKE_CURRENT_SOURCE_DIR}/plot_variables.m  ${CMAKE_CURRENT_BINARY_DIR} COPYONLY)
ENDIF (${CMAKE_CURRENT_SOURCE_DIR}/plot_variables.m  IS_NEWER_THAN ${OctaveFile} )

