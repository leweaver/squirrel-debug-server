# Note that we can't use CMake 3.19.0 due to this issue: https://gitlab.kitware.com/cmake/cmake/-/issues/21529
# error: 'includes target "squirrel" more than once in the export set'
# This happens to be the version bundled with VS 2019 16.2.

cmake_minimum_required(VERSION 3.19.2)
project(vscode_quirrel_debugger)

set(CMAKE_CXX_STANDARD 17)

set(SDB_BUILD_TESTING OFF CACHE BOOL "Enable unit tests")

if(SDB_BUILD_TESTING)
    enable_testing()
endif()

add_subdirectory(interfaces)
add_subdirectory(embedded_server)
add_subdirectory(squirrel_debugger)
add_subdirectory(sample_app)