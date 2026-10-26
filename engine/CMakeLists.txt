cmake_minimum_required(VERSION 3.28)

project(engine VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(GTest CONFIG REQUIRED)
find_package(benchmark CONFIG REQUIRED)

add_library(engine_core STATIC src/core/version.cpp)
add_library(refresher STATIC src/refresher/ScopedFile.cpp)

target_compile_definitions(engine_core PUBLIC ENGINE_VERSION="${PROJECT_VERSION}")

target_include_directories(engine_core PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(refresher PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

## Hiding warning flags that aren't relevant right here

add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)

target_link_libraries(engine_core PRIVATE project_warnings)

add_executable(engine src/main.cpp)

target_link_libraries(engine PRIVATE engine_core)

enable_testing()
add_subdirectory(tests)
add_subdirectory(bench)
