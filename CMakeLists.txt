cmake_minimum_required(VERSION 3.24)

project(Bomberman
  VERSION 0.1.0
  LANGUAGES C CXX
)

option(BOMBERMAN_BUILD_CLIENT "Build the Bomberman client target" ON)
option(BOMBERMAN_BUILD_SERVER "Build the Bomberman dedicated server target" ON)
option(BOMBERMAN_BUNDLE_DLLS  "Bundle MinGW/SDL2 DLLs into Windows client packages" ON)
set(BOMBERMAN_MINGW_SYSROOT "/usr/x86_64-w64-mingw32" CACHE PATH "MinGW sysroot path used for DLL bundling")

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

include(FetchContent)

if(BOMBERMAN_BUILD_CLIENT)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2       REQUIRED IMPORTED_TARGET sdl2)
  pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
  pkg_check_modules(SDL2_TTF   REQUIRED IMPORTED_TARGET SDL2_ttf)
  pkg_check_modules(SDL2_MIXER REQUIRED IMPORTED_TARGET SDL2_mixer)
endif()

# ENet – bundled static library; always sourced from third_party/enet
FetchContent_Declare(enet SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet")
set(ENET_SHARED OFF CACHE BOOL "" FORCE)
set(ENET_STATIC ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(enet)
set_target_properties(enet PROPERTIES FOLDER "ThirdParty")

# spdlog – header-only interface from third_party/spdlog
add_library(spdlog_interface INTERFACE)
target_include_directories(spdlog_interface SYSTEM INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/include)

# nlohmann/json – single-header from third_party/json
set(NLOHMANN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/single_include")
if(NOT EXISTS "${NLOHMANN_INCLUDE_DIR}/nlohmann/json.hpp")
  message(FATAL_ERROR "nlohmann/json not found at ${NLOHMANN_INCLUDE_DIR}. Ensure third_party/json is present in the repo.")
endif()

# ── Sources ───────────────────────────────────────────────────────────────────

if(BOMBERMAN_BUILD_CLIENT)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/MultiplayerLevelScene/MultiplayerLevelScene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/MultiplayerLevelScene/MultiplayerLevelScene.Authority.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/MultiplayerLevelScene/MultiplayerLevelScene.Presentation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/MultiplayerLevelScene/MultiplayerLevelScene.RemotePresentation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/MultiplayerLevelScene/MultiplayerLevelScene.Telemetry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/MultiplayerLevelScene/MultiplayerLevelScene.WorldPresentation.cpp
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
endif()

if(BOMBERMAN_BUILD_SERVER)
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
endif()

# ── Targets ───────────────────────────────────────────────────────────────────

if(BOMBERMAN_BUILD_CLIENT)
  add_executable(${PROJECT_NAME} ${SOURCES})
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "Bomberman")
endif()

if(BOMBERMAN_BUILD_SERVER)
  add_executable(Bomberman_Server ${SERVER_SOURCES})
endif()

# ── Include directories ───────────────────────────────────────────────────────

set(_targets)
if(BOMBERMAN_BUILD_CLIENT)
  list(APPEND _targets ${PROJECT_NAME})
endif()
if(BOMBERMAN_BUILD_SERVER)
  list(APPEND _targets Bomberman_Server)
endif()

set(_enet_include "${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/include")

foreach(_t IN LISTS _targets)
  target_include_directories(${_t} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_include_directories(${_t} SYSTEM PRIVATE
    ${NLOHMANN_INCLUDE_DIR}
    ${_enet_include}
  )
endforeach()

# ── Link libraries ────────────────────────────────────────────────────────────

if(BOMBERMAN_BUILD_CLIENT)
  target_link_libraries(${PROJECT_NAME} PRIVATE
    PkgConfig::SDL2
    PkgConfig::SDL2_IMAGE
    PkgConfig::SDL2_TTF
    PkgConfig::SDL2_MIXER
    enet
    spdlog_interface
    $<$<PLATFORM_ID:Windows>:ws2_32 winmm>
  )
endif()

if(BOMBERMAN_BUILD_SERVER)
  target_link_libraries(Bomberman_Server PRIVATE
    enet
    spdlog_interface
    $<$<PLATFORM_ID:Windows>:ws2_32 winmm>
  )
endif()

# ── Compile options ───────────────────────────────────────────────────────────

foreach(_t IN LISTS _targets)
  if(MSVC)
    target_compile_options(${_t} PRIVATE /W4)
  else()
    target_compile_options(${_t} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()

# ── Compile definitions ───────────────────────────────────────────────────────

# Shared log-level definitions for both targets
set(_log_defs
  $<IF:$<CONFIG:Debug>,SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE,SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
  $<IF:$<CONFIG:Debug>,BOMBERMAN_DEFAULT_LOG_LEVEL=SPDLOG_LEVEL_DEBUG,BOMBERMAN_DEFAULT_LOG_LEVEL=SPDLOG_LEVEL_INFO>
)

# BOMBERMAN_SOURCE_DIR is only defined in Debug so the binary can locate the
# in-tree config during development.  Release packages rely on the CWD-relative
# fallback path in Util/Log.cpp.
if(BOMBERMAN_BUILD_CLIENT)
  target_compile_definitions(${PROJECT_NAME} PRIVATE
    ${_log_defs}
    $<IF:$<CONFIG:Debug>,BOMBERMAN_ENABLE_NET_DIAG=1,BOMBERMAN_ENABLE_NET_DIAG=0>
    $<IF:$<CONFIG:Debug>,BOMBERMAN_ENABLE_CLIENT_NETCODE_DEBUG_OPTIONS=1,BOMBERMAN_ENABLE_CLIENT_NETCODE_DEBUG_OPTIONS=0>
    $<$<CONFIG:Debug>:BOMBERMAN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}">
  )
endif()

if(BOMBERMAN_BUILD_SERVER)
  target_compile_definitions(Bomberman_Server PRIVATE
    ${_log_defs}
    BOMBERMAN_ENABLE_NET_DIAG=1
    $<$<CONFIG:Debug>:BOMBERMAN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}">
  )
endif()

# ── Install / Resources ───────────────────────────────────────────────────────

if(BOMBERMAN_BUILD_CLIENT)
  install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION .
    COMPONENT Client)

  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Configs
    DESTINATION .
    COMPONENT Client)

  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets
    DESTINATION .
    COMPONENT Client)
