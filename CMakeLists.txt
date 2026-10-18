cmake_minimum_required(VERSION 3.16)
project(cas VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
endif()

# Core sources
set(CORE_SOURCES
    src/core/crypto_util.cpp
    src/core/fastcdc.cpp
    src/core/pack_format.cpp
    src/core/json.cpp
    src/core/http_client.cpp
    src/core/config.cpp
    src/core/s3_client.cpp
    src/core/repo_manager.cpp
)

include_directories(src/core)

# Core static library
add_library(cas_core STATIC ${CORE_SOURCES})
target_link_libraries(cas_core winhttp bcrypt z comctl32 ole32 shell32 uuid)

# Headless CLI
add_executable(cas_cli src/cli/main_cli.cpp)
target_link_libraries(cas_cli cas_core)

# Windows Native GUI
if(WIN32)
    add_executable(cas_app WIN32
        src/ui/ui_window.cpp
        src/ui/main_ui.cpp
    )
    target_link_libraries(cas_app cas_core)
endif()
