cmake_minimum_required(VERSION 3.16)

project(ttdctl
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find nlohmann_json - use the one from the parent project's 3rdparty
set(NLOHMANN_JSON_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/src/3rdparty")

add_executable(ttdctl
    src/main.cpp
    src/cli_common.cpp
    src/cli_common.h
    src/commands_query.cpp
    src/commands_action.cpp
    src/commands_infra.cpp
    src/commands_vehicle.cpp
    src/rpc_client.cpp
    src/rpc_client.h
)

target_include_directories(ttdctl PRIVATE
    ${NLOHMANN_JSON_INCLUDE_DIR}
)

# Install alongside the main game
install(TARGETS ttdctl
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
