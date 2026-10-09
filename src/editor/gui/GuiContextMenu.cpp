#include "../Gui.h"
#include "../Renderer.h"
#include "../BspRenderer.h"
#include "bsp/Bsp.h"
#include "../Settings.h"
#include "lang.h"
#include "filedialog/ImFileDialog.h"
#include "imgui_stdlib.h"
#include "quantizer.h"
#include "vis.h"
#include "winding.h"
#include "util.h"
#include "log.h"
#include "BspMerger.h"
#include "LeafNavMesh.h"
#include "as.h"
#include "lodepng.h"
#include "fmt/format.h"
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <unordered_set>

extern float g_tooltip_delay;
extern std::string g_working_dir;
extern Settings g_settings;
extern Renderer* g_app;
extern int pickCount;
extern std::string g_game_dir;
extern bool g_console_visible;
extern std::vector<BspRenderer*> mapRenderers;
extern bool DebugKeyPressed;

enum cell_type : unsigned char
{
	cell_none = 0,
	cell_brush,
	cell_wall,
	cell_hostage,
	cell_player_TT,
	cell_player_CT,
	cell_light,
	cell_buyzone,
	cell_bombzone,
	cell_waterzone
};

static int UMD_MAGIC = 'umd2';

struct cell
{
	unsigned char height;
	unsigned char height_offset;
	unsigned char texid;
	cell_type type;
};

static int cell_idx(const vec3& pos, const vec3& mins, float cell_size, int cell_x, int cell_y, int cell_layers, int layer)
{
	int x = static_cast<int>(std::round((pos.x - mins.x) / cell_size));
	int y = static_cast<int>(std::round((pos.y - mins.y) / cell_size));
	int lvl = static_cast<int>(std::round((pos.z - mins.z) / cell_size));

	if (x < 0 || x >= cell_x || y < 0 || y >= cell_y || layer < 0 || layer >= cell_layers)
	{
		return -1;
	}

	int lvlIdx = lvl * cell_x * cell_y * cell_layers;

	y = cell_y - 1 - y;

	int index = lvlIdx + layer * cell_x * cell_y + y * cell_x + x;
	return index;
}

static inline void IMGUI_TOOLTIP(ImGuiContext& g, const std::string& text)
{
	if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
	{
		ImGui::BeginTooltip();
		ImGui::TextUnformatted(text.c_str());
		ImGui::EndTooltip();
	}
}

namespace umd_flags
{
enum
{
	UMD_TEXTURES_SKIP_OPTIMIZE = 1 << 0,
	UMD_OPTIMIZE_DISABLED = 1 << 1
};
}

