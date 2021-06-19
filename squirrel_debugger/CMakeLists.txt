cmake_minimum_required(VERSION 3.19.2)
project(squirrel_debugger)

set(CMAKE_CXX_STANDARD 17)

add_library(${PROJECT_NAME} PUBLIC

add_library(
    "include/qdb/SquirrelDebugger.h" 
    "SquirrelDebugger.cpp"
)
add_library(qdb::squirrel_debugger ALIAS squirrel_debugger)

target_include_directories(${PROJECT_NAME}
        INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
