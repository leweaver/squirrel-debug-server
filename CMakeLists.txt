# Note that we can't use CMake 3.19.0 due to this issue: https://gitlab.kitware.com/cmake/cmake/-/issues/21529
# error: 'includes target "squirrel" more than once in the export set'
# This happens to be the version bundled with VS 2019 16.2.

cmake_minimum_required(VERSION 3.19.2)
project(quirrel_debugger)

set(CMAKE_CXX_STANDARD 17)

################################
# Executable
add_executable(quirrel_debugger main.cpp)

################################
# Dependencies
include(FetchContent)

# quirrel
FetchContent_Declare(
        quirrel
        GIT_REPOSITORY https://github.com/leweaver/quirrel.git
)

#set(SQ_DISABLE_CMAKE_INSTALLER OFF CACHE INTERNAL "")
#set(SQ_DISABLE_INSTALLER OFF CACHE INTERNAL "")
#set(DISABLE_DYNAMIC  OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(quirrel)

#FetchContent_GetProperties(quirrel)
#if(NOT quirrel_POPULATED)
#    FetchContent_Populate(quirrel)
    
    #add_subdirectory(${quirrel_SOURCE_DIR}/squirrel)
    #add_subdirectory(${quirrel_SOURCE_DIR}/sqstdlib)
    #add_subdirectory(${quirrel_SOURCE_DIR}/sqmodules)
    #add_subdirectory(${quirrel_SOURCE_DIR}/sq)
#    add_subdirectory(${quirrel_SOURCE_DIR} ${quirrel_BINARY_DIR})
#    MESSAGE(INFO ${quirrel_BINARY_DIR})
#endif()

target_link_libraries(${PROJECT_NAME} squirrel::squirrel_static squirrel::sqstdlib_static)