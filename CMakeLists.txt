cmake_minimum_required(VERSION 3.16)
project(functiongemma_pong LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
# IMPORTED_TARGET carries include dirs, library dirs (-L) and flags, which is
# required on Homebrew so the linker can locate SDL2main.
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)

find_package(CURL REQUIRED)
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  )
  FetchContent_MakeAvailable(json)
endif()

add_executable(pong
  src/main.cpp
  src/game.cpp
  src/ai_agent.cpp
  src/model_client.cpp
  src/hud.cpp
)

target_include_directories(pong PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(pong PRIVATE
  PkgConfig::SDL2
  PkgConfig::SDL2_TTF
  CURL::libcurl
  nlohmann_json::nlohmann_json
)

if(APPLE)
  target_link_options(pong PRIVATE -framework Cocoa)
endif()

install(TARGETS pong RUNTIME DESTINATION bin)

# --- Optional: synthetic dataset generator for fine-tuning ---------------------
# Reuses the game's constants and SerializeStateForModel() so training prompts
# match runtime prompts exactly. No SDL / CURL / JSON deps.
#   cmake --build build --target gen_dataset
#   ./build/gen_dataset 4000 finetune/data/train.jsonl
add_executable(gen_dataset
  finetune/gen_dataset.cpp
  src/game.cpp
)
target_include_directories(gen_dataset PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Not built by default (ALL); build explicitly with --target gen_dataset.
set_target_properties(gen_dataset PROPERTIES EXCLUDE_FROM_ALL TRUE)
