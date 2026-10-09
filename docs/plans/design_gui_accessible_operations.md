# Design Specification: GUI-Accessible Core Operations Architecture

## 1. Architecture Overview

This design outlines the technical integration patterns required to bring all dormant and CLI-only capabilities into the GUI. The architectural touchpoints span:
1. **Menu Bar (`GuiMenuBar.cpp`)**: Adding entry points across `File`, `Map`, `Tools`, `View`, and rebuilding `drawMenu_Windows()`.
2. **Dialogs & Windows (`GuiDialogs.cpp`, `Gui.h`)**:
   - Enhancing `drawMergeWindow()` with dynamic row operations, loaded-map detection, and vertical offset automation.
   - Adding `drawShiftMapDialog()` for whole-map delta translations.
   - Adding `drawModentDialog()` leveraging `EntityQuery` for in-GUI batch entity manipulation.
3. **Panels Toolbar (`Gui.cpp`)**: Adding the Map Merger tool button with custom texture fallback.
4. **Interactive Screenshots (`Renderer.cpp`, `Renderer.h`)**: Adding non-terminating viewport FBO screenshot capture.
5. **Action Registry (`ActionRegistryInit.cpp`)**: Centralizing all actions for Command Palette fuzzy searching (`Ctrl+K`).

---

## 2. Component Design Details

### 2.1. Map Merger Dialog Overhaul (`GuiDialogs.cpp`)
Currently, `drawMergeWindow()` relies on an implicit `inPaths` array growth when the last input text length > 1, with no row removal or preset features.
We enhance `drawMergeWindow()` with:
- Row deletion button (`"-"` or `"X"`) next to each input row, allowing pruning redundant rows.
- `"Add Open Maps"` button: scans `mapRenderers`, extracting valid non-submodel map paths (`bspRend->map->bsp_path`) and appending them into `inPaths` with default offsets.
- `"Stack Vertically"` button: automatically recalculates `inOffsets[i] = vec3(0, 0, 512.0f * (float)i)`.
- `"Clear All"` button: resets `inPaths` and `inOffsets` to a clean single empty entry.
- Menu entries in:
  - `File -> Merge Maps...`
  - `Map -> Merge Maps...`
  - `Tools -> Merge Maps...`
  - `Windows -> Map Merger`

### 2.2. Restoring `Windows` Menu (`GuiMenuBar.cpp:drawMenu_Windows`)
The `Windows` menu will be organized into logical sections:
1. **Primary Editor Panels**:
   - Limits & Engine Statistics (`F2`) -> `showLimitsWidget`
   - Entity Report (`F3`) -> `showEntityReport`
   - Texture Browser (`F4`) -> `showTextureBrowser`
   - Face Editor (`F6`) -> `showFaceEditWidget`
   - Entity Keyvalues (`Alt+Enter`) -> `showKeyvalueWidget`
   - 3D Transform Tool (`Ctrl+M`) -> `showTransformWidget`
   - Lightmap Editor -> `showLightmapEditorWidget`
2. **Tools & Diagnostics**:
   - Map Merger -> `showMergeMapWidget`
   - Map Overview (2D Radar) -> `showOverviewWidget`
   - Go to Coordinates (GOTO) (`Ctrl+Shift+G`) -> `showGOTOWidget`
   - Log Console (`F5`) -> `showLogWidget`
   - Debug PVS / Engine Inspector -> `showDebugWidget`
   - Console (Win32 embedded console toggle)
3. **Loaded Map Tabs**:
   - Radio items listing each open map from `mapRenderers`, allowing fast workspace switching.

### 2.3. Scene Multi-Map Loading (`SHOW_IMPORT_ADD_NEW`)
In `drawMenu_File()` under `Import`:
```cpp
if (ImGui::MenuItem("Add Map to Scene / Renderer...", NULL, false, !app->isLoading))
{
    showImportMapWidget_Type = SHOW_IMPORT_ADD_NEW;
    showImportMapWidget = true;
}
```
This triggers `drawImportMapWidget()` configured to append the selected BSP into `app->mapRenderers` using `app->addMap(new Bsp(mapPath))`, enabling simultaneous multi-map inspection.

### 2.4. Whole-Map 3D Translation (`drawShiftMapDialog`)
We add a state variable in `Gui.h`:
```cpp
bool showShiftMapDialog = false;
vec3 shiftMapDelta = vec3(0.0f, 0.0f, 0.0f);
```
Inside `GuiDialogs.cpp`, `drawShiftMapDialog()` renders a focused modal:
- InputFloat3 for `Delta X, Y, Z`.
- Quick preset buttons: `+512 Z (Stack Above)`, `-512 Z (Stack Below)`, `Reset`.
- `Apply Shift`: executes `map->move(shiftMapDelta)`, calls `rend->reload()`, pushes `rend->pushUndoState("Shift Map", EDIT_MODEL_LUMPS | FL_ENTITIES)`, and logs confirmation.

### 2.5. Batch Entity Query (`drawModentDialog`)
We add a state variable in `Gui.h`:
```cpp
bool showModentDialog = false;
```
Inside `GuiDialogs.cpp`, `drawModentDialog()` provides:
- Query input field with syntax hint (e.g. `classname=monster_* AND targetname=`).
- Action selection combo:
  - `0: Delete matched entities`
  - `1: Set / Add keyvalues (e.g. key1=val1, key2=val2)`
  - `2: Remove key (e.g. targetname)`
- Value parameter input string.
- Action execution:
  - Evaluates each non-world entity against `EntityQuery`.
  - Performs the modification on matches.
  - Pushes undo state `FL_ENTITIES`.
  - Calls `rend->preRenderEnts()`, `g_app->updateEnts()`.

### 2.6. Interactive Viewport Screenshot
In `Renderer.h` / `Renderer.cpp`:
We add `void save_viewport_screenshot();` that triggers on the next frame render after FBO swap to read the current backbuffer pixels via `glReadPixels`, inverts row lines, and calls `stbi_write_tga` into `g_working_dir + "screenshots/"` without setting `is_closing = true`.

---

## 3. Localization & Strings
All new menus, tooltips, and dialog labels will provide clear English defaults and fallbacks, matching the established conventions in `GuiMenuBar.cpp` and `ActionRegistryInit.cpp`.
