add_executable(tests tests.cpp doctest.h)
target_link_libraries(tests PRIVATE enetcpp)
