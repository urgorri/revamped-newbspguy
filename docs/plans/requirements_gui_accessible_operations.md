# Requirements Specification: GUI-Accessible Core Operations

## Executive Summary
Several core engine and CLI capabilities in `revamped-newbspguy`—most notably the Map Merge tool, batch entity querying (`modent`), scene-level multi-map importing, whole-map 3D translation (`bspguy transform -move`), interactive viewport screenshot capturing, and panel re-opening from the menu bar—are currently inaccessible or disconnected from the graphical user interface. This specification defines functional and non-functional requirements to make 100% of these engine features first-class, reachable, and discoverable from the GUI (menu bar, panels toolbar, dialogs, and the Command Palette).

---

## 1. Map Merger Accessibility & Workflow Integration
- **REQ-1.1**: The Map Merger window (`Gui::drawMergeWindow()`) MUST be directly accessible and toggleable from:
  - `File -> Merge Maps...`
  - `Map -> Merge Maps...`
  - `Tools -> Merge Maps...`
  - `Windows -> Map Merger`
- **REQ-1.2**: The Panels Toolbar floating on the left viewport MUST include a dedicated Map Merger button with tooltip and active/dim highlight states.
- **REQ-1.3**: The Map Merger window MUST provide:
  - A button to populate the merge list with currently open/loaded maps (`"Add Loaded Maps"`).
  - A button to remove individual map rows.
  - A button to clear all map entries.
  - A preset/helper to automatically calculate vertical stacking offsets (`vec3(0, 0, 512 * i)`) to facilitate vertical merging without manual coordinate entry.
- **REQ-1.4**: The Map Merger action MUST be registered in the `ActionRegistry` (`map.merge_maps` and `window.merge_maps`) for global Command Palette (`Ctrl+K`) accessibility.

---

## 2. Complete Windows Menu Restoration
- **REQ-2.1**: The `Windows` menu in the top menu bar (`drawMenu_Windows()`) MUST provide toggle menu items with current open/closed checkmark states for all editor windows and tool panels:
  - `Entity Keyvalues` (`Alt+Enter`, `showKeyvalueWidget`)
  - `3D Transform Tool` (`Ctrl+M`, `showTransformWidget`)
  - `Face Editor` (`F6`, `showFaceEditWidget`)
  - `Texture Browser` (`F4`, `showTextureBrowser`)
  - `Lightmap Editor` (`showLightmapEditorWidget`)
  - `Map Limits & Engine Statistics` (`F2`, `showLimitsWidget`)
  - `Entity Report` (`F3`, `showEntityReport`)
  - `Go to Coordinates (GOTO)` (`Ctrl+Shift+G`, `showGOTOWidget`)
  - `Log Console` (`F5`, `showLogWidget`)
  - `Map Overview (2D Radar)` (`showOverviewWidget`)
  - `Debug PVS / Engine Inspector` (`showDebugWidget`)
  - `Map Merger` (`showMergeMapWidget`)
- **REQ-2.2**: Each menu item in `Windows` MUST display its respective hotkey shortcut (e.g. `F2`, `F3`, `F4`, `F5`, `F6`, `Alt+Enter`, `Ctrl+M`, `Ctrl+Shift+G`).

---

## 3. Scene-Level Multi-Map Importing (`SHOW_IMPORT_ADD_NEW`)
- **REQ-3.1**: `File -> Import` MUST include an option `Add Map to Scene...` (or `Add Map to Renderer...`).
- **REQ-3.2**: Selecting this option MUST open the import dialog with `showImportMapWidget_Type = SHOW_IMPORT_ADD_NEW`, allowing users to load and view multiple `.bsp` maps side-by-side without clearing existing loaded maps.

---

## 4. Whole-Map 3D Translation / Move
- **REQ-4.1**: `Map -> MAP TRANSFORMATION` MUST include an option `Shift / Move Map (X, Y, Z)...`.
- **REQ-4.2**: Activating this tool MUST display an interactive modal dialog where the user specifies delta coordinate offsets ($X, Y, Z$), and an `Apply` button that executes `map->move(delta)`, reloads the renderer, and pushes an undo state (`EDIT_MODEL_LUMPS | FL_ENTITIES`).

---

## 5. Headless Batch Entity Query Engine (`modent`) GUI Integration
- **REQ-5.1**: `Tools` MUST include an option `Batch Entity Query (modent)...` opening an interactive dialog.
- **REQ-5.2**: The dialog MUST allow entering query syntax compatible with `EntityQuery` (e.g. `classname=monster_* AND targetname=`), previewing the count of matching entities, selecting an action:
  - Delete matched entities
  - Set / Add keyvalues (`key=value, ...`)
  - Remove key (`key`)
- **REQ-5.3**: The operation MUST execute with a single undo step (`FL_ENTITIES`), reload entity representations in the viewport, and log the count of modified entities.

---

## 6. Interactive Viewport Screenshot Tool
- **REQ-6.1**: The GUI MUST provide a `Take Viewport Screenshot` command accessible from:
  - `File -> Export -> Viewport Screenshot (.tga)`
  - `View -> Take Screenshot` (Shortcut: `F12`)
- **REQ-6.2**: Capturing a screenshot MUST write the current 3D viewport buffer cleanly to `bspguy_work/screenshots/` (or configured screenshot directory) with timestamp/sequence naming and log the output path without exiting the application.

---

## 7. Command Palette & Action Registry Parity
- **REQ-7.1**: All added GUI operations (`map.merge_maps`, `window.merge_maps`, `window.keyvalues`, `window.transform`, `window.lightmap_editor`, `window.overview`, `window.debug`, `file.import_add_map`, `map.transform_move`, `tools.modent_query`, `view.screenshot`) MUST be registered in `ActionRegistryInit.cpp` with categories and searchable descriptions.