void Gui::drawBspContexMenu()
{
	ImGuiContext& g = *GImGui;

	Bsp* map = app->getSelectedMap();

	if (!map)
		return;

	BspRenderer* rend = map->getBspRender();

	if (!rend)
		return;

	auto entIdxs = app->pickInfo.selectedEnts;

	if (app->originHovered && entIdxs.size())
	{
		int entIdx = entIdxs[0];
		Entity* ent = map->ents[entIdx];
		int modelIdx = ent->getBspModelIdx();

		if (ImGui::BeginPopup("ent_context") || ImGui::BeginPopup("empty_context"))
		{
			if (modelIdx > 0 && app->transformTarget == TRANSFORM_ORIGIN)
			{
				BSPMODEL& model = map->models[modelIdx];

				if (ImGui::MenuItem(get_localized_string(LANG_0430).c_str(), ""))
				{
					map->models[modelIdx].vOrigin = getCenter(map->models[modelIdx].nMins,
															  map->models[modelIdx].nMaxs);
					rend->refreshModel(modelIdx);
					pickCount++; // force gui refresh
				}

				if (ImGui::BeginMenu(get_localized_string(LANG_0431).c_str()))
				{
					if (ImGui::MenuItem(get_localized_string(LANG_0432).c_str()))
					{
						map->models[modelIdx].vOrigin = getCenter(map->models[modelIdx].nMins,
																  map->models[modelIdx].nMaxs);
						map->models[modelIdx].vOrigin.z = model.nMaxs.z;
						rend->refreshModel(modelIdx);
						pickCount++;
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0433).c_str()))
					{
						map->models[modelIdx].vOrigin = getCenter(map->models[modelIdx].nMins,
																  map->models[modelIdx].nMaxs);
						map->models[modelIdx].vOrigin.z = model.nMins.z;
						rend->refreshModel(modelIdx);
						pickCount++;
					}
					ImGui::Separator();
					if (ImGui::MenuItem(get_localized_string(LANG_0434).c_str()))
					{
						map->models[modelIdx].vOrigin = getCenter(map->models[modelIdx].nMins,
																  map->models[modelIdx].nMaxs);
						map->models[modelIdx].vOrigin.x = model.nMins.x;
						rend->refreshModel(modelIdx);
						pickCount++;
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0435).c_str()))
					{
						map->models[modelIdx].vOrigin = getCenter(map->models[modelIdx].nMins,
																  map->models[modelIdx].nMaxs);
						map->models[modelIdx].vOrigin.x = model.nMaxs.x;
						rend->refreshModel(modelIdx);
						pickCount++;
					}
					ImGui::Separator();
					if (ImGui::MenuItem(get_localized_string(LANG_0436).c_str()))
					{
						map->models[modelIdx].vOrigin = getCenter(map->models[modelIdx].nMins,
																  map->models[modelIdx].nMaxs);
						map->models[modelIdx].vOrigin.y = model.nMins.y;
						rend->refreshModel(modelIdx);
						pickCount++;
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0437).c_str()))
					{
						map->models[modelIdx].vOrigin = getCenter(map->models[modelIdx].nMins,
																  map->models[modelIdx].nMaxs);
						map->models[modelIdx].vOrigin.y = model.nMaxs.y;
						rend->refreshModel(modelIdx);
						pickCount++;
					}

					ImGui::EndMenu();
				}

				ImGui::Separator();
				if (ImGui::MenuItem("Fix transparent rendering"))
				{
					rend->pushUndoState("Fix transparency", FL_ENTITIES | FL_TEXTURES);
					for (int entIdx : entIdxs)
					{
						Entity* sel_ent = map->ents[entIdx];
						int sel_modelIdx = sel_ent->getBspModelIdx();
						if (sel_modelIdx <= 0)
							continue;

						BSPMODEL& sel_model = map->models[sel_modelIdx];
						for (int i = 0; i < sel_model.nFaces; i++)
						{
							int faceIdx = sel_model.iFirstFace + i;
							BSPFACE32& face = map->faces[faceIdx];
							BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
							map->fix_transparency(texinfo.iMiptex);
						}
						rend->refreshEnt(entIdx);
					}
					rend->reuploadTextures();
					rend->preRenderFaces();
					pickCount++;
				}
			}
			else if (modelIdx > 0)
			{
				BSPMODEL& model = map->models[modelIdx];

				if (ImGui::MenuItem(get_localized_string(LANG_0430).c_str(), ""))
				{
					ent->setOrAddKeyvalue("origin", (-getCenter(map->models[modelIdx].nMins,
																map->models[modelIdx].nMaxs))
														.toKeyvalueString());
					rend->refreshEnt(entIdx);
					pickCount++; // force gui refresh
				}

				if (ImGui::BeginMenu(get_localized_string(LANG_0431).c_str()))
				{
					if (ImGui::MenuItem(get_localized_string(LANG_0432).c_str()))
					{
						vec3 vOrigin = getCenter(map->models[modelIdx].nMins,
												 map->models[modelIdx].nMaxs);
						vOrigin.z = model.nMaxs.z;
						ent->setOrAddKeyvalue("origin", (-vOrigin).toKeyvalueString());
						rend->refreshEnt(entIdx);
						pickCount++;
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0433).c_str()))
					{
						vec3 vOrigin = getCenter(map->models[modelIdx].nMins,
												 map->models[modelIdx].nMaxs);
						vOrigin.z = model.nMins.z;
						ent->setOrAddKeyvalue("origin", (-vOrigin).toKeyvalueString());
						rend->refreshEnt(entIdx);
						pickCount++;
					}
					ImGui::Separator();
					if (ImGui::MenuItem(get_localized_string(LANG_0434).c_str()))
					{
						vec3 vOrigin = getCenter(map->models[modelIdx].nMins,
												 map->models[modelIdx].nMaxs);
						vOrigin.x = model.nMins.x;
						ent->setOrAddKeyvalue("origin", (-vOrigin).toKeyvalueString());
						rend->refreshEnt(entIdx);
						pickCount++;
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0435).c_str()))
					{
						vec3 vOrigin = getCenter(map->models[modelIdx].nMins,
												 map->models[modelIdx].nMaxs);
						vOrigin.x = model.nMaxs.x;
						ent->setOrAddKeyvalue("origin", (-vOrigin).toKeyvalueString());
						rend->refreshEnt(entIdx);
					}
					ImGui::Separator();
					if (ImGui::MenuItem(get_localized_string(LANG_0436).c_str()))
					{
						vec3 vOrigin = getCenter(map->models[modelIdx].nMins,
												 map->models[modelIdx].nMaxs);
						vOrigin.y = model.nMins.y;
						ent->setOrAddKeyvalue("origin", (-vOrigin).toKeyvalueString());
						rend->refreshEnt(entIdx);
						pickCount++;
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0437).c_str()))
					{
						vec3 vOrigin = getCenter(map->models[modelIdx].nMins,
												 map->models[modelIdx].nMaxs);
						vOrigin.y = model.nMaxs.y;
						ent->setOrAddKeyvalue("origin", (-vOrigin).toKeyvalueString());
						rend->refreshEnt(entIdx);
						pickCount++;
					}
					ImGui::EndMenu();
				}

				ImGui::Separator();
				if (ImGui::MenuItem("Fix transparent rendering"))
				{
					rend->pushUndoState("Fix transparency", FL_ENTITIES | FL_TEXTURES);
					for (int entIdx : entIdxs)
					{
						Entity* sel_ent = map->ents[entIdx];
						int sel_modelIdx = sel_ent->getBspModelIdx();
						if (sel_modelIdx <= 0)
							continue;

						BSPMODEL& sel_model = map->models[sel_modelIdx];
						for (int i = 0; i < sel_model.nFaces; i++)
						{
							int faceIdx = sel_model.iFirstFace + i;
							BSPFACE32& face = map->faces[faceIdx];
							BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
							map->fix_transparency(texinfo.iMiptex);
						}
						rend->refreshEnt(entIdx);
					}
					rend->reuploadTextures();
					rend->preRenderFaces();
					pickCount++;
				}
			}
			else
			{
				ImGui::BeginDisabled();
				ImGui::MenuItem("No selected model");
				ImGui::EndDisabled();
			}
			ImGui::EndPopup();
		}

		return;
	}

	if (ImGui::BeginPopup("face_context"))
		{
			if (app->pickInfo.selectedFaces.size() > 0)
			{
				bool allWorld = true;
				for (int fIdx : app->pickInfo.selectedFaces)
				{
					if (map->get_model_from_face(fIdx) != 0)
					{
						allWorld = false;
						break;
					}
				}

				if (allWorld)
				{
					if (ImGui::MenuItem(get_localized_string("DELETE_FACES").c_str(), "Del"))
					{
						rend->pushUndoState("Delete Faces", EDIT_MODEL_LUMPS);
						map->remove_faces(app->pickInfo.selectedFaces);
						app->deselectFaces();
						rend->reload();
						pickCount++;
					}
					IMGUI_TOOLTIP(g, "Delete picked polygon faces from the BSP model geometry");
					ImGui::Separator();
				}
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0438).c_str(), "Ctrl+C"))
			{
				copyTexture();
			}
			IMGUI_TOOLTIP(g, "Copy picked face texture name, scale, shift offsets, and alignment flags");

			if (ImGui::MenuItem(get_localized_string(LANG_0440).c_str(), "Ctrl+V", false,
								copiedMiptex >= 0 && copiedMiptex < map->textureCount))
			{
				pasteTexture();
			}
			IMGUI_TOOLTIP(g, "Apply copied texture name and mapping coordinates to selected faces");

			ImGui::Separator();

			if (ImGui::MenuItem(get_localized_string("COPY_STYLE").c_str()))
			{
				copyStyle();
			}
			IMGUI_TOOLTIP(g, "Copy dynamic lighting style index from the picked face");

			if (ImGui::MenuItem(get_localized_string("PASTE_STYLE").c_str(), "", false, copiedStyle.valid))
			{
				pasteStyle();
			}
			IMGUI_TOOLTIP(g, "Paste copied dynamic lighting style index onto selected faces");

			ImGui::Separator();

			if (ImGui::MenuItem(get_localized_string(LANG_0442).c_str()))
			{
				copyLightmap();
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0444).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0445).c_str(), "", false, copiedLightmap.face >= 0 && copiedLightmap.face < map->faceCount))
			{
				pasteLightmap();
			}
			IMGUI_TOOLTIP(g, "Paste copied lightmap color samples onto selected face with matching dimensions");

			ImGui::Separator();

			if (ImGui::MenuItem(get_localized_string("SELECT_ALL_TEXTURED").c_str()))
			{
				if (g_app->pickInfo.selectedFaces.size())
				{
					BSPFACE32& selface = map->faces[g_app->pickInfo.selectedFaces[0]];
					BSPTEXTUREINFO& seltexinfo = map->texinfos[selface.iTextureInfo];
					g_app->deselectFaces();
					for (int i = 0; i < map->faceCount; i++)
					{
						BSPFACE32& face = map->faces[i];
						BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
						if (texinfo.iMiptex == seltexinfo.iMiptex)
						{
							rend->highlightFace(i, 1);
							g_app->pickInfo.selectedFaces.push_back(i);
						}
					}
					pickCount++;
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string("SELECT_ALL_TEXTURED_FULL").c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string("SELECT_FACE_MDL").c_str()))
			{
				if (g_app->pickInfo.selectedFaces.size())
				{
					int modelIdx = map->get_model_from_face((int)g_app->pickInfo.selectedFaces[0]);
					if (modelIdx >= 0)
					{
						BSPMODEL& model = map->models[modelIdx];
						for (int i = 0; i < model.nFaces; i++)
						{
							int faceIdx = model.iFirstFace + i;
							rend->highlightFace(faceIdx, 1);
							app->pickInfo.selectedFaces.push_back(faceIdx);
						}
					}
					pickCount++;
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string("SELECT_FACE_MDL_FULL").c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::BeginMenu("Select Linked"))
			{
				for (int i = 0; i < 5; i++)
				{
					if (ImGui::MenuItem(fmt::format("Depth {}", (i + 1)).c_str()))
					{
						for (int n = 0; n <= i; n++)
						{
							std::vector<int> surfEdges;
							std::vector<int> vertices;
							for (auto f : app->pickInfo.selectedFaces)
							{
								BSPFACE32 face = map->faces[f];

								for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++)
								{
									int edgeIdx = map->surfedges[e];
									surfEdges.push_back(edgeIdx);

									for (int v = 0; v < 2; v++)
									{
										int vertIdx = map->edges[abs(edgeIdx)].iVertex[v];
										vertices.push_back(vertIdx);
									}
								}
							}

							for (int f = 0; f < map->faceCount; f++)
							{
								BSPFACE32 face = map->faces[f];

								if (std::find(app->pickInfo.selectedFaces.begin(), app->pickInfo.selectedFaces.end(), (int)f) != app->pickInfo.selectedFaces.end())
								{
									continue;
								}

								bool found = false;

								for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++)
								{
									int edgeIdx = map->surfedges[e];
									if (std::find(surfEdges.begin(), surfEdges.end(), edgeIdx) != surfEdges.end())
									{
										found = true;
										app->pickInfo.selectedFaces.push_back(f);
										rend->highlightFace(f, 1);
										break;
									}

									for (int v = 0; v < 2; v++)
									{
										int vertIdx = map->edges[abs(edgeIdx)].iVertex[v];

										if (std::find(vertices.begin(), vertices.end(), vertIdx) != vertices.end())
										{
											found = true;
											app->pickInfo.selectedFaces.push_back(f);
											rend->highlightFace(f, 1);
											break;
										}
									}
								}

								if (found)
									continue;
							}
						}
					}
				}

				ImGui::EndMenu();
			}

			ImGui::Separator();

			if (ImGui::BeginMenu(get_localized_string(LANG_0466).c_str(), !app->isLoading && map && !app->pickInfo.selectedFaces.empty()))
			{
				std::string timestamp = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
				std::string export_path = g_working_dir + "exported_models/" + map->bsp_name + "/faces_" + timestamp + ".bsp";

				if (ImGui::BeginMenu(get_localized_string(LANG_0467).c_str(), !app->isLoading))
				{
					if (ImGui::MenuItem(get_localized_string(LANG_0468).c_str(), 0, false, !app->isLoading))
					{
						ExportFaceModel(map, export_path, app->pickInfo.selectedFaces, 0, false);
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0469).c_str(), 0, false, !app->isLoading))
					{
						ExportFaceModel(map, export_path, app->pickInfo.selectedFaces, 2, false);
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0470).c_str(), 0, false, !app->isLoading))
					{
						ExportFaceModel(map, export_path, app->pickInfo.selectedFaces, 1, false);
					}
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu(get_localized_string(LANG_0471).c_str(), !app->isLoading && map))
				{
					if (ImGui::MenuItem(get_localized_string(LANG_1070).c_str(), 0, false, !app->isLoading))
					{
						ExportFaceModel(map, export_path, app->pickInfo.selectedFaces, 0, true);
					}
					if (ImGui::MenuItem(get_localized_string(LANG_1071).c_str(), 0, false, !app->isLoading))
					{
						ExportFaceModel(map, export_path, app->pickInfo.selectedFaces, 2, true);
					}
					if (ImGui::MenuItem(get_localized_string(LANG_1072).c_str(), 0, false, !app->isLoading))
					{
						ExportFaceModel(map, export_path, app->pickInfo.selectedFaces, 1, true);
					}
					ImGui::EndMenu();
				}

				ImGui::EndMenu();
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0472).c_str());
				ImGui::EndTooltip();
			}

			ImGui::EndPopup();
		}

		if (!app->originHovered && ImGui::BeginPopup("ent_context"))
		{
			if (entIdxs.size())
			{
			Entity* ent = map->ents[entIdxs[0]];
			int modelIdx = ent->getBspModelIdx();
			if (modelIdx < 0 && ent->isWorldSpawn())
				modelIdx = 0;

			if (modelIdx != 0 || app->hasCopiedEnt())
			{
				if (modelIdx != 0)
				{
					if (ImGui::MenuItem(get_localized_string(LANG_0446).c_str(), "Ctrl+X", false, app->pickInfo.selectedEnts.size()))
					{
						app->cutEnt();
					}
					IMGUI_TOOLTIP(g, "Cut selected entity to clipboard");

					if (ImGui::MenuItem(get_localized_string(LANG_0448).c_str(), "Ctrl+C", false, app->pickInfo.selectedEnts.size()))
					{
						app->copyEnt();
					}
					IMGUI_TOOLTIP(g, "Copy selected entity to clipboard");
				}

				if (app->hasCopiedEnt())
				{
					if (ImGui::BeginMenu((get_localized_string(LANG_0449) + "###BeginPaste").c_str()))
					{
						if (ImGui::MenuItem((get_localized_string(LANG_0449) + "###BEG_PASTE1").c_str(), "Ctrl+V", false))
						{
							app->pasteEnt(false);
						}
						IMGUI_TOOLTIP(g, "Paste entity from clipboard in front of the 3D camera");

						if (ImGui::MenuItem((get_localized_string(LANG_0450) + "###BEG_OPASTE1").c_str(), 0, false))
						{
							app->pasteEnt(true);
						}
						IMGUI_TOOLTIP(g, "Paste clipboard entity preserving its original coordinates");

						if (ImGui::MenuItem("Paste with bspmodel###BEG_PASTE2", "Ctrl+V", false))
						{
							app->pasteEnt(false, true);
						}
						IMGUI_TOOLTIP(g, "Paste entity while duplicating and attaching its underlying BSP model data");

						if (ImGui::MenuItem("Paste at this origin###BEG_PASTE_ORIGIN", 0, false))
						{
							vec3 pivot = vec3();
							for (int i : entIdxs)
							{
								pivot += map->getEntOrigin(map->ents[i]);
							}
							pivot /= (float)entIdxs.size();
							app->pasteEntAtOrigin(pivot);
						}
						IMGUI_TOOLTIP(g, "Paste clipboard entity directly at the picked coordinate point");

						ImGui::EndMenu();
					}
				}

				if (modelIdx != 0)
				{
					if (ImGui::MenuItem(get_localized_string(LANG_0451).c_str(), "Del"))
					{
						app->deleteEnts();
					}
					IMGUI_TOOLTIP(g, "Delete selected entity from the map");
				}
			}
			if (entIdxs[0] < (int)map->ents.size() && map->ents[entIdxs[0]]->hide)
			{
				if (ImGui::MenuItem(get_localized_string(LANG_0453).c_str(), get_localized_string(LANG_0454).c_str()))
				{
					map->ents[entIdxs[0]]->hide = false;
					rend->refreshEnt(entIdxs[0]);
					app->updateEntConnections();
				}
			}
			else if (ImGui::MenuItem(get_localized_string(LANG_0455).c_str(), get_localized_string(LANG_0454).c_str()))
			{
				map->hideEnts();
				app->clearSelection();
				rend->preRenderEnts();
				app->updateEntConnections();
				pickCount++;
			}

			ImGui::Separator();
			if (modelIdx >= 0)
			{
				BSPMODEL& model = map->models[modelIdx];
				if (ImGui::BeginMenu(get_localized_string(LANG_0456).c_str()))
				{
					if (modelIdx > 0 || map->is_bsp_model)
					{
						if (ImGui::BeginMenu(get_localized_string(LANG_0457).c_str(), !app->isLoading && (modelIdx > 0 || map->is_bsp_model)))
						{
							if (ImGui::MenuItem(get_localized_string(LANG_0458).c_str()))
							{
								rend->pushUndoState("Regenerate Clipnodes", EDIT_MODEL_LUMPS | FL_CLIPNODES | FL_PLANES | FL_MODELS);
								map->regenerate_model_clipnodes_universal(modelIdx, -1);
								rend->refreshModelClipnodes(modelIdx);
								checkValidHulls();
								print_log(get_localized_string(LANG_0328), modelIdx);
							}

							ImGui::Separator();

							for (int i = 1; i < MAX_MAP_HULLS; i++)
							{
								if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str()))
								{
									rend->pushUndoState("Regenerate Hull " + std::to_string(i), EDIT_MODEL_LUMPS | FL_CLIPNODES | FL_PLANES | FL_MODELS);
									map->regenerate_model_clipnodes_universal(modelIdx, i);
									rend->refreshModelClipnodes(modelIdx);
									checkValidHulls();
									print_log(get_localized_string(LANG_0329), i, modelIdx);
								}
							}
							ImGui::EndMenu();
						}

						if (ImGui::BeginMenu(get_localized_string(LANG_0459).c_str(), !app->isLoading))
						{
							if (ImGui::MenuItem(get_localized_string(LANG_0460).c_str()))
							{
								rend->pushUndoState("Delete All Hulls", EDIT_MODEL_LUMPS | FL_CLIPNODES | FL_PLANES | FL_MODELS);
								map->delete_hull(0, modelIdx, -1);
								map->delete_hull(1, modelIdx, -1);
								map->delete_hull(2, modelIdx, -1);
								map->delete_hull(3, modelIdx, -1);
								rend->refreshModel(modelIdx);
								checkValidHulls();
								print_log(get_localized_string(LANG_0330), modelIdx);
							}
							if (ImGui::MenuItem(get_localized_string(LANG_1069).c_str()))
							{
								rend->pushUndoState("Delete Clipnodes", EDIT_MODEL_LUMPS | FL_CLIPNODES | FL_PLANES | FL_MODELS);
								map->delete_hull(1, modelIdx, -1);
								map->delete_hull(2, modelIdx, -1);
								map->delete_hull(3, modelIdx, -1);
								rend->refreshModelClipnodes(modelIdx);
								checkValidHulls();
								print_log(get_localized_string(LANG_0331), modelIdx);
							}

							ImGui::Separator();

							for (int i = 0; i < MAX_MAP_HULLS; i++)
							{
								bool isHullValid = model.iHeadnodes[i] >= 0;

								if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str(), 0, false, isHullValid))
								{
									rend->pushUndoState("Delete Hull " + std::to_string(i), EDIT_MODEL_LUMPS | FL_CLIPNODES | FL_PLANES | FL_MODELS);
									map->delete_hull(i, modelIdx, -1);
									checkValidHulls();
									if (i == 0)
										rend->refreshModel(modelIdx);
									else
										rend->refreshModelClipnodes(modelIdx);
									print_log(get_localized_string(LANG_0332), i, modelIdx);
								}
							}

							ImGui::EndMenu();
						}

						if (ImGui::BeginMenu(get_localized_string(LANG_0461).c_str(), !app->isLoading))
						{
							if (ImGui::MenuItem(get_localized_string(LANG_1152).c_str()))
							{
								rend->pushUndoState("Simplify All Hulls", EDIT_MODEL_LUMPS | FL_CLIPNODES | FL_PLANES | FL_MODELS);
								map->simplify_model_collision(modelIdx, 1);
								map->simplify_model_collision(modelIdx, 2);
								map->simplify_model_collision(modelIdx, 3);
								rend->refreshModelClipnodes(modelIdx);
								checkValidHulls();
								print_log(get_localized_string(LANG_0333), modelIdx);
							}

							ImGui::Separator();

							for (int i = 1; i < MAX_MAP_HULLS; i++)
							{
								if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str(), 0, false, !app->isLoading))
								{
									rend->pushUndoState("Simplify Hull " + std::to_string(i), EDIT_MODEL_LUMPS | FL_CLIPNODES | FL_PLANES | FL_MODELS);
									map->simplify_model_collision(modelIdx, i);
									rend->refreshModelClipnodes(modelIdx);
									checkValidHulls();
									print_log(get_localized_string(LANG_0334), i, modelIdx);
								}
							}

							ImGui::EndMenu();
						}

						bool canRedirect = map->models[modelIdx].iHeadnodes[1] != map->models[modelIdx].iHeadnodes[2] || map->models[modelIdx].iHeadnodes[1] != map->models[modelIdx].iHeadnodes[3];

						if (ImGui::BeginMenu(get_localized_string(LANG_0462).c_str(), canRedirect && !app->isLoading))
						{
							for (int i = 1; i < MAX_MAP_HULLS; i++)
							{
								if (ImGui::BeginMenu(("Hull " + std::to_string(i)).c_str()))
								{

									for (int k = 1; k < MAX_MAP_HULLS; k++)
									{
										if (i == k)
											continue;

										bool isHullValid = map->models[modelIdx].iHeadnodes[k] >= 0 && map->models[modelIdx].iHeadnodes[k] != map->models[modelIdx].iHeadnodes[i];

										if (ImGui::MenuItem(("Hull " + std::to_string(k)).c_str(), 0, false, isHullValid))
										{
											map->models[modelIdx].iHeadnodes[i] = map->models[modelIdx].iHeadnodes[k];
											rend->refreshModelClipnodes(modelIdx);
											checkValidHulls();
											print_log(get_localized_string(LANG_0335), i, k, modelIdx);
										}
									}

									ImGui::EndMenu();
								}
							}

							ImGui::EndMenu();
						}
					}
					if (ImGui::BeginMenu(get_localized_string(LANG_0463).c_str(), !app->isLoading))
					{
						for (int i = 0; i < MAX_MAP_HULLS; i++)
						{
							if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str()))
							{
								map->print_model_hull(modelIdx, i);
								showLogWidget = true;
							}
						}
						ImGui::EndMenu();
					}

					if (ImGui::MenuItem("Print HeadNodes"))
					{
						if (modelIdx >= 0)
						{
							for (int i = 0; i < MAX_MAP_HULLS; i++)
							{
								print_log("iHeadNode{} = {}\n", i, map->models[modelIdx].iHeadnodes[i]);
							}
						}
					}

					ImGui::EndMenu();
				}

				ImGui::Separator();

				bool allowDuplicate = app->pickInfo.selectedEnts.size() > 0;
				if (allowDuplicate && app->pickInfo.selectedEnts.size() > 1)
				{
					for (auto& tmpentIdx : app->pickInfo.selectedEnts)
					{
						if (map->ents[tmpentIdx]->getBspModelIdx() <= 0)
						{
							allowDuplicate = false;
							break;
						}
					}
				}

				if (modelIdx > 0)
				{
					if (ImGui::MenuItem(get_localized_string("LANG_DUPLICATE_BSP").c_str(), 0, false, !app->isLoading && allowDuplicate))
					{
						print_log(get_localized_string(LANG_0336), app->pickInfo.selectedEnts.size());
						for (auto& tmpentIdx : app->pickInfo.selectedEnts)
						{
							if (map->ents[tmpentIdx]->isBspModel())
							{
								app->modelUsesSharedStructures = false;
								map->ents[tmpentIdx]->setOrAddKeyvalue("model", "*" + std::to_string(map->duplicate_model(map->ents[tmpentIdx]->getBspModelIdx())));
							}
						}
						map->remove_unused_model_structures(CLEAN_LEAVES);
						rend->pushUndoState(get_localized_string("LANG_DUPLICATE_BSP"), EDIT_MODEL_LUMPS | FL_ENTITIES);
					}

					if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(get_localized_string("LANG_CREATE_DUPLICATE_BSP").c_str());
						ImGui::EndTooltip();
					}

					bool disableBspDupStruct = !app->modelUsesSharedStructures;
					if (disableBspDupStruct)
					{
						ImGui::BeginDisabled();
					}
					if (ImGui::MenuItem(get_localized_string("LANG_DUPLICATE_BSP_STRUCT").c_str(), 0, false, !app->isLoading && allowDuplicate))
					{
						print_log(get_localized_string(LANG_0336), app->pickInfo.selectedEnts.size());
						for (auto& tmpentIdx : app->pickInfo.selectedEnts)
						{
							if (map->ents[tmpentIdx]->isBspModel())
							{
								map->duplicate_model_structures(map->ents[tmpentIdx]->getBspModelIdx());
								app->modelUsesSharedStructures = false;
							}
						}

						rend->pushUndoState(get_localized_string("LANG_DUPLICATE_BSP_STRUCT"), EDIT_MODEL_LUMPS);
						pickCount++;
					}

					if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(get_localized_string("LANG_CREATE_DUPLICATE_STRUCT").c_str());
						ImGui::EndTooltip();
					}
					if (disableBspDupStruct)
					{
						ImGui::EndDisabled();
					}
					bool IsValidForMerge = false;
					std::vector<Entity*> toMerge;
					if (app->pickInfo.selectedEnts.size() > 1)
					{
						IsValidForMerge = true;
						for (auto tmpentIdx : app->pickInfo.selectedEnts)
						{
							if (tmpentIdx < 0 || tmpentIdx >= (int)map->ents.size())
							{
								IsValidForMerge = false;
								break;
							}
							Entity* e = map->ents[tmpentIdx];
							if (!e->isBspModel() || e->isWorldSpawn())
							{
								IsValidForMerge = false;
								break;
							}
							toMerge.push_back(e);
						}
					}
					// fixme
					if (ImGui::MenuItem("MERGE BSPMODELS (WIP)", 0, false, !app->isLoading && IsValidForMerge))
					{
						std::vector<Entity*> toErasePtrs;

						while (toMerge.size() > 1)
						{
							Entity* e1 = toMerge[toMerge.size() - 1];
							Entity* e2 = toMerge[toMerge.size() - 2];

							int newmodelid = map->merge_two_models_ents(e1, e2);
							if (newmodelid < 0)
							{
								print_log(PRINT_RED, "Merge failed for models {} and {}\n", e1->getBspModelIdx(), e2->getBspModelIdx());
								break;
							}
							e2->setOrAddKeyvalue("model", "*" + std::to_string(newmodelid));
							e1->removeKeyvalue("model");

							rend->refreshModel(newmodelid);
							rend->refreshModelClipnodes(newmodelid);

							toErasePtrs.push_back(e1);
							toMerge.pop_back();
						}

						for (Entity* delent : toErasePtrs)
						{
							auto it = std::find(map->ents.begin(), map->ents.end(), delent);
							if (it != map->ents.end())
							{
								map->ents.erase(it);
								delete delent;
							}
						}

						map->update_ent_lump();
						map->update_lump_pointers();
						map->save_undo_lightmaps();

						// Clean up unused structures
						map->remove_unused_model_structures();

						g_app->pickInfo.selectedEnts.clear();
						rend->loadLightmaps();
						rend->pushUndoState("MERGE BSP ENTITIES", EDIT_MODEL_LUMPS | FL_ENTITIES);
						rend->preRenderEnts();
					}

					if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted("Merge multiple selected brush models into a single combined BSP model (experimental).");
						ImGui::EndTooltip();
					}
				}
				if (ImGui::BeginMenu(get_localized_string(LANG_0466).c_str(), !app->isLoading && map))
				{
					if (ImGui::BeginMenu(get_localized_string(LANG_0467).c_str(), !app->isLoading))
					{
						if (ImGui::MenuItem(get_localized_string(LANG_0468).c_str(), 0, false, !app->isLoading))
						{
							ExportModel(map, "", modelIdx, 0, false);
						}
						IMGUI_TOOLTIP(g, "Export model referencing external WAD textures");

						if (ImGui::MenuItem(get_localized_string(LANG_0469).c_str(), 0, false, !app->isLoading))
						{
							ExportModel(map, "", modelIdx, 2, false);
						}
						IMGUI_TOOLTIP(g, "Export model with embedded GoldSrc 256-color paletted textures");

						if (ImGui::MenuItem(get_localized_string(LANG_0470).c_str(), 0, false, !app->isLoading))
						{
							ExportModel(map, "", modelIdx, 1, false);
						}
						IMGUI_TOOLTIP(g, "Export model with embedded Quake/Xash3D textures");

						ImGui::EndMenu();
					}

					if (ImGui::BeginMenu(get_localized_string(LANG_0471).c_str(), !app->isLoading && map))
					{
						if (ImGui::MenuItem(get_localized_string(LANG_1070).c_str(), 0, false, !app->isLoading))
						{
							ExportModel(map, "", modelIdx, 0, true);
						}
						IMGUI_TOOLTIP(g, "Export model referencing external WAD textures without model origin");

						if (ImGui::MenuItem(get_localized_string(LANG_1071).c_str(), 0, false, !app->isLoading))
						{
							ExportModel(map, "", modelIdx, 2, true);
						}
						IMGUI_TOOLTIP(g, "Export model with embedded GoldSrc textures without model origin");

						if (ImGui::MenuItem(get_localized_string(LANG_1072).c_str(), 0, false, !app->isLoading))
						{
							ExportModel(map, "", modelIdx, 1, true);
						}
						IMGUI_TOOLTIP(g, "Export model with embedded Quake/Xash3D textures without model origin");
						ImGui::EndMenu();
					}

					ImGui::EndMenu();
				}

				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(get_localized_string(LANG_0472).c_str());
					ImGui::EndTooltip();
				}
			}
			if (ImGui::MenuItem(app->movingEnt ? "Ungrab" : "Grab", "Alt+G"))
			{
				if (!app->movingEnt)
					app->grabEnt();
				else
				{
					app->ungrabEnt();
				}
			}
			IMGUI_TOOLTIP(g, "Toggle interactive mouse movement of selected entity in 3D space");

			if (ImGui::MenuItem(get_localized_string(LANG_0474).c_str(), "Ctrl+M"))
			{
				showTransformWidget = true;
			}
			IMGUI_TOOLTIP(g, "Open numeric translation, rotation, and scaling transform widget");

			ImGui::Separator();
			if (ImGui::MenuItem(get_localized_string(LANG_0476).c_str(), "Alt+Enter"))
			{
				showKeyvalueWidget = true;
			}
			IMGUI_TOOLTIP(g, "Open SmartEdit Entity Keyvalues and Spawnflags inspector");
		}

		ImGui::EndPopup();
	}

	if (ImGui::BeginPopup("empty_context"))
	{
		bool enabled = app->hasCopiedEnt();

		if (ImGui::MenuItem((get_localized_string(LANG_0449) + "###CONTENT_PASTE1").c_str(), "Ctrl+V", false, enabled))
		{
			app->pasteEnt(false);
		}
		IMGUI_TOOLTIP(g, "Paste entity from clipboard in front of the 3D camera");

		if (ImGui::MenuItem((get_localized_string(LANG_0450) + "###CONTENT_OPASTE1").c_str(), 0, false, enabled))
		{
			app->pasteEnt(true);
		}
		IMGUI_TOOLTIP(g, "Paste clipboard entity preserving its original coordinates");

		if (ImGui::MenuItem("Paste with bspmodel###CONTENT_PASTE2", "Ctrl+V", false))
		{
			app->pasteEnt(false, true);
		}
		IMGUI_TOOLTIP(g, "Paste entity while duplicating and attaching its underlying BSP model data");

		ImGui::EndPopup();
	}
}

void Gui::drawContextMenu_Entity() {}
void Gui::drawContextMenu_Face() {}
void Gui::drawContextMenu_Empty() {}
