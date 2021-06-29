
cmake_minimum_required(VERSION 3.19.2)
project(interfaces)

set(CMAKE_CXX_STANDARD 17)

add_library(${PROJECT_NAME} INTERFACE
    "include/sdb/MessageInterface.h" "include/sdb/LogInterface.h")
add_library(sdb::interfaces ALIAS interfaces)

target_include_directories(${PROJECT_NAME}
        INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
