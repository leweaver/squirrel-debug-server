cmake_minimum_required(VERSION 3.19.2)
project(squirrel_debugger)

set(CMAKE_CXX_STANDARD 17)

set(SDB_DEPENDENCIES_SQUIRREL "FETCHCONTENT" CACHE STRING "Location where to find quirrel modules. can be [INSTALLED|FETCHCONTENT]")
set(SDB_DEPENDENCIES_SQUIRREL_INSTALLED INSTALLED)
set(SDB_DEPENDENCIES_SQUIRREL_FETCHCONTENT FETCHCONTENT)

add_library(${PROJECT_NAME} STATIC
    "include/sdb/SquirrelDebugger.h" 
    "SquirrelDebugger.cpp"
 "BreakpointMap.h" "BreakpointMap.cpp" "SquirrelVmHelpers.h" "SquirrelVmHelpers.cpp")
add_library(sdb::squirrel_debugger ALIAS squirrel_debugger)

target_include_directories(${PROJECT_NAME}
        INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

################################
# Dependencies
include(FetchContent)

if(SDB_DEPENDENCIES_SQUIRREL STREQUAL SDB_DEPENDENCIES_SQUIRREL_FETCHCONTENT)
    message("Finding squirrel in location=FETCHCONTENT")
    # quirrel
    FetchContent_Declare(
            quirrel
            GIT_REPOSITORY https://github.com/leweaver/quirrel.git
    )
    FetchContent_MakeAvailable(quirrel)
elseif(SDB_DEPENDENCIES_SQUIRREL STREQUAL SDB_DEPENDENCIES_SQUIRREL_INSTALLED)
    message("Finding squirrel in location=INSTALLED")
else()
    message("FATAL_ERROR Unknown location to find squirrel '${SDB_DEPENDENCIES_SQUIRREL}'")
endif()

target_link_libraries(${PROJECT_NAME}
        sdb::interfaces
        squirrel::squirrel_static
        squirrel::sqstdlib_static)

#####
# Testing
#####
if(SDB_BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()