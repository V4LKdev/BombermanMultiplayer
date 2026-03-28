cmake_minimum_required(VERSION 3.24)

project(Bomberman
  VERSION 0.1.0
  LANGUAGES C CXX
)

# ── Build settings ────────────────────────────────────────────────────────────

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default for single-config generators (Ninja, Make)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ──────────────────────────────────────────────────────────────

find_package(PkgConfig REQUIRED)
include(FetchContent)

pkg_check_modules(SDL2       REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
pkg_check_modules(SDL2_TTF   REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(SDL2_MIXER REQUIRED IMPORTED_TARGET SDL2_mixer)

# ENet – bundled static library; always sourced from third_party/enet
FetchContent_Declare(enet SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet")
set(ENET_SHARED OFF CACHE BOOL "" FORCE)
set(ENET_STATIC ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(enet)
set_target_properties(enet PROPERTIES FOLDER "ThirdParty")

# spdlog – header-only interface from third_party/spdlog
add_library(spdlog_interface INTERFACE)
target_include_directories(spdlog_interface INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/include)

# nlohmann/json – single-header from third_party/json
set(NLOHMANN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/single_include")
if(NOT EXISTS "${NLOHMANN_INCLUDE_DIR}/nlohmann/json.hpp")
  message(FATAL_ERROR "nlohmann/json not found at ${NLOHMANN_INCLUDE_DIR}. Ensure third_party/json is present in the repo.")
endif()

# ── Sources ───────────────────────────────────────────────────────────────────

set(SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Game.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Core/Animation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/Scene.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/MenuScene.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/ConnectScene.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/LobbyScene.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/LevelScene.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/LevelSceneFactory.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/SingleplayerLevelScene.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/MultiplayerLevelScene.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/MultiplayerLevelScene.Authority.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/MultiplayerLevelScene.Presentation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/GameOverScene.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/StageScene.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Managers/SceneManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Managers/AssetManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Entities/Object.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Entities/Text.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Entities/Sprite.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Entities/Creature.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Entities/Enemy.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Entities/Player.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Entities/Music.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Entities/Sound.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Util/Collision.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Util/Pathfinding.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Util/Log.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Net/NetDiagnostics.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Net/ClientDiagnostics.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Net/ClientPrediction.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Net/NetClient.cpp
)

set(SERVER_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/server_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Server/ServerSession.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Server/ServerFlow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Server/ServerLobbyFlow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Server/ServerMatchFlow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Server/ServerSimulation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Server/ServerBombs.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Server/ServerPowerups.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Server/ServerControlHandlers.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Server/ServerHandlers.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Server/ServerInputHandlers.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Server/ServerEvents.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Server/ServerSnapshot.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Net/NetDiagnostics.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Util/Log.cpp
)

# ── Targets ───────────────────────────────────────────────────────────────────

add_executable(${PROJECT_NAME} ${SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "Bomberman")

add_executable(Bomberman_Server ${SERVER_SOURCES})

# ── Include directories ───────────────────────────────────────────────────────

set(_enet_include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/include")

foreach(_t ${PROJECT_NAME} Bomberman_Server)
  target_include_directories(${_t} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${NLOHMANN_INCLUDE_DIR}
    ${_enet_include}
  )
endforeach()

# ── Link libraries ────────────────────────────────────────────────────────────

target_link_libraries(${PROJECT_NAME} PRIVATE
  PkgConfig::SDL2
  PkgConfig::SDL2_IMAGE
  PkgConfig::SDL2_TTF
  PkgConfig::SDL2_MIXER
  enet
  spdlog_interface
  $<$<PLATFORM_ID:Windows>:ws2_32 winmm>
)

target_link_libraries(Bomberman_Server PRIVATE
  enet
  spdlog_interface
  $<$<PLATFORM_ID:Windows>:ws2_32 winmm>
)

# ── Compile options ───────────────────────────────────────────────────────────

if(MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── Compile definitions ───────────────────────────────────────────────────────

# Shared log-level definitions for both targets
set(_log_defs
  $<IF:$<CONFIG:Debug>,SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE,SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
  $<IF:$<CONFIG:Debug>,BOMBERMAN_DEFAULT_LOG_LEVEL=SPDLOG_LEVEL_DEBUG,BOMBERMAN_DEFAULT_LOG_LEVEL=SPDLOG_LEVEL_INFO>
)

# BOMBERMAN_SOURCE_DIR is only defined in Debug so the binary can locate the
# in-tree config during development.  Release packages rely on the CWD-relative
# fallback path in Util/Log.cpp.
target_compile_definitions(${PROJECT_NAME} PRIVATE
  ${_log_defs}
  $<IF:$<CONFIG:Debug>,BOMBERMAN_ENABLE_NET_DIAG=1,BOMBERMAN_ENABLE_NET_DIAG=0>
  $<IF:$<CONFIG:Debug>,BOMBERMAN_ENABLE_CLIENT_NETCODE_DEBUG_OPTIONS=1,BOMBERMAN_ENABLE_CLIENT_NETCODE_DEBUG_OPTIONS=0>
  $<$<CONFIG:Debug>:BOMBERMAN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}">
)

target_compile_definitions(Bomberman_Server PRIVATE
  ${_log_defs}
  BOMBERMAN_ENABLE_NET_DIAG=1
  $<$<CONFIG:Debug>:BOMBERMAN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}">
)

# ── Install / Resources ───────────────────────────────────────────────────────

install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION .
  COMPONENT Client)

install(TARGETS Bomberman_Server
  RUNTIME DESTINATION .
  COMPONENT Server)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Configs
  DESTINATION .
  COMPONENT Client)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets
  DESTINATION .
  COMPONENT Client)

# Windows: bundle runtime DLLs from the MinGW sysroot so the ZIP is self-contained.
# CMAKE_FIND_ROOT_PATH is set to /usr/x86_64-w64-mingw32 by the toolchain file.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_win_bin "${CMAKE_FIND_ROOT_PATH}/bin")
  install(FILES
    # SDL2
    "${_win_bin}/SDL2.dll"
    "${_win_bin}/SDL2_image.dll"
    "${_win_bin}/SDL2_ttf.dll"
    "${_win_bin}/SDL2_mixer.dll"
    # MinGW C++ runtime
    "${_win_bin}/libgcc_s_seh-1.dll"
    "${_win_bin}/libstdc++-6.dll"
    "${_win_bin}/libwinpthread-1.dll"
    "${_win_bin}/libssp-0.dll"
    # Image codec deps (pulled in by SDL2_image)
    "${_win_bin}/libpng16-16.dll"
    "${_win_bin}/zlib1.dll"
    "${_win_bin}/libjpeg-8.dll"
    "${_win_bin}/libtiff-6.dll"
    "${_win_bin}/libwebp.dll"
    "${_win_bin}/libwebpdecoder.dll"
    "${_win_bin}/libwebpdemux.dll"
    "${_win_bin}/libwebpmux.dll"
    "${_win_bin}/libsharpyuv.dll"
    DESTINATION .
    COMPONENT Client)
endif()

# ── Packaging ─────────────────────────────────────────────────────────────────

set(CPACK_PACKAGE_NAME "Bomberman")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
# Windows cross-compile only targets the client; no server package for that platform
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(CPACK_COMPONENTS_ALL Client)
else()
  set(CPACK_COMPONENTS_ALL Client Server)
endif()
set(CPACK_COMPONENT_CLIENT_DISPLAY_NAME "Client")
set(CPACK_COMPONENT_SERVER_DISPLAY_NAME "Server")
set(CPACK_COMPONENT_CLIENT_DESCRIPTION "Bomberman client and assets")
set(CPACK_COMPONENT_SERVER_DESCRIPTION "Dedicated server")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

# TGZ for Linux; ZIP for Windows (cross-compiled packages run on Windows)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
