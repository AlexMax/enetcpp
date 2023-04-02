add_executable(tests tests.cpp doctest.h)
target_link_libraries(tests PRIVATE enetcpp)

if(CLANG_TIDY_EXE)
    set_target_properties(tests PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()
