cmake_minimum_required(VERSION 3.20)
project(idc-platform-backend VERSION 1.0.0 LANGUAGES CXX)

# ── C++ Standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── CMAKE_MODULE_PATH: our cmake/ dir found first (overrides Drogon's FindJsoncpp) ─
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ── Find required system packages (so targets are available in our scope) ────
find_package(OpenSSL REQUIRED)

# ── Fetch Drogon with PostgreSQL & Redis support ──────────────────────────────
include(FetchContent)

set(BUILD_POSTGRESQL ON  CACHE BOOL "" FORCE)
set(BUILD_REDIS      ON  CACHE BOOL "" FORCE)
set(BUILD_CTL        OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING    OFF CACHE BOOL "" FORCE)
set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "" FORCE)

FetchContent_Declare(drogon
    GIT_REPOSITORY  https://github.com/drogonframework/drogon.git
    GIT_TAG         v1.9.10
    GIT_SHALLOW     TRUE
)

FetchContent_MakeAvailable(drogon)

# ── Sources ───────────────────────────────────────────────────────────────────
file(GLOB_RECURSE SOURCES
    src/*.cc
    src/*.cpp
)

add_executable(idc-platform ${SOURCES})

target_link_libraries(idc-platform
    PRIVATE
        drogon
        pthread
        crypt
        OpenSSL::SSL
        OpenSSL::Crypto
)

target_include_directories(idc-platform
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ── Copy config to build directory ────────────────────────────────────────────
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.json
    ${CMAKE_CURRENT_BINARY_DIR}/config.json
    COPYONLY
)
