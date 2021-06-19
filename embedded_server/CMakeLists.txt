
cmake_minimum_required(VERSION 3.19.2)
project(embedded_server)

set(CMAKE_CXX_STANDARD 17)

add_library(${PROJECT_NAME} PUBLIC
    "include/sdb/EmbeddedServer.h"
    "EmbeddedServer.cpp"
    "SwaggerComponent.h"
    "Logger.h"
    "Logger.cpp"
    "AppComponents.h"
    "AppComponents.cpp"
    "controller/TestAppController.h"
    "dto/EventDto.h"
    "websocket/WSListener.h"
    "websocket/WSListener.cpp"
)

add_library(sdb::embedded_server ALIAS embedded_server)

target_include_directories(${PROJECT_NAME}
        INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
