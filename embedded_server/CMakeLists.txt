
cmake_minimum_required(VERSION 3.19.2)
project(embedded_server)

set(CMAKE_CXX_STANDARD 17)

set(SDB_DEPENDENCIES_OATPP "FETCHCONTENT" CACHE STRING "Location where to find oatpp modules. can be [INSTALLED|FETCHCONTENT]")
set(SDB_DEPENDENCIES_OATPP_INSTALLED INSTALLED)
set(SDB_DEPENDENCIES_OATPP_FETCHCONTENT FETCHCONTENT)

add_library(${PROJECT_NAME} STATIC
    "include/sdb/EmbeddedServer.h"
    "EmbeddedServer.cpp"
    "SwaggerComponent.h"
    "ForwardingLogger.h"
    "ForwardingLogger.cpp"
    "AppComponents.h"
    "AppComponents.cpp"
    "controller/DebugCommandController.h"
    "dto/EventDto.h"
    "websocket/WSListener.h"
    "websocket/WSListener.cpp"
    "controller/StaticController.h"
    "controller/WebsocketController.h")

add_library(sdb::embedded_server ALIAS embedded_server)

target_include_directories(${PROJECT_NAME}
        INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

################################
# Dependencies
include(FetchContent)

if(SDB_DEPENDENCIES_OATPP STREQUAL SDB_DEPENDENCIES_OATPP_FETCHCONTENT)
    message("Finding oatpp in location=FETCHCONTENT")
    # oatpp
    FetchContent_Declare(
            oatpp
            GIT_REPOSITORY https://github.com/oatpp/oatpp.git
            GIT_TAG 9899786b235b2d987c8b131cdb01909727b98d4f
    )
    FetchContent_Declare(
            oatpp-swagger
            GIT_REPOSITORY https://github.com/oatpp/oatpp-swagger.git
            GIT_TAG effa2635fc772260f17117bd15e256a44fdbbc18
    )
    FetchContent_Declare(
            oatpp-websocket
            GIT_REPOSITORY https://github.com/oatpp/oatpp-websocket.git
            GIT_TAG 36c3ceaf4706e54390d020390081203fa2cc693c
    )
    set(OATPP_BUILD_TESTS OFF)
    FetchContent_MakeAvailable(oatpp)

    set(OATPP_MODULES_LOCATION CUSTOM CACHE STRING "" FORCE)
    set(OATPP_DIR_SRC ${oatpp_SOURCE_DIR} CACHE STRING "" FORCE)
    set(OATPP_DIR_LIB ${oatpp_BINARY_DIR} CACHE STRING "" FORCE)
    FetchContent_MakeAvailable(oatpp-swagger)

    FetchContent_MakeAvailable(oatpp-websocket)
elseif(SDB_DEPENDENCIES_OATPP STREQUAL SDB_DEPENDENCIES_OATPP_INSTALLED)
    message("Finding squirrel in location=INSTALLED")
else()
    message("FATAL_ERROR Unknown location to find squirrel '${SDB_DEPENDENCIES_OATPP}'")
endif()

get_target_property(OATPP_SWAGGER_SOURCE_DIR oatpp-swagger SOURCE_DIR)
add_definitions(
        ## define path to swagger-ui static resources folder
        -DOATPP_SWAGGER_RES_PATH="${OATPP_SWAGGER_SOURCE_DIR}/../res"
        )

target_link_libraries(${PROJECT_NAME}
        sdb::interfaces
        oatpp
        oatpp-swagger
        oatpp-websocket)