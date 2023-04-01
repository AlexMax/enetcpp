set(DOCTEST_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/include")
set(DOCTEST_ARCHIVE "${CMAKE_CURRENT_SOURCE_DIR}/doctest.h.zip")

file(MAKE_DIRECTORY "${DOCTEST_INCLUDE}")
add_custom_command(OUTPUT "${DOCTEST_INCLUDE}/doctest.h"
    COMMAND "${CMAKE_COMMAND}" -E tar xf "${DOCTEST_ARCHIVE}" --format=zip
    WORKING_DIRECTORY "${DOCTEST_INCLUDE}"
    VERBATIM)
add_custom_target(doctest_header DEPENDS "${DOCTEST_INCLUDE}/doctest.h")

add_library(doctest INTERFACE "${DOCTEST_INCLUDE}/doctest.h")
add_dependencies(doctest doctest_header)
target_include_directories(doctest INTERFACE "${DOCTEST_INCLUDE}")

add_executable(tests tests.cpp)
target_link_libraries(tests PRIVATE enetcpp doctest)
