# Note that we can't use CMake 3.19.0 due to this issue: https://gitlab.kitware.com/cmake/cmake/-/issues/21529
# error: 'includes target "squirrel" more than once in the export set'
# This happens to be the version bundled with VS 2019 16.2.

cmake_minimum_required(VERSION 3.19.2)
project(sample_app)

set(CMAKE_CXX_STANDARD 17)

################################
# Executable
add_executable(sample_app
    "main.cpp"  )
    
################################
# Dependencies
include(FetchContent)

FetchContent_Declare(
        plog
        GIT_REPOSITORY https://github.com/SergiusTheBest/plog.git
        # 1.1.5
        GIT_TAG f4c22b03d5d3aa753cca8e716636ac4eb29b0917
)
FetchContent_MakeAvailable(plog)

target_link_libraries(${PROJECT_NAME}
        plog::plog
        sdb::interfaces
        sdb::embedded_server
        sdb::squirrel_debugger)