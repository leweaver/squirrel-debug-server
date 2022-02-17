cmake_minimum_required(VERSION 3.19.2)

project(squirrel_debugger_tests)

################################
# GTest
################################
include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        release-1.11.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(BUILD_GTEST ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

################################
# Tests
################################
# Add test cpp file
add_executable(${PROJECT_NAME} testmain.cpp DebuggerTestUtils.cpp DebuggerTestUtils.h)
# Link test executable against gtest & gtest_main
target_link_libraries(${PROJECT_NAME} gtest gtest_main sdb::squirrel_debugger)
enable_testing()
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

configure_file(test.nut test.nut COPYONLY)