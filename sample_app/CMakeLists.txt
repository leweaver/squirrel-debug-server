# Note that we can't use CMake 3.19.0 due to this issue: https://gitlab.kitware.com/cmake/cmake/-/issues/21529
# error: 'includes target "squirrel" more than once in the export set'
# This happens to be the version bundled with VS 2019 16.2.

cmake_minimum_required(VERSION 3.19.2)
project(sample_app)

set(CMAKE_CXX_STANDARD 17)

################################
# Executable
add_executable(sample_app
    "main.cpp"
    "debug-server/Endpoint.cpp"
    "debug-server/Endpoint.h"
    "debug-server/SwaggerComponent.h"
    "debug-server/controller/TestAppController.h"
    "debug-server/websocket/WSListener.cpp"
    "debug-server/websocket/WSListener.h"
    "debug-server/Logger.h"
    "debug-server/Logger.cpp"
    "debug-server/MessageInterface.h"
    "debugger/QuirrelDebugger.h" "debugger/QuirrelDebugger.cpp")

################################
# Dependencies
include(FetchContent)

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

set(OATPP_MODULES_LOCATION CUSTOM)
set(OATPP_DIR_SRC ${oatpp_SOURCE_DIR})
set(OATPP_DIR_LIB ${oatpp_BINARY_DIR})
FetchContent_MakeAvailable(oatpp-swagger)
FetchContent_MakeAvailable(oatpp-websocket)

get_target_property(OATPP_SWAGGER_SOURCE_DIR oatpp-swagger SOURCE_DIR)
message("OATPP_SWAGGER_SOURCE_DIR=${OATPP_SWAGGER_SOURCE_DIR}")
add_definitions(
        ## define path to swagger-ui static resources folder
        -DOATPP_SWAGGER_RES_PATH="${OATPP_SWAGGER_SOURCE_DIR}/../res"
        )

# quirrel
FetchContent_Declare(
        quirrel
        GIT_REPOSITORY https://github.com/leweaver/quirrel.git
)
FetchContent_MakeAvailable(quirrel)

target_link_libraries(${PROJECT_NAME}
        squirrel::squirrel_static
        squirrel::sqstdlib_static
        oatpp
        oatpp-swagger
        oatpp-websocket)