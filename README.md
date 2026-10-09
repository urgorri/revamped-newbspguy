# bspguy REVAMPED

![bspguy REVAMPED](https://i.imgur.com/g8I8lbq.png)

**A hardened fork of the GoldSrc map editor (bspguy & newbspguy) <br>Focused on robustness, vertical map merging, and modernized build flows.**

## Projects / genealogy

* **[bspguy - goldsrc map editor project](https://github.com/wootguy/bspguy)** - the original project and starting point.
* **[newbspguy - improved fork by unrealkaraulov](https://github.com/UnrealKaraulov/newbspguy)** - an improved fork with several enhancements.
* **[revamped-newbspguy - personal fork repo](https://github.com/urgorri/revamped-newbspguy)** - this repository (named `revamped-newbspguy`) published under the display name **bspguy REVAMPED**.

> Note: code, build files and some assets may still reference `bspguy` or `newbspguy`. This README clarifies lineage and the purpose of this fork.

---

## What this repo delivers

* 3D editor and CLI tools to view, edit and merge `.bsp` GoldSrc maps (supports multiple BSP variants: BSP2, 2PSB, 29, bsp30ex, and broken clipnodes).
* Vertical merge support (stack maps vertically) and arbitrary model/map transforms.
* Modernized build: C++20, MSVC-friendly configuration and resource post-build copying.
* Robustness hardening across renderer, importer, merger, and GUI.
* Documentation, usage examples and a promotional image for the project.

---

## Warning

This software is still experimental. The editor contains bugs and some operations are destructive. **Always back up your maps before using merge / simplify / transform / CRC-spoofing features.**

---

# Quick usage

* Launch the 3D editor by dragging a `.bsp` onto the executable or by opening the app and selecting `File -> Open`.
* Run `bspguy` without arguments to open the main window.
* Use the CLI for automated tasks and batch operations (see CLI reference below).

---

# Editor: Feature highlights

* FGD-aware keyvalue editor (J.A.C.K. FGD supported).
* Create and duplicate entities and BSP models.
* Move, scale objects and manipulate vertices; face splitting for precise triggers.
* BSP model origin movement/alignment and hull manipulation (delete/redirect/create).
* Face editor with manual vertex editing and improved texture support.
* Full-featured **LightMap Editor** for single or multiple faces.
* Export/Import: OBJ, WAD, ENT, BSP (multiple modes), HLRAD export, Quake LIT import/export, etc.
* Render BSP/MDL and preliminary SPR rendering (WIP).
* Undo/Redo support for many actions (not exhaustive - save often).
* CRC-spoofing to allow replacing original maps for testing on servers.
* Map protection features (anti-decompile - WIP).

---

# Changes introduced in this fork (revamped-newbspguy)

These are the main technical changes and fixes added on top of the prior codebase.

### Build & project layout

* Move `.github/workflows/build.yml` → `.github.example/workflows/build.yml` (store as example/template rather than active CI).
* Set the project to C++20 (no extensions). Reorganize CMake for MSVC:

  * MSVC-specific defines, linker paths for GLEW (x64 / Win32).
  * Compiler options and `POST_BUILD` commands to copy resources (fonts, languages, palettes, pictures, scripts, `bspguy.ini`).
  * Remove the previous fatal error that forced using the VS solution and eliminate duplicated `CMAKE_CXX_STANDARD` entries.

### New features and robustness

* Vertical merge: new `verticalMerge` option with `verticalGap` support in the BspMerger; ability to force a separation plane for stacking maps vertically.
* Rework of model movement logic: `move()` now correctly handles world moves versus brush-origin submodels to avoid double-moving geometry and to split shared structures when necessary.
* `transform(modelIdx, matrix, center, logged)` added to apply arbitrary matrix transforms to model geometry, planes, texinfos and bounds.
* Fixes in texture import (lodepng memory handling) to allocate/copy/free correctly.
* BspMerger applies `worldspawn` origin transforms prior to separation; improved offsets/overlap handling; fixed visibility decompression length/init.
* CLI now uses `std::filesystem` for path handling.

### Renderer & picking

* Defensive and correctness fixes in `BspRenderer`: null checks, proper future waits in destructors, mutex/locking fixes.
* Computing face-local bounds for correct picking and using them where appropriate.
* Face UV update improvements (support override texinfo and optional reupload).
* Guarding navmesh / clip buffers and fixing render loop indexing/bounds.

### GUI & UX

* Avoid leaking `lodepng` buffers when loading icons/textures.
* `allowExternalTextures` flag to enable optional external texture loading without breaking rendering.
* Copy/Paste *Style* commands, improved lightmap paste (scaling, accumulation, style copy) and persistent copied lightmap handling.
* Improved input flow: hotkeys are disabled while an input/window is active to avoid state conflicts.
* **Enhanced Entity Report**: Advanced filtering with multi-line support, customizable operators (`=`, `!=`), and boolean logic (`AND`, `OR`). Consistent `*` wildcard support across CLI and GUI. Matches empty keyvalues correctly.

### Localization and headers

* Added UI strings for new features (copy/paste style, flip/rotate/fill, vertical merge/gap, etc.) across English, Russian and Chinese translation files.
* Header updates exposing new functions and signatures; include fixes and small API adjustments.

---

# CLI reference

```
Usage: bspguy <command> <mapname> [options]

<Commands>
  info      : Show BSP data summary
  merge     : Merges two or more maps together (supports --verticalMerge / --verticalGap or -overlapgap)
  noclip    : Delete some clipnodes/nodes from the BSP
  simplify  : Simplify BSP models
  delete    : Delete BSP models
  transform : Apply 3D transformations to the BSP
  unembed   : Deletes embedded texture data
  exportobj : Export bsp geometry to obj [WIP]
  cullfaces : Remove leaf faces from map
  exportlit : Export .lit (Quake) lightdata file
  importlit : Import .lit (Quake) lightdata file to map
  exportrad : Export RAD.exe .ext & .wa_ files for hlrad.exe
  exportwad : Export all map textures to .wad file
  importwad : Import all .wad textures to map
  modent    : Batch query, filter, update, or export map entities via CLI
  screenshot: Capture high-resolution viewport render of a map

Run 'bspguy <command> help' for command-specific options.
```

> Note: The `merge` command supports both `--verticalMerge` / `--verticalGap <dist>` and legacy `-overlapgap <vector_list>` syntax. Run `bspguy merge help` for detailed options.

---

# First-time setup (GUI)

1. `File` → `Settings` → `General`.
2. Set the **Game Directory** and click `Apply Changes`.
3. On the `Assets` tab add full or relative paths to your mod directories (e.g. `cstrike/`, `valve/`) to resolve missing textures.
4. On the `FGDs` tab add paths to your mod `.fgd` files (e.g. `mod_name.fgd`) and click `Apply Changes`.

> Configuration files are saved to the executable folder by default.

---

# Building from source

## Windows (MSVC)

1. Install Visual Studio 2022 with "Desktop development with C++".
2. Download and extract the source.
3. Generate a build with CMake (MSVC-aware settings are included):

```powershell
mkdir build; cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

`POST_BUILD` steps copy required resource files (fonts, languages, palettes, pictures, scripts, `bspguy.ini`) to the executable output directory.

## Linux

1. Install dependencies. Example (Debian/Ubuntu):

```
sudo apt install build-essential git cmake libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev xorg-dev libglfw3-dev libglew-dev
```

2. Clone and build:

```
git clone https://github.com/wootguy/bspguy.git
mkdir build; cd build
cmake .. -DCMAKE_BUILD_TYPE=RELEASE
make -j$(nproc)
```

---

# License

This fork respects the original project’s license unless an explicit LICENSE file in this repo states otherwise. See the repo root for `LICENSE`.
