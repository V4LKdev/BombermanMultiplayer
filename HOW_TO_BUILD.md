# How to Build

## Prerequisites

**All platforms**
- CMake 3.24+
- Ninja

**Linux (client + server)**
- GCC 12+ or Clang 15+ (C++20 required)
- SDL2 dev packages:
  ```
  # Arch
  sudo pacman -S sdl2 sdl2_image sdl2_ttf sdl2_mixer

  # Ubuntu / Debian
  sudo apt install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libsdl2-mixer-dev
  ```

**Windows client (cross-compile from Linux)**
- MinGW-w64 toolchain:
  ```
  # Arch
  sudo pacman -S mingw-w64-gcc
  ```
- SDL2 MinGW packages (headers + import libs in `/usr/x86_64-w64-mingw32/`):
  ```
  # Arch (AUR)
  paru -S mingw-w64-sdl2 mingw-w64-sdl2_image mingw-w64-sdl2_ttf mingw-w64-sdl2_mixer
  ```

All other dependencies (ENet, spdlog, nlohmann/json) are vendored in `third_party/` — no additional installs needed.

---

## Cloning

```bash
git clone <repo-url>
cd Bomberman
```

No submodule init required. All dependencies are committed under `third_party/`.

---

## CLI builds

Each build preset shares a configure directory. Running `cmake --preset <configure>` once is enough; both client and server can then be built from the same configured tree.

### Configure

```bash
cmake --preset linux-debug    # Debug (client + server)
cmake --preset linux-release  # Release (client + server)
cmake --preset windows-release  # Windows cross-compile
```

### Build

```bash
# Linux client
cmake --build --preset linux-client-debug
cmake --build --preset linux-client-release

# Linux server
cmake --build --preset linux-server-debug
cmake --build --preset linux-server-release

# Windows client
cmake --build --preset windows-client-release
```

Binaries are written to `build/<configure-preset>/`.

---

## Packaging

Run from the relevant build directory after building:

```bash
cd build/linux-release
cpack --config CPackConfig.cmake
```

This produces archives in the build directory:

| Build dir | File | Contents |
|-----------|------|----------|
| `build/linux-release` | `Bomberman-0.1.0-Linux-Client.tar.gz` | `Bomberman`, `assets/`, `Configs/` |
| `build/linux-release` | `Bomberman-0.1.0-Linux-Server.tar.gz` | `Bomberman_Server` |
| `build/windows-release` | `Bomberman-0.1.0-win64-Client.zip` | `Bomberman.exe`, `assets/`, `Configs/` |

### Windows DLL bundling

The Windows client package does not automatically include runtime DLLs. After packaging, copy the following from `/usr/x86_64-w64-mingw32/bin/` into the extracted client directory:

- `SDL2.dll`, `SDL2_image.dll`, `SDL2_ttf.dll`, `SDL2_mixer.dll`
- `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`
- Image codec deps (`libpng16-16.dll`, `zlib1.dll`, `libjpeg-8.dll`, `libtiff-6.dll`, `libwebp*.dll`, etc.)
