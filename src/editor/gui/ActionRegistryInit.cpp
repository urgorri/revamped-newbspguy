#include "ActionRegistry.h"
#include "GuiCommandPalette.h"
#include "../Gui.h"
#include "../Renderer.h"
#include "../BspRenderer.h"
#include "bsp/Bsp.h"
#include "../Settings.h"
#include "lang.h"
#include "filedialog/ImFileDialog.h"

extern Settings g_settings;
extern Renderer* g_app;

void RegisterAllAppActions(Gui* gui, Renderer* app)
{
	ActionRegistry& reg = ActionRegistry::getInstance();
	reg.clear();

	auto hasMap = [app]() -> bool
	{
		return app && app->getSelectedMap() != nullptr;
	};

	// ----------------------------------------------------
	// FILE ACTIONS
	// ----------------------------------------------------
	reg.registerAction({"file.open", "Open Map...", "File", "Ctrl+O",
						"Open an existing BSP or supported map file",
						[gui]()
						{ ifd::FileDialog::Instance().Open("OpenMapDialog", "Open BSP Map", "Valve BSP (*.bsp){.bsp},.*"); },
						[]()
						{ return true; }});

	reg.registerAction({"file.save", "Save Map", "File", "Ctrl+S",
						"Save current BSP changes",
						[app]()
						{
							Bsp* map = app ? app->getSelectedMap() : nullptr;
							if (map && !map->is_mdl_model)
							{
								std::string savePath = map->bsp_path;
								if (savePath.empty())
									savePath = g_working_dir + map->bsp_name + ".bsp";
								map->update_ent_lump();
								map->update_lump_pointers();
								map->validate();
								map->write(savePath);
								if (map->getBspRender())
								{
									map->getBspRender()->preRenderEnts();
								}
							}
						},
						hasMap});

	reg.registerAction({"file.save_as", "Save Map As...", "File", "Ctrl+Shift+S",
						"Save current map to a new BSP file",
						[app]()
						{
							Bsp* map = app ? app->getSelectedMap() : nullptr;
							if (map)
								ifd::FileDialog::Instance().Save("SaveMapAsDialog", "Save BSP As", "Valve BSP (*.bsp){.bsp}");
						},
						hasMap});

	reg.registerAction({"file.import_add_map", "Add Map to Scene / Renderer...", "File", "",
						"Load an additional BSP map into the renderer workspace",
						[gui]()
						{
							if (gui)
							{
								gui->showImportMapWidget_Type = SHOW_IMPORT_ADD_NEW;
								gui->showImportMapWidget = true;
							}
						},
						[]()
						{ return true; }});

	reg.registerAction({"file.screenshot_overview", "Render Map Overview Screenshot...", "File", "",
						"Open overview screenshot renderer tool",
						[gui]()
						{ if (gui) gui->showOverviewWidget = true; },
						hasMap});

	reg.registerAction({"file.settings", "Application Settings", "File", "Ctrl+P",
						"Open bspguy preferences and settings",
						[gui]()
						{ if (gui) gui->showSettingsWidget = true; },
						[]()
						{ return true; }});

	reg.registerAction({"file.exit", "Exit bspguy", "File", "Alt+F4",
						"Close the editor",
						[app]()
						{ if (app && app->window) glfwSetWindowShouldClose(app->window, 1); },
						[]()
						{ return true; }});

	// ----------------------------------------------------
	// EDIT ACTIONS
	// ----------------------------------------------------
	reg.registerAction({"edit.undo", "Undo", "Edit", "Ctrl+Z",
						"Undo last operation",
						[app]()
						{
							Bsp* map = app ? app->getSelectedMap() : nullptr;
							if (map && map->getBspRender())
								map->getBspRender()->undo();
						},
						[app]() -> bool
						{
							Bsp* map = app ? app->getSelectedMap() : nullptr;
							BspRenderer* rend = map ? map->getBspRender() : nullptr;
							return rend && !rend->undoHistory.empty() && !app->isLoading;
						}});

	reg.registerAction({"edit.redo", "Redo", "Edit", "Ctrl+Y",
						"Redo previously undone operation",
						[app]()
						{
							Bsp* map = app ? app->getSelectedMap() : nullptr;
							if (map && map->getBspRender())
								map->getBspRender()->redo();
						},
						[app]() -> bool
						{
							Bsp* map = app ? app->getSelectedMap() : nullptr;
							BspRenderer* rend = map ? map->getBspRender() : nullptr;
							return rend && !rend->redoHistory.empty() && !app->isLoading;
						}});

	reg.registerAction({"edit.cut", "Cut Entity", "Edit", "Ctrl+X",
						"Cut selected entity to clipboard",
						[app]()
						{ if (app) app->cutEnt(); },
						[app]() -> bool
						{ return app && !app->pickInfo.selectedEnts.empty(); }});

	reg.registerAction({"edit.copy", "Copy Entity / Texture", "Edit", "Ctrl+C",
						"Copy selected entity or face texture",
						[app, gui]()
						{
							if (!app)
								return;
							if (!app->pickInfo.selectedEnts.empty())
								app->copyEnt();
							else if (!app->pickInfo.selectedFaces.empty() && gui)
								gui->copyTexture();
						},
						[app]() -> bool
						{ return app && (!app->pickInfo.selectedEnts.empty() || !app->pickInfo.selectedFaces.empty()); }});

	reg.registerAction({"edit.paste", "Paste Entity", "Edit", "Ctrl+V",
						"Paste entity from clipboard at camera target",
						[app]()
						{ if (app) app->pasteEnt(false); },
						[app]() -> bool
						{ return app && app->hasCopiedEnt(); }});

	reg.registerAction({"edit.paste_origin", "Paste at Entity Origin", "Edit", "",
						"Paste copied entity at selected entity's pivot origin",
						[app]()
						{
							if (!app)
								return;
							Bsp* map = app->getSelectedMap();
							if (!map || app->pickInfo.selectedEnts.empty())
								return;
							vec3 pivot = vec3();
							for (int i : app->pickInfo.selectedEnts)
								pivot += map->getEntOrigin(map->ents[i]);
							pivot /= (float)app->pickInfo.selectedEnts.size();
							app->pasteEntAtOrigin(pivot);
						},
						[app]() -> bool
						{ return app && app->hasCopiedEnt() && !app->pickInfo.selectedEnts.empty(); }});

	reg.registerAction({"edit.delete", "Delete Entity", "Edit", "Del",
						"Delete selected entities",
						[app]()
						{ if (app) app->deleteEnts(); },
						[app]() -> bool
						{ return app && !app->pickInfo.selectedEnts.empty(); }});

	reg.registerAction({"edit.select_all", "Select All Entities", "Edit", "Ctrl+A",
						"Select all non-worldspawn entities",
						[app]()
						{
							if (!app)
								return;
							Bsp* map = app->getSelectedMap();
							if (!map)
								return;
							app->pickInfo.selectedEnts.clear();
							for (size_t i = 1; i < map->ents.size(); i++)
								app->pickInfo.AddSelectedEnt((int)i);
						},
						hasMap});

	reg.registerAction({"edit.deselect_all", "Deselect All", "Edit", "Esc",
						"Deselect all entities and faces",
						[app]()
						{
							if (app)
							{
								app->deselectFaces();
								app->deselectObject();
							}
						},
						hasMap});

	reg.registerAction({"edit.select_same_texture", "Select Faces with Same Texture", "Edit", "Ctrl+Alt+A",
						"Select all faces in the map sharing the active face texture",
						[app]()
						{
							if (!app)
								return;
							Bsp* map = app->getSelectedMap();
							if (!map || app->pickInfo.selectedFaces.empty())
								return;
							BSPFACE32& selface = map->faces[app->pickInfo.selectedFaces[0]];
							BSPTEXTUREINFO& seltexinfo = map->texinfos[selface.iTextureInfo];
							app->deselectFaces();
							for (int i = 0; i < map->faceCount; i++)
							{
								BSPFACE32& face = map->faces[i];
								BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
								if (texinfo.iMiptex == seltexinfo.iMiptex)
								{
									map->getBspRender()->highlightFace(i, 1);
									app->pickInfo.selectedFaces.push_back(i);
								}
							}
						},
						[app]() -> bool
						{ return app && !app->pickInfo.selectedFaces.empty(); }});

	reg.registerAction({"edit.split_face", "Split Face", "Edit", "F",
						"Split face across selected edge vertices",
						[app]()
						{ if (app) app->splitModelFace(); },
						[app]() -> bool
						{ return app && app->pickMode != PICK_OBJECT; }});

	reg.registerAction({"edit.transform", "Transform Tool", "Edit", "Ctrl+M",
						"Open translation, rotation, scaling transform widget",
						[gui]()
						{ if (gui) gui->showTransformWidget = !gui->showTransformWidget; },
						[app]() -> bool
						{ return app && !app->pickInfo.selectedEnts.empty(); }});

	reg.registerAction({"edit.keyvalues", "Entity Keyvalues (SmartEdit)", "Edit", "Ctrl+G",
						"Edit keyvalues, flags, and attributes for selected entity",
						[gui]()
						{ if (gui) gui->showKeyvalueWidget = !gui->showKeyvalueWidget; },
						[app]() -> bool
						{ return app && !app->pickInfo.selectedEnts.empty(); }});

	// ----------------------------------------------------
	// VIEW ACTIONS
	// ----------------------------------------------------
	reg.registerAction({"view.command_palette", "Command Palette...", "View", "Ctrl+K",
						"Search and run any editor command or action",
						[]()
						{ GuiCommandPalette::getInstance().toggle(); },
						[]()
						{ return true; }});

	reg.registerAction({"view.ortho_toggle", "Toggle Perspective / Orthographic", "View", "Ctrl+O",
						"Switch between 3D perspective and 2D ortho overview camera",
						[gui]()
						{ if (gui) gui->orthoMode = !gui->orthoMode; },
						[]()
						{ return true; }});

	reg.registerAction({"view.wireframe_toggle", "Toggle Wireframe Rendering", "View", "Ctrl+W",
						"Toggle polygon wireframe overlay",
						[]()
						{ g_render_flags ^= RENDER_WIREFRAME; },
						[]()
						{ return true; }});

	reg.registerAction({"view.textures_toggle", "Toggle Textures Rendering", "View", "Ctrl+T",
						"Toggle surface texture rendering",
						[]()
						{ g_render_flags ^= RENDER_TEXTURES; },
						[]()
						{ return true; }});

	reg.registerAction({"view.lightmaps_toggle", "Toggle Lightmaps Rendering", "View", "Ctrl+L",
						"Toggle lightmap brightness rendering",
						[]()
						{ g_render_flags ^= RENDER_LIGHTMAPS; },
						[]()
						{ return true; }});

	reg.registerAction({"view.skybox_toggle", "Toggle Skybox Rendering", "View", "Ctrl+Alt+S",
						"Toggle 3D environment skybox rendering",
						[]()
						{ g_render_flags ^= RENDER_SKYBOX; },
						[]()
						{ return true; }});

	reg.registerAction({"view.screenshot", "Take Viewport Screenshot", "View", "F12",
						"Capture full-resolution viewport image and save to screenshots directory",
						[app]()
						{ if (app) app->save_viewport_screenshot(); },
						[]()
						{ return true; }});

	// ----------------------------------------------------
	// TOOLS & REPAIRS
	// ----------------------------------------------------
	reg.registerAction({"tools.fix_transparency", "Fix Transparent Rendering", "Tools", "",
						"Fix black outline / alpha artifacts on transparent masked textures",
						[app]()
						{
							Bsp* map = app ? app->getSelectedMap() : nullptr;
							BspRenderer* rend = map ? map->getBspRender() : nullptr;
							if (!map || !rend)
								return;
							rend->pushUndoState("Fix transparency", FL_ENTITIES | FL_TEXTURES);
							for (int i = 0; i < map->faceCount; i++)
							{
								BSPFACE32& face = map->faces[i];
								if (face.iTextureInfo < map->texinfoCount)
								{
									BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
									map->fix_transparency(texinfo.iMiptex);
								}
							}
							rend->reuploadTextures();
							rend->preRenderFaces();
						},
						hasMap});

	reg.registerAction({"tools.fix_face_ranges", "Fix Invalid Model Face Ranges", "Tools", "",
						"Repair models with out-of-bounds face ranges",
						[app]()
						{
							Bsp* map = app ? app->getSelectedMap() : nullptr;
							BspRenderer* rend = map ? map->getBspRender() : nullptr;
							if (map && rend)
							{
								rend->pushUndoState("Fix model face ranges", EDIT_MODEL_LUMPS);
								map->fix_invalid_model_face_ranges();
							}
						},
						hasMap});

	reg.registerAction({"tools.optimize_bsp", "Optimize BSP Geometry", "Tools", "",
						"Remove duplicate vertices, unused planes, and compact lumps",
						[app]()
						{
							Bsp* map = app ? app->getSelectedMap() : nullptr;
							BspRenderer* rend = map ? map->getBspRender() : nullptr;
							if (map && rend)
							{
								rend->pushUndoState("Optimize BSP", EDIT_MODEL_LUMPS);
								map->remove_unused_model_structures();
							}
						},
						hasMap});

	reg.registerAction({"tools.modent_query", "Batch Entity Query (modent)...", "Tools", "",
						"Search, modify, or delete entities across active map using query expressions",
						[gui]()
						{ if (gui) gui->showModentDialog = !gui->showModentDialog; },
						hasMap});

	reg.registerAction({"map.merge_maps", "Merge Maps...", "Map", "",
						"Combine, stack, or align multiple BSP maps into a single file",
						[gui]()
						{ if (gui) gui->showMergeMapWidget = !gui->showMergeMapWidget; },
						[]()
						{ return true; }});

	reg.registerAction({"map.transform_move", "Shift / Move Map (X, Y, Z)...", "Map", "",
						"Translate all map geometry and entities by delta offset",
						[gui]()
						{ if (gui) gui->showShiftMapDialog = !gui->showShiftMapDialog; },
						hasMap});

	reg.registerAction({"map.recompile_lighting", "Recompile Lighting (HLRAD)", "Map", "",
						"Execute external HLRAD compiler and hot reload lightmaps in viewport",
						[gui]()
						{ if (gui) gui->recompileLighting(); },
						hasMap});

	// ----------------------------------------------------
	// WINDOWS
	// ----------------------------------------------------
	reg.registerAction({"window.limits", "Map Limits & Engine Statistics", "Windows", "F2",
						"Open engine limits and lump usage analyzer",
						[gui]()
						{ if (gui) gui->showLimitsWidget = !gui->showLimitsWidget; },
						hasMap});

	reg.registerAction({"window.entity_report", "Entity Report Window", "Windows", "F3",
						"Open full searchable entity report table",
						[gui]()
						{ if (gui) gui->showEntityReport = !gui->showEntityReport; },
						hasMap});

	reg.registerAction({"window.texture_browser", "Texture Browser", "Windows", "F4",
						"Browse embedded and WAD textures",
						[gui]()
						{ if (gui) gui->showTextureBrowser = !gui->showTextureBrowser; },
						hasMap});

	reg.registerAction({"window.face_editor", "Face Editor Widget", "Windows", "F6",
						"Open texture alignment, scaling, and face manipulation panel",
						[gui]()
						{ if (gui) gui->showFaceEditWidget = !gui->showFaceEditWidget; },
						hasMap});

	reg.registerAction({"window.keyvalues", "Entity Keyvalues (SmartEdit)", "Windows", "Alt+Enter",
						"Open Entity Keyvalue & SmartEdit inspector panel",
						[gui]()
						{ if (gui) gui->showKeyvalueWidget = !gui->showKeyvalueWidget; },
						hasMap});

	reg.registerAction({"window.transform", "3D Transform Tool", "Windows", "Ctrl+M",
						"Open 3D coordinate transform manipulator panel",
						[gui]()
						{ if (gui) gui->showTransformWidget = !gui->showTransformWidget; },
						hasMap});

	reg.registerAction({"window.lightmap_editor", "Lightmap Editor", "Windows", "",
						"Open face lightmap luminance and RGB color editor panel",
						[gui]()
						{ if (gui) gui->showLightmapEditorWidget = !gui->showLightmapEditorWidget; },
						hasMap});

	reg.registerAction({"window.overview", "Map Overview (2D Radar)", "Windows", "",
						"Open 2D radar/overview map rendering controls",
						[gui]()
						{ if (gui) gui->showOverviewWidget = !gui->showOverviewWidget; },
						hasMap});

	reg.registerAction({"window.merge_maps", "Map Merger", "Windows", "",
						"Open multi-map merger tool window",
						[gui]()
						{ if (gui) gui->showMergeMapWidget = !gui->showMergeMapWidget; },
						[]()
						{ return true; }});

	reg.registerAction({"window.log", "Log & Console Output", "Windows", "F5",
						"Open bspguy output log console",
						[gui]()
						{ if (gui) gui->showLogWidget = !gui->showLogWidget; },
						[]()
						{ return true; }});

	reg.registerAction({"window.goto", "Go to Coordinates (GOTO)", "Windows", "Ctrl+Shift+G",
						"Teleport camera to specific coordinates or entity index",
						[gui]()
						{ if (gui) gui->showGOTOWidget = !gui->showGOTOWidget; },
						hasMap});

	reg.registerAction({"window.debug", "Debug PVS / Engine Inspector", "Windows", "",
						"Open low-level BSP engine debug and PVS inspection panel",
						[gui]()
						{ if (gui) gui->showDebugWidget = !gui->showDebugWidget; },
						hasMap});

	reg.registerAction({"help.shortcuts", "Help & Keybindings", "Help", "F1",
						"Show help dialogue and keyboard shortcuts list",
						[gui]()
						{ if (gui) gui->showHelpWidget = !gui->showHelpWidget; },
						[]()
						{ return true; }});

	reg.registerAction({"help.about", "About bspguy", "Help", "",
						"Show application version, build date, and contributor information",
						[gui]()
						{ if (gui) gui->showAboutWidget = !gui->showAboutWidget; },
						[]()
						{ return true; }});
}
