find_program(DOXYGEN_EXECUTABLE NAMES doxygen REQUIRED)

set(AWESOME_CSS_DIR ${CMAKE_SOURCE_DIR}/third_party/doxygen-awesome-css)

set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

add_custom_target(docs
    COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generate Doxygen documentation into ${CMAKE_CURRENT_BINARY_DIR}/html"
    VERBATIM)
