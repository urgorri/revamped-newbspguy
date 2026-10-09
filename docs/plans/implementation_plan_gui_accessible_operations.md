# Implementation Plan: GUI-Accessible Core Operations

This plan tracks the end-to-end implementation for making all dormant, CLI-only, and core engine features fully accessible and discoverable across the GUI.

---

## Task Breakdown

- [x] 1. Core State & Header Setup (`Gui.h`, `Renderer.h`)
  - [x] 1.1 Declare `showShiftMapDialog`, `shiftMapDelta`, `showModentDialog`, and `drawShiftMapDialog()`, `drawModentDialog()` in `src/editor/Gui.h`.
  - [x] 1.2 Declare `request_viewport_screenshot` and `save_viewport_screenshot()` in `src/editor/Renderer.h` and implement capture logic in `src/editor/Renderer.cpp`.
  - [x] 1.3 Add merger icon texture handling and `drawShiftMapDialog()`, `drawModentDialog()` calls to `Gui::draw()` in `src/editor/Gui.cpp`.

- [x] 2. Map Merger Dialog Overhaul (`GuiDialogs.cpp`)
  - [x] 2.1 Add `"Add Open Maps"` button to populate `inPaths` with paths from active `mapRenderers`.
  - [x] 2.2 Add per-row remove button and `"Clear All"` button for managing the merge map list.
  - [x] 2.3 Add `"Stack Vertically"` button to calculate vertical stacking offsets automatically.
  - [x] 2.4 Ensure clean window close and cancel buttons without abrupt state loss.

- [x] 3. Whole-Map Shift & Batch Entity Query Dialogs (`GuiDialogs.cpp`)
  - [x] 3.1 Implement `Gui::drawShiftMapDialog()` with delta inputs, stacking presets, and undo integration.
  - [x] 3.2 Implement `Gui::drawModentDialog()` with `EntityQuery` parser evaluation, actions (Delete, Set, Remove Key), matching preview count, and undo support.

- [x] 4. Menu Bar & Toolbar Integration (`GuiMenuBar.cpp`, `Gui.cpp`)
  - [x] 4.1 Rebuild `Gui::drawMenu_Windows()` to expose all 12 editor panels/tools with shortcuts and checkmark indicators.
  - [x] 4.2 Add `File -> Merge Maps...`, `Map -> Merge Maps...`, and `Tools -> Merge Maps...`.
  - [x] 4.3 Add `File -> Import -> Add Map to Scene...` (`SHOW_IMPORT_ADD_NEW`).
  - [x] 4.4 Add `Map -> MAP TRANSFORMATION -> Shift / Move Map (X, Y, Z)...`.
  - [x] 4.5 Add `Tools -> Batch Entity Query (modent)...`.
  - [x] 4.6 Add `View -> Take Screenshot (F12)` and `File -> Export -> Viewport Screenshot (.tga)`.
  - [x] 4.7 Add Map Merger button to `drawPanelsToolbar()` in `src/editor/Gui.cpp`.

- [x] 5. Action Registry & Command Palette Wiring (`ActionRegistryInit.cpp`)
  - [x] 5.1 Register `map.merge_maps` and `window.merge_maps`.
  - [x] 5.2 Register missing window actions: `window.keyvalues`, `window.transform`, `window.lightmap_editor`, `window.overview`, `window.debug`.
  - [x] 5.3 Register `file.import_add_map`, `map.transform_move`, `tools.modent_query`, and `view.screenshot`.

- [x] 6. Verification, Testing & Polish
  - [x] 6.1 Clean compilation with CMake / MSVC.
  - [x] 6.2 Smoke tests verification on CLI and GUI paths.
  - [x] 6.3 Verify shortcuts, tooltips, and Command Palette fuzzy search.