endif()

if(BOMBERMAN_BUILD_SERVER)
  install(TARGETS Bomberman_Server
    RUNTIME DESTINATION .
    COMPONENT Server)
endif()

# Windows: bundle runtime DLLs from the MinGW sysroot so the ZIP is self-contained.
# The sysroot path can be overridden with BOMBERMAN_MINGW_SYSROOT.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND BOMBERMAN_BUILD_CLIENT AND BOMBERMAN_BUNDLE_DLLS)
  set(_win_bin "${BOMBERMAN_MINGW_SYSROOT}/bin")
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

set(CPACK_COMPONENTS_ALL)
if(BOMBERMAN_BUILD_CLIENT)
  list(APPEND CPACK_COMPONENTS_ALL Client)
endif()
if(BOMBERMAN_BUILD_SERVER AND NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
  list(APPEND CPACK_COMPONENTS_ALL Server)
endif()
set(CPACK_COMPONENT_CLIENT_DISPLAY_NAME "Client")
set(CPACK_COMPONENT_SERVER_DISPLAY_NAME "Server")
set(CPACK_COMPONENT_CLIENT_DESCRIPTION "Bomberman client and assets")
set(CPACK_COMPONENT_SERVER_DESCRIPTION "Dedicated server")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

# TGZ for Linux; ZIP for Windows (cross-compiled packages run on Windows)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(CPACK_GENERATOR "ZIP")
  set(CPACK_SYSTEM_NAME "win64")
else()
  set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
