cmake_minimum_required(VERSION 3.24)

project(Bomberman
  VERSION 0.1.0
  LANGUAGES C CXX
)

# ---- Build settings ----

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Prefer a sensible default build type for single-config generators
if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Export compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Dependencies ----

find_package(PkgConfig REQUIRED)

# pkg-config is the most reliable way to get SDL2 + friends.
pkg_check_modules(SDL2       REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
pkg_check_modules(SDL2_TTF   REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(SDL2_MIXER REQUIRED IMPORTED_TARGET SDL2_mixer)
pkg_check_modules(ENET       REQUIRED IMPORTED_TARGET libenet)

# ---- Sources ----

set(SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Game.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Core/Animation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/Scene.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/MenuScene.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scenes/LevelScene.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/Net/NetClient.cpp
)

set(SERVER_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/server_main.cpp
  # ${CMAKE_CURRENT_SOURCE_DIR}/NetServer.cpp
)

# ---- macOS bundle parameters (from original) ----

set(APP_ICON "")
if (CMAKE_MACOSX_BUNDLE)
  set(MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME})
  set(MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.ibardinov.${PROJECT_NAME}")
  set(MACOSX_BUNDLE_INFO_STRING "Super Arcade Bang Bang")
  set(MACOSX_BUNDLE_ICON_FILE "icon.icns")
  set(MACOSX_BUNDLE_LONG_VERSION_STRING ${PROJECT_VERSION})
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
  set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
  set(MACOSX_BUNDLE_COPYRIGHT "Ilia Bardinov (c) 2019")
  set(APP_ICON ${CMAKE_CURRENT_SOURCE_DIR}/../icons/icon.icns)
  set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

# ---- Target ----

add_executable(${PROJECT_NAME}
  ${APP_ICON}
  ${SOURCES}
)

set_target_properties(${PROJECT_NAME}
  PROPERTIES
    OUTPUT_NAME "bomberman_client"
)

add_executable(bomberman_server
  ${SERVER_SOURCES}
)

# Project headers
target_include_directories(${PROJECT_NAME}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(bomberman_server
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# SDL2 libs via pkg-config imported targets
target_link_libraries(${PROJECT_NAME}
  PRIVATE
    PkgConfig::SDL2
    PkgConfig::SDL2_IMAGE
    PkgConfig::SDL2_TTF
    PkgConfig::SDL2_MIXER
    PkgConfig::ENET
)

target_link_libraries(bomberman_server
  PRIVATE
    PkgConfig::ENET
)

# Warnings
if (MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Install / Resources ----

install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION .
  BUNDLE  DESTINATION .
)

install (TARGETS bomberman_server
  RUNTIME DESTINATION .
)

if (CMAKE_MACOSX_BUNDLE)
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../assets
          DESTINATION ${PROJECT_NAME}.app/Contents/Resources)
else()
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../assets
          DESTINATION .)
endif()

# Fix dylib (macOS)
if (CMAKE_MACOSX_BUNDLE)
  install(CODE "include(BundleUtilities)
    fixup_bundle(\"${CMAKE_INSTALL_PREFIX}/${PROJECT_NAME}.app\" \"\" \"${CMAKE_BINARY_DIR}\")")
endif()
