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
#include "MutexManager.h"
#include "GuiCommandPalette.h"
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

void Gui::drawMenuBar()
{
	ImGuiContext& g = *GImGui;
	static bool ditheringEnabled = false;
	Bsp* map = app->getSelectedMap();
	BspRenderer* rend = map ? map->getBspRender() : NULL;

	if (ImGui::BeginMainMenuBar())
	{
		if (ifd::FileDialog::Instance().IsDone("PngDirOpenDialog"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				std::string png_import_dir = stripFileName(res.string());
				g_settings.lastdir = stripFileName(res.string());
				if (dirExists(png_import_dir))
				{
					createDir(g_working_dir + "temp/");
					std::string tempWad = g_working_dir + "temp/temp2.wad";
					removeFile(tempWad);
					if (map && map->import_textures_to_wad(tempWad, png_import_dir, ditheringEnabled))
					{
						map->ImportWad(tempWad);
					}
					removeFile(tempWad);
				}
			}
			ifd::FileDialog::Instance().Close();
		}

		if (ifd::FileDialog::Instance().IsDone("WadOpenDialog"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				if (fileExists(res.string()))
				{
					if (!map)
					{
						app->addMap(new Bsp(""));
						app->selectMapId(0);
						map = app->getSelectedMap();
					}

					g_settings.AddRecentFile(res.string());
					for (size_t i = 0; i < map->ents.size(); i++)
					{
						if (map->ents[i]->keyvalues["classname"] == "worldspawn")
						{
							std::vector<std::string> wadNames = splitString(map->ents[i]->keyvalues["wad"], ";");
							std::string newWadNames;
							for (size_t k = 0; k < wadNames.size(); k++)
							{
								if (wadNames[k].find(res.filename().string()) == std::string::npos)
									newWadNames += wadNames[k] + ";";
							}
							map->ents[i]->setOrAddKeyvalue("wad", newWadNames);
							break;
						}
					}
					app->updateEnts();
					map->ImportWad(res.string());
					app->reloadBspModels();
					g_settings.lastdir = stripFileName(res.string());
				}
			}
			ifd::FileDialog::Instance().Close();
		}

		if (ifd::FileDialog::Instance().IsDone("MapOpenDialog"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				if (fileExists(res.string()))
				{
					g_settings.AddRecentFile(res.string());
					OpenFile(res.string());
					g_settings.lastdir = stripFileName(res.string());
				}
			}
			ifd::FileDialog::Instance().Close();
		}

		drawMenu_File();
		drawMenu_Edit();
		drawMenu_View();
		drawMenu_Map();
		drawMenu_Tools();
		drawMenu_Create();
		drawMenu_Windows();
		drawMenu_Help();
		if (showDebugWidget)
		{
			drawMenu_Debug();
		}

		ImGui::EndMainMenuBar();
	}
}

void Gui::drawMenu_File()
{
	ImGuiContext& g = *GImGui;
	static bool ditheringEnabled = false;
	Bsp* map = app->getSelectedMap();
	BspRenderer* rend = map ? map->getBspRender() : NULL;

	static cell_type cell_brush = cell_type::cell_brush;
	static cell_type cell_wall = cell_type::cell_wall;
	static cell_type cell_waterzone = cell_type::cell_waterzone;
	static cell_type cell_buyzone = cell_type::cell_buyzone;
	static cell_type cell_bombzone = cell_type::cell_bombzone;
	static cell_type cur_cell = cell_brush;

	static bool splitSmd = false;
	static bool oneRoot = false;
	static int g_scale = 1;
	static bool g_group_faces = false;
	static bool g_group_as_objects = false;
	static bool merge_faces = false;
	static bool use_one_back_vert = false;
	static bool inside_box = false;
	static int cell_size = 32;
	static bool texture_support = true;
	static bool fill_all_space = false;
	static bool scan_faces = true;
	static bool NO_OPTIMIZE = false;

	if (ImGui::BeginMenu(get_localized_string(LANG_0478).c_str()))
	{
		if (ImGui::MenuItem(get_localized_string(LANG_0479).c_str(), "Ctrl+S", false, map && !map->is_mdl_model && !app->isLoading))
		{
			map->update_ent_lump();
			map->update_lump_pointers();
			map->validate();
			map->write(map->bsp_path);
		}
		IMGUI_TOOLTIP(g, "Saves all current entity, texture, and geometry changes directly to the active .bsp file");
		if (ImGui::BeginMenu(get_localized_string(LANG_0480).c_str(), map && !map->is_mdl_model && !app->isLoading))
		{
			bool old_is_bsp30ext = map->is_bsp30ext;
			bool old_is_bsp2 = map->is_bsp2;
			bool old_is_bsp2_old = map->is_bsp2_old;
			bool old_is_bsp29 = map->is_bsp29;
			bool old_is_32bit_clipnodes = map->is_32bit_clipnodes;
			bool old_is_broken_clipnodes = map->is_broken_clipnodes;
			bool old_is_blue_shift = map->is_blue_shift;
			bool old_is_colored_lightmap = map->is_colored_lightmap;

			int old_bsp_version = map->bsp_header.nVersion;

			bool is_default_format = !old_is_bsp30ext && !old_is_bsp2 &&
									 !old_is_bsp2_old && !old_is_bsp29 && !old_is_32bit_clipnodes && !old_is_broken_clipnodes && !old_is_blue_shift && old_is_colored_lightmap && old_bsp_version == 30;

			bool is_need_reload = false;

			if (ImGui::MenuItem(get_localized_string(LANG_0481).c_str(), NULL, is_default_format && map->is_texture_has_pal))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = false;
					map->is_bsp2_old = false;
					map->is_bsp29 = false;
					map->is_32bit_clipnodes = false;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = true;
					map->target_save_texture_has_pal = true;

					map->bsp_header.nVersion = 30;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0341));
					}
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0341));
				}
			}

			if (ImGui::MenuItem((get_localized_string(LANG_0481) + "[NO PALETTE]").c_str(), NULL, is_default_format && !map->is_texture_has_pal))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = false;
					map->is_bsp2_old = false;
					map->is_bsp29 = false;
					map->is_32bit_clipnodes = false;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = true;
					map->target_save_texture_has_pal = true;

					map->bsp_header.nVersion = 30;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0341));
					}
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0341));
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (is_default_format)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0482).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0483).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0484).c_str());
				}
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0485).c_str(), NULL, old_is_blue_shift))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = false;
					map->is_bsp2_old = false;
					map->is_bsp29 = false;
					map->is_32bit_clipnodes = false;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = true;
					map->is_colored_lightmap = true;
					map->target_save_texture_has_pal = true;

					map->bsp_header.nVersion = 30;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
					}
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_blue_shift)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0486).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0487).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0488).c_str());
				}
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0489).c_str(), NULL, old_is_bsp29 && !old_is_broken_clipnodes && old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = false;
					map->is_bsp2_old = false;
					map->is_bsp29 = true;
					map->is_32bit_clipnodes = false;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = true;
					map->target_save_texture_has_pal = false;

					map->bsp_header.nVersion = 29;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
					}
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp29 && !old_is_broken_clipnodes && old_is_colored_lightmap)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0490).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0491).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0492).c_str());
				}
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0493).c_str(), NULL, old_is_bsp29 && !old_is_broken_clipnodes && !old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = false;
					map->is_bsp2_old = false;
					map->is_bsp29 = true;
					map->is_32bit_clipnodes = false;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = false;
					map->target_save_texture_has_pal = false;

					map->bsp_header.nVersion = 29;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
					}
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp29 && !old_is_broken_clipnodes && !old_is_colored_lightmap)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0494).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0495).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0496).c_str());
				}
				ImGui::EndTooltip();
			}

			if (old_is_broken_clipnodes)
			{
				if (ImGui::MenuItem(get_localized_string(LANG_0497).c_str(), NULL, old_is_bsp29 && old_is_colored_lightmap))
				{
					if (map->isValid())
					{
						map->update_ent_lump();
						map->update_lump_pointers();

						map->is_bsp30ext = false;
						map->is_bsp2 = false;
						map->is_bsp2_old = false;
						map->is_bsp29 = true;
						map->is_32bit_clipnodes = false;
						map->is_broken_clipnodes = true;
						map->is_blue_shift = false;
						map->is_colored_lightmap = true;
						map->target_save_texture_has_pal = false;

						map->bsp_header.nVersion = 29;

						if (map->validate() && map->isValid())
						{
							is_need_reload = true;
							map->write(map->bsp_path);
						}
						else
						{
							print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
						}
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
					}
				}

				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					if (old_is_bsp29 && old_is_colored_lightmap)
					{
						ImGui::TextUnformatted(get_localized_string(LANG_0498).c_str());
					}
					else if (map->isValid())
					{
						ImGui::TextUnformatted(get_localized_string(LANG_0499).c_str());
					}
					else
					{
						ImGui::TextUnformatted(get_localized_string(LANG_0500).c_str());
					}
					ImGui::EndTooltip();
				}

				if (ImGui::MenuItem(get_localized_string(LANG_0501).c_str(), NULL, old_is_bsp29 && !old_is_colored_lightmap))
				{
					if (map->isValid())
					{
						map->update_ent_lump();
						map->update_lump_pointers();

						map->is_bsp30ext = false;
						map->is_bsp2 = false;
						map->is_bsp2_old = false;
						map->is_bsp29 = true;
						map->is_32bit_clipnodes = false;
						map->is_broken_clipnodes = true;
						map->is_blue_shift = false;
						map->is_colored_lightmap = false;
						map->target_save_texture_has_pal = false;

						map->bsp_header.nVersion = 29;

						if (map->validate() && map->isValid())
						{
							is_need_reload = true;
							map->write(map->bsp_path);
						}
						else
						{
							print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
						}
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
					}
				}

				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					if (old_is_bsp29 && !map->is_colored_lightmap && !old_is_colored_lightmap)
					{
						ImGui::TextUnformatted(get_localized_string(LANG_0502).c_str());
					}
					else if (map->isValid())
					{
						ImGui::TextUnformatted(get_localized_string(LANG_0503).c_str());
					}
					else
					{
						ImGui::TextUnformatted(get_localized_string(LANG_0504).c_str());
					}
					ImGui::EndTooltip();
				}
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0505).c_str(), NULL, old_is_bsp2 && !old_is_bsp2_old && old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = true;
					map->is_bsp2_old = false;
					map->is_bsp29 = true;
					map->is_32bit_clipnodes = true;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = true;
					map->target_save_texture_has_pal = false;

					map->bsp_header.nVersion = 29;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
					}
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp2 && !old_is_bsp2_old && old_is_colored_lightmap)
				{
					ImGui::TextUnformatted("Map already saved in BSP2(29) + COLOR LIGHT format.");
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted("Saving map to BSP2(29) + COLOR LIGHT compatibility format.");
				}
				else
				{
					ImGui::TextUnformatted("Map limits is reached, and can't be converted to BSP2(29) + COLOR LIGH.");
				}
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0506).c_str(), NULL, old_is_bsp2 && !old_is_bsp2_old && !old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = true;
					map->is_bsp2_old = false;
					map->is_bsp29 = true;
					map->is_32bit_clipnodes = true;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = false;
					map->target_save_texture_has_pal = false;

					map->bsp_header.nVersion = 29;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
					}
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp2 && !old_is_bsp2_old && !old_is_colored_lightmap)
				{
					ImGui::TextUnformatted("Map already saved in BSP2(29) + MONO LIGHT format.");
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted("Saving map to BSP2(29) + MONO LIGHT compatibility format.");
				}
				else
				{
					ImGui::TextUnformatted("Map limits is reached, and can't be converted to BSP2(29) + MONO LIGH.");
				}
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0507).c_str(), NULL, old_is_bsp2_old && old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = true;
					map->is_bsp2_old = true;
					map->is_bsp29 = true;
					map->is_32bit_clipnodes = true;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = true;
					map->target_save_texture_has_pal = false;

					map->bsp_header.nVersion = 29;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
					}
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp2_old && !old_is_colored_lightmap)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0508).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0509).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0510).c_str());
				}
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0511).c_str(), NULL, old_is_bsp2_old && !old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = true;
					map->is_bsp2_old = true;
					map->is_bsp29 = true;
					map->is_32bit_clipnodes = true;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = false;
					map->target_save_texture_has_pal = false;

					map->bsp_header.nVersion = 29;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
					}
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp2_old && !old_is_colored_lightmap)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0512).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0513).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0514).c_str());
				}
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0515).c_str(), NULL, old_is_bsp30ext && old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = true;
					map->is_bsp2 = false;
					map->is_bsp2_old = false;
					map->is_bsp29 = false;
					map->is_32bit_clipnodes = true;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = true;
					map->target_save_texture_has_pal = true;

					map->bsp_header.nVersion = 30;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
					}
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp30ext && old_is_colored_lightmap)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0516).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0517).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0518).c_str());
				}
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0519).c_str(), NULL, old_is_bsp2_old && !old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = true;
					map->is_bsp2 = false;
					map->is_bsp2_old = false;
					map->is_bsp29 = false;
					map->is_32bit_clipnodes = true;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = false;
					map->target_save_texture_has_pal = true;

					map->bsp_header.nVersion = 30;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
					}
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp30ext && !old_is_colored_lightmap)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0520).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0521).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0522).c_str());
				}
				ImGui::EndTooltip();
			}

			map->is_bsp30ext = old_is_bsp30ext;
			map->is_bsp2 = old_is_bsp2;
			map->is_bsp2_old = old_is_bsp2_old;
			map->is_bsp29 = old_is_bsp29;
			map->is_32bit_clipnodes = old_is_32bit_clipnodes;
			map->is_broken_clipnodes = old_is_broken_clipnodes;
			map->is_blue_shift = old_is_blue_shift;
			map->is_colored_lightmap = old_is_colored_lightmap;
			map->bsp_header.nVersion = old_bsp_version;
			if (is_need_reload)
			{
				app->reloadMaps();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(get_localized_string(LANG_0523).c_str()))
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0524).c_str()))
			{
				filterNeeded = true;

				ifd::FileDialog::Instance().Open("MapOpenDialog", "Select map path", "Map file (*.bsp){.bsp}", false, g_settings.lastdir);
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0525).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0526).c_str()))
			{
				filterNeeded = true;
				ifd::FileDialog::Instance().Open("MapOpenDialog", "Select model path", "Model file (*.mdl){.mdl}", false, g_settings.lastdir);
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0527).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string("OPEN_SPR_VIEW").c_str()))
			{
				filterNeeded = true;
				ifd::FileDialog::Instance().Open("MapOpenDialog", "Select sprite path", "Sprite file (*.spr){.spr}", false, g_settings.lastdir);
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0527).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string("OPEN_XASHNT_CSM_VIEW").c_str()))
			{
				filterNeeded = true;
				ifd::FileDialog::Instance().Open("MapOpenDialog", "Select XashNT CSM path", "XashNT CSM model (*.csm){.csm}", false, g_settings.lastdir);
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0527).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0528).c_str()))
			{
				filterNeeded = true;
				ifd::FileDialog::Instance().Open("MapOpenDialog", "Select wad path", "Wad file (*.wad){.wad}", false, g_settings.lastdir);
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0529).c_str());
				ImGui::EndTooltip();
			}
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem(get_localized_string(LANG_0530).c_str(), NULL, false, !app->isLoading && map))
		{
			filterNeeded = true;
			int mapRenderId = map->getBspRenderId();
			if (mapRenderId >= 0)
			{
				if (rend)
				{
					map->setBspRender(NULL);
					app->deselectObject();
					app->clearSelection();
					app->deselectMap();
					mapRenderers.erase(mapRenderers.begin() + mapRenderId);
					delete rend;
					rend = NULL;
					map = NULL;
					app->selectMapId(0);

					if (mapRenderers.empty())
					{
						for (auto& s : mdl_models)
						{
							delete s.second;
						}
						mdl_models.clear();
					}
				}
			}
		}

		if (mapRenderers.size() > 1)
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0531).c_str(), NULL, false, !app->isLoading))
			{
				filterNeeded = true;
				if (map)
				{
					app->deselectObject();
					app->clearSelection();
					app->deselectMap();
					app->clearMaps();
					app->selectMapId(0);

					rend = NULL;
					map = NULL;
					app->selectMapId(0);
					print_log(get_localized_string(LANG_0907));

					for (auto& s : mdl_models)
					{
						delete s.second;
					}
					mdl_models.clear();
				}
			}
		}

		if (ImGui::BeginMenu(get_localized_string(LANG_0532).c_str(), !app->isLoading && map))
		{
			if (ImGui::MenuItem("Viewport Screenshot (.tga)", "F12"))
			{
				app->save_viewport_screenshot();
			}
			IMGUI_TOOLTIP(g, "Capture full-resolution viewport image and save to screenshots folder");

			if (ImGui::MenuItem(get_localized_string(LANG_0533).c_str(), NULL, false, map && !map->is_mdl_model))
			{
				std::string entFilePath;
				if (g_settings.same_dir_for_ent)
				{
					std::string bspFilePath = map->bsp_path;
					if (bspFilePath.size() < 4 || bspFilePath.rfind(".bsp") != bspFilePath.size() - 4)
					{
						entFilePath = bspFilePath + ".ent";
					}
					else
					{
						entFilePath = bspFilePath.substr(0, bspFilePath.size() - 4) + ".ent";
					}
				}
				else
				{
					createDir(g_working_dir + "exported_entities/");
					entFilePath = g_working_dir + "exported_entities/" + (map->bsp_name + ".ent");
				}

				print_log(get_localized_string(LANG_0342), entFilePath);
				map->export_entities(entFilePath);
			}
			IMGUI_TOOLTIP(g, "Export all entity definitions as a raw .ent text file");

			if (ImGui::MenuItem(get_localized_string(LANG_0534).c_str(), NULL, false, map && !map->is_mdl_model))
			{
				createDir(g_working_dir + "exported_wads/");
				std::string wadPath = g_working_dir + "exported_wads/" + map->bsp_name + ".wad";
				print_log(get_localized_string(LANG_0343), g_working_dir + "exported_wads/", map->bsp_name + ".wad");
				if (map->ExportEmbeddedWad(wadPath))
				{
					print_log(get_localized_string(LANG_0344));
					map->delete_embedded_textures();
					if (map->ents.size())
					{
						std::string wadstr = map->ents[0]->keyvalues["wad"];
						if (wadstr.find(map->bsp_name + ".wad;") == std::string::npos)
						{
							map->ents[0]->keyvalues["wad"] += map->bsp_name + ".wad;";
						}
					}
				}
			}
			IMGUI_TOOLTIP(g, "Extract all internal map textures into an external WAD package and update worldspawn");

			static bool splitSmd = true;
			static bool oneRoot = false;

			if (ImGui::BeginMenu("StudioModel Data (.smd) [WIP]", map && !map->is_mdl_model))
			{
				if (ImGui::MenuItem("Split to goldsrc", NULL, &splitSmd))
				{
					// splitSmd
				}
				IMGUI_TOOLTIP(g, "Split mesh into submodels compatible with GoldSrc engine limits");

				if (ImGui::MenuItem("Only root bone", NULL, &oneRoot))
				{
					// oneRoot
				}
				IMGUI_TOOLTIP(g, "Attach all vertices to a single root bone without skeletal animation");

				if (ImGui::MenuItem("Do Export", NULL))
				{
					std::string exportPath = g_working_dir + "exported_geometry/";
					createDir(exportPath);
					map->ExportToSmdWIP(exportPath, splitSmd, oneRoot);
				}
				IMGUI_TOOLTIP(g, "Export BSP geometry to Valve SMD format in exported_geometry folder");
				ImGui::EndMenu();
			}

			static int g_scale = 1;

			static bool g_group_faces = false;
			static bool g_group_as_objects = false;

			if (ImGui::BeginMenu("Wavefront(.obj)/XashNT(.csm) [WIP]", map && !map->is_mdl_model))
			{
				if (ImGui::BeginMenu("Select scale"))
				{
					if (ImGui::MenuItem(get_localized_string(LANG_0535).c_str(), NULL, g_scale == 1))
					{
						g_scale = 1;
					}

					for (int scale = 2; scale < 10; scale += 2)
					{
						std::string scaleitem = "UpScale x" + std::to_string(scale);
						if (ImGui::MenuItem(scaleitem.c_str(), NULL, g_scale == scale))
						{
							g_scale = scale;
						}
					}

					for (int scale = 16; scale > 0; scale -= 2)
					{
						std::string scaleitem = "DownScale x" + std::to_string(scale);
						if (ImGui::MenuItem(scaleitem.c_str(), NULL, g_scale == -scale))
						{
							g_scale = -scale;
						}
					}

					ImGui::EndMenu();
				}

				if (ImGui::MenuItem("Create face groups[OBJ]", NULL, &g_group_faces))
				{
					if (g_group_faces)
						g_group_as_objects = !g_group_faces;
				}

				if (ImGui::MenuItem("Create face objects[OBJ]", NULL, &g_group_as_objects))
				{
					if (g_group_faces)
						g_group_faces = !g_group_as_objects;
				}

				if (ImGui::BeginMenu("Export .obj"))
				{
					std::string exportPath = g_working_dir + "exported_geometry/";
					createDir(exportPath);
					if (ImGui::MenuItem("Export only bsp"))
					{
						map->ExportToObjWIP(exportPath, g_scale, false, false, false, !g_group_faces && !g_group_as_objects ? 0 : (g_group_faces ? 1 : 2));
					}
					if (ImGui::MenuItem("Export with models"))
					{
						map->ExportToObjWIP(exportPath, g_scale, false, true);
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Export .csm"))
				{
					std::string exportPath = g_working_dir + "exported_geometry/";
					createDir(exportPath);
					if (ImGui::MenuItem("Export only bsp"))
					{
						map->ExportToObjWIP(exportPath, g_scale, false, false, true);
					}
					if (ImGui::MenuItem("Export with models"))
					{
						map->ExportToObjWIP(exportPath, g_scale, false, true, true);
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0536).c_str());
				ImGui::EndTooltip();
			}

			static bool merge_faces = true;
			static bool use_one_back_vert = true;
			static bool inside_box = false;

			if (ImGui::BeginMenu("ValveHammerEditor (.map) [WIP]", map && !map->is_mdl_model))
			{
				ImGui::MenuItem("Merge faces", NULL, &merge_faces);

				ImGui::MenuItem("One back vert", NULL, &use_one_back_vert);

				ImGui::MenuItem("Create box", NULL, &inside_box);

				ImGui::Separator();

				if (ImGui::MenuItem("Full .map"))
				{
					std::string exportPath = g_working_dir + "exported_maps/";
					createDir(exportPath);
					map->ExportToMapWIP(exportPath, false, merge_faces, use_one_back_vert, inside_box);
				}
				else if (ImGui::MenuItem("Selected faces"))
				{
					std::string exportPath = g_working_dir + "exported_maps/";
					createDir(exportPath);
					map->ExportToMapWIP(exportPath, true, merge_faces, use_one_back_vert, inside_box);
				}
				ImGui::EndMenu();
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Export .map ( WIP )");
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0537).c_str(), NULL, false, map && !map->is_mdl_model))
			{
				createDir(g_working_dir + "exported_vis/");
				map->ExportPortalFile(g_working_dir + "exported_vis/" + map->bsp_name + ".bsp");
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0538).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0539).c_str(), NULL, false, map && !map->is_mdl_model))
			{
				std::string newpath;
				createDir(g_working_dir + "exported_rad/");
				map->ExportExtFile(g_working_dir + "exported_rad/" + map->bsp_name + ".bsp", newpath);
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Export face extens (.ext) file for rad.exe");
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0540).c_str(), NULL, false, map && !map->is_mdl_model))
			{
				createDir(g_working_dir + "exported_lighting/");
				map->ExportLightFile(g_working_dir + "exported_lighting/" + map->bsp_name + ".lit");
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0541).c_str());
				ImGui::EndTooltip();
			}

			ImGui::SetNextWindowSize({-1.0f, 600.0f});
			if (ImGui::BeginMenu(get_localized_string(LANG_1076).c_str(), map && !map->is_mdl_model))
			{
				int modelIdx = -1;
				auto pickEnt = app->pickInfo.selectedEnts;
				if (pickEnt.size())
				{
					modelIdx = map->ents[pickEnt[0]]->getBspModelIdx();
				}
				for (int i = 0; i < map->modelCount; i++)
				{
					if (ImGui::BeginMenu(((modelIdx != i ? "Export Model" : "+ Export Model") + std::to_string(i) + ".bsp").c_str()))
					{
						if (ImGui::BeginMenu(get_localized_string(LANG_1077).c_str(), true))
						{
							if (ImGui::MenuItem(get_localized_string(LANG_1154).c_str(), 0, false, true))
							{
								ExportModel(map, "", i, 0, false);
							}
							if (ImGui::MenuItem(get_localized_string(LANG_1155).c_str(), 0, false, true))
							{
								ExportModel(map, "", i, 2, false);
							}
							if (ImGui::MenuItem(get_localized_string(LANG_1156).c_str(), 0, false, true))
							{
								ExportModel(map, "", i, 1, false);
							}
							ImGui::EndMenu();
						}
						if (ImGui::BeginMenu(get_localized_string(LANG_1078).c_str(), true))
						{
							if (ImGui::MenuItem(get_localized_string(LANG_1173).c_str(), 0, false, true))
							{
								ExportModel(map, "", i, 0, true);
							}
							if (ImGui::MenuItem(get_localized_string(LANG_1174).c_str(), 0, false, true))
							{
								ExportModel(map, "", i, 2, true);
							}
							if (ImGui::MenuItem(get_localized_string(LANG_1175).c_str(), 0, false, true))
							{
								ExportModel(map, "", i, 1, true);
							}
							ImGui::EndMenu();
						}

						ImGui::EndMenu();
					}
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Export Selected Faces as BSP...", map && !map->is_mdl_model && !app->pickInfo.selectedFaces.empty()))
			{
				std::string timestamp = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
				std::string export_path = g_working_dir + map->bsp_name + "_faces_" + timestamp + ".bsp";

				if (ImGui::BeginMenu(get_localized_string(LANG_1077).c_str()))
				{
					if (ImGui::MenuItem(get_localized_string(LANG_1154).c_str()))
					{
						ExportFaceModel(map, export_path, app->pickInfo.selectedFaces, 0, false);
					}
					if (ImGui::MenuItem(get_localized_string(LANG_1155).c_str()))
					{
						ExportFaceModel(map, export_path, app->pickInfo.selectedFaces, 2, false);
					}
					if (ImGui::MenuItem(get_localized_string(LANG_1156).c_str()))
					{
						ExportFaceModel(map, export_path, app->pickInfo.selectedFaces, 1, false);
					}
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu(get_localized_string(LANG_1078).c_str()))
				{
					if (ImGui::MenuItem(get_localized_string(LANG_1173).c_str()))
					{
						ExportFaceModel(map, export_path, app->pickInfo.selectedFaces, 0, true);
					}
					if (ImGui::MenuItem(get_localized_string(LANG_1174).c_str()))
					{
						ExportFaceModel(map, export_path, app->pickInfo.selectedFaces, 2, true);
					}
					if (ImGui::MenuItem(get_localized_string(LANG_1175).c_str()))
					{
						ExportFaceModel(map, export_path, app->pickInfo.selectedFaces, 1, true);
					}
					ImGui::EndMenu();
				}

				ImGui::EndMenu();
			}

			if (ImGui::MenuItem("Render Map Overview Screenshot...", 0, false, map != nullptr))
			{
				showOverviewWidget = true;
			}

			if (ImGui::BeginMenu(get_localized_string(LANG_0542).c_str(), map && !map->is_mdl_model))
			{
				std::string hash = "##1";
				for (auto& wad : rend->wads)
				{
					if (wad->dirEntries.size() == 0)
						continue;
					hash += "1";
					if (ImGui::MenuItem((basename(wad->filename) + hash).c_str()))
					{
						print_log(get_localized_string(LANG_0345), basename(wad->filename));
						std::string exportPath = g_working_dir + "exported_wads/pngs/" + basename(wad->filename);
						createDir(g_working_dir + "exported_wads/");
						createDir(g_working_dir + "exported_wads/pngs/");
						map->export_wad_to_pngs(wad->filename, exportPath);
					}
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("UnrealMapDrawTool (.umd) [WIP]", map && !map->is_mdl_model))
			{
				static int cell_size = 24;
				static bool texture_support = true;
				static bool fill_all_space = true;
				static bool NO_OPTIMIZE = false;
				static bool scan_faces = true;

				if (ImGui::BeginMenu("Options###1"))
				{
					if (ImGui::BeginMenu("[Scan] Cell size"))
					{
						for (int tmpSize = 0; tmpSize <= 64;)
						{
							if (tmpSize <= 32)
							{
								tmpSize += 4;
							}
							else
							{
								tmpSize += 8;
							}

							if (ImGui::MenuItem((std::to_string(tmpSize) + " units###" + std::to_string(tmpSize)).c_str(), NULL, cell_size == tmpSize))
							{
								cell_size = tmpSize;
							}
						}
						ImGui::EndMenu();
					}

					if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted("Smaller scan cell size is more better\nbut needed more time to scan.");
						ImGui::EndTooltip();
					}

					if (ImGui::MenuItem("Support textures", NULL, texture_support))
					{
						texture_support = !texture_support;
						if (texture_support)
						{
							fill_all_space = false;
						}
					}

					if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted("Generate more faces but textured!");
						ImGui::EndTooltip();
					}

					if (ImGui::MenuItem("Fill near faces", NULL, !fill_all_space, !NO_OPTIMIZE))
					{
						fill_all_space = !fill_all_space;
					}

					if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted("Instead fill all NON-EMPTY contents\ndo fill only with near faces.\nCan generate more faces.");
						ImGui::EndTooltip();
					}

					if (ImGui::MenuItem("Scan faces", NULL, scan_faces))
					{
						scan_faces = !scan_faces;
					}

					if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted("Scanning faces instead of leaves, faster than leaves.");
						ImGui::EndTooltip();
					}

					if (ImGui::MenuItem("NO OPTIMIZE [!!WARN!!]", NULL, NO_OPTIMIZE))
					{
						NO_OPTIMIZE = !NO_OPTIMIZE;
						fill_all_space = false;
					}

					if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted("Gives fucking big number of cubes,\n can be used only for test purposes.");
						ImGui::EndTooltip();
					}

					ImGui::EndMenu();
				}

				int hull_for_export = -1;

				if (ImGui::MenuItem("Do Export [MAP]###2", NULL))
				{
					hull_for_export = 0;
					rend->calcFaceMaths();
				}

				if (ImGui::MenuItem("Do Export [HEAD_HULL]###3", NULL))
				{
					hull_for_export = 3;
				}

				if (hull_for_export >= 0)
				{
					print_log("Start exporting to UnrealMapDrawTool....\n");

					int lightEnts = 0;
					for (size_t e = 0; e < map->ents.size(); e++)
					{
						if (map->ents[e]->classname.find("light") != std::string::npos)
						{
							lightEnts++;
						}
					}

					if (lightEnts < 5)
					{
						mapFixLightEnts(map);
					}

					FlushConsoleLog();
					vec3 mins{}, maxs{};
					/*map->get_bounding_box(mins, maxs);*/

					vec3 pos_debug_mins{}, pos_debug_maxs{};

					for (int i = 0; i < map->models[0].nFaces; i++)
					{
						BSPFACE32& face = map->faces[map->models[0].iFirstFace + i];
						for (int e = 0; e < face.nEdges; e++)
						{
							int edgeIdx = map->surfedges[face.iFirstEdge + e];
							BSPEDGE32& edge = map->edges[abs(edgeIdx)];
							int vertIdx = edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1];
							expandBoundingBox(map->verts[vertIdx], mins, maxs);
						}
					}

					FlushConsoleLog();

					mins += rend->mapOffset;
					maxs += rend->mapOffset;

					if (scan_faces)
					{
						mins -= cell_size * 1.5f;
						maxs += cell_size * 1.5f;
					}
					else
					{
						mins -= cell_size * 0.5f;
						maxs += cell_size * 0.5f;
					}

					print_log("Found real world mins/maxs! Map offsets {},{},{}\n", rend->mapOffset.x, rend->mapOffset.y, rend->mapOffset.z);

					int hull = hull_for_export;

					int cell_x = 0;
					for (float x = mins.x; x <= maxs.x; x += cell_size)
					{
						cell_x += 1;
					}
					int cell_y = 0;
					for (float y = mins.y; y <= maxs.y; y += cell_size)
					{
						cell_y += 1;
					}

					int cell_levels = 0;
					for (float z = mins.z; z <= maxs.z; z += cell_size)
					{
						cell_levels += 1;
					}

					int cell_layers = 1;

					std::vector<std::string> umdTextures{};

					std::vector<cell> cell_list((cell_x * cell_y * cell_levels) * cell_layers);
					memset(&cell_list[0], 0, cell_list.size() * sizeof(cell));

					print_log("Pre vars calculated. CellX/Y {}/{} /\n Map mins/maxs {},{},{} / {},{},{}!\n", cell_x, cell_y, mins.x, mins.y, mins.z, maxs.x, maxs.y, maxs.z);
					FlushConsoleLog();

					for (size_t entIdx = 0; entIdx < map->ents.size(); entIdx++)
					{
						print_log("\rProcess {} entity of {}...", entIdx, map->ents.size());
						FlushConsoleLog();
						int modelIdx = map->ents[entIdx]->getBspModelIdx();
						bool worldspawn = map->ents[entIdx]->isWorldSpawn();
						auto entity = map->ents[entIdx];
						if (modelIdx < 0)
						{
							int idx = cell_idx(entity->origin, mins, cell_size * 1.0f, cell_x, cell_y, cell_layers, 0);

							if ((size_t)idx >= cell_list.size())
							{
								print_log("Fatal crash[#3], index {} out of bounds {}\n", idx, cell_list.size());
								return;
							}

							if (idx < (int)cell_list.size())
							{
								if (entity->classname.find("light") != std::string::npos)
								{
									cell_list[idx] = {50, 50, 0, cell_light};
								}
								else if (entity->classname == "hostage_entity")
								{
									cell_list[idx] = {50, 50, 0, cell_hostage};
								}
								else if (entity->classname == "info_player_start")
								{
									cell_list[idx] = {50, 50, 0, cell_player_CT};
								}
								else if (entity->classname == "info_player_deathmatch")
								{
									cell_list[idx] = {50, 50, 0, cell_player_TT};
								}
							}
							else
							{
								print_log("Fatal crash, index {} out of bounds {}\n", idx, cell_list.size());
							}
							continue;
						}

						std::mutex paralel_muta;

						BSPMODEL& model = map->models[modelIdx];

						int headNode = model.iHeadnodes[hull];
						if (headNode < 0)
							continue;

						if (!scan_faces)
						{
							vec3 model_mins{}, model_maxs{};

							if (modelIdx == 0)
							{
								model_mins = mins;
								model_maxs = maxs;
							}
							else
							{
								for (int i = 0; i < model.nFaces; i++)
								{
									BSPFACE32& face = map->faces[model.iFirstFace + i];
									for (int e = 0; e < face.nEdges; e++)
									{
										int edgeIdx = map->surfedges[face.iFirstEdge + e];
										BSPEDGE32& edge = map->edges[abs(edgeIdx)];
										int vertIdx = edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1];
										expandBoundingBox(map->verts[vertIdx], model_mins, model_maxs);
									}
								}

								model_mins += entity->origin;
								model_maxs += entity->origin;
							}

							auto faceIndices = std::vector<int>();
							for (int i = 0; i < model.nFaces; i++)
							{
								faceIndices.push_back(model.iFirstFace + i);
							}

							std::vector<std::vector<vec3>> faceVecs(faceIndices.size());
							for (size_t i = 0; i < faceIndices.size(); i++)
							{
								faceVecs[i] = map->get_face_verts(faceIndices[i]);
							}

							std::vector<vec3> offsets =
								{
									{0, 0, 0},
									{cell_size / 4.f, 0, 0},
									{-cell_size / 4.f, 0, 0},
									{0, cell_size / 4.f, 0},
									{0, -cell_size / 4.f, 0},
									{0, 0, cell_size / 4.f},
									{0, 0, -cell_size / 4.f},
									{cell_size / 2.f, 0, 0},
									{-cell_size / 2.f, 0, 0},
									{0, cell_size / 2.f, 0},
									{0, -cell_size / 2.f, 0},
									{0, 0, cell_size / 2.f},
									{0, 0, -cell_size / 2.f},
								};

							std::vector<float> parallel_X{};
							for (float x = mins.x; x < maxs.x; x += cell_size)
							{
								parallel_X.push_back(x);
							}

							for (float z = mins.z; z < maxs.z; z += cell_size)
							{
								print_log("\rProcess {} entity of {}... [{} of {}].........", entIdx, map->ents.size(), z, maxs.z);
								FlushConsoleLog();

								if (z > model_maxs.z || z < model_mins.z)
								{
									continue;
								}

								for (float y = mins.y; y < maxs.y; y += cell_size)
								{
									if (y > model_maxs.y || y < model_mins.y)
									{
										continue;
									}

									std::for_each(std::execution::par_unseq, parallel_X.begin(), parallel_X.end(),
												  [&](float x)
												  {
													  if (x > model_maxs.x || x < model_mins.x)
													  {
														  return;
													  }

													  unsigned char texid = 0;

													  vec3 pos = vec3(x, y, z);

													  int index = cell_idx(pos, mins, (float)cell_size, cell_x, cell_y, cell_layers, 0);

													  if ((size_t)index >= cell_list.size())
													  {
														  print_log("Fatal crash[#2], index {} out of bounds {}\n", index, cell_list.size());
														  return;
													  }
													  cell& cur_cell = cell_list[index];
													  expandBoundingBox(pos, pos_debug_mins, pos_debug_maxs);

													  bool found = false;
													  int leafIdx = 0;
													  int planeIdx = -1;

													  for (size_t off = 0; off < offsets.size(); off++)
													  {
														  int content = map->pointLeaf(headNode, pos + offsets[off], hull, leafIdx, planeIdx);
														  if (CONTENTS_SOLID == content || (modelIdx > 0 && content == CONTENTS_WATER))
														  {
															  found = true;
															  break;
														  }
													  }

													  /*bool found = false;

													  auto leaf_list = map->getLeafsFromPos(pos, cell_size);
													  for (auto& leaf : leaf_list)
													  {
														  if (map->leaves[leaf].nContents == CONTENTS_SOLID)
														  {
															  found = true;
															  break;
														  }
													  }*/

													  if (found /*|| leaf_list.empty()*/)
													  {
														  int minFace = -1;

														  if (texture_support)
														  {
															  float minDist = cell_size * 1.5f;
															  for (size_t f = 0; f < faceIndices.size(); f++)
															  {
																  BSPFACE32& face = map->faces[faceIndices[f]];

																  if (map->texinfos[face.iTextureInfo].nFlags & TEX_SPECIAL)
																  {
																	  continue;
																  }

																  auto& faceMath = rend->faceMaths[faceIndices[f]];

																  float distanceToPlane = dotProduct(faceMath.normal, pos) - faceMath.fdist;
																  float dot = std::fabs(distanceToPlane);

																  if (dot > minDist)
																  {
																	  continue;
																  }

																  bool isInsideFace = true;
																  const std::vector<vec3>& vertices = faceVecs[f];

																  for (size_t i = 0; i < vertices.size(); i++)
																  {
																	  const vec3& v0 = vertices[i];
																	  const vec3& v1 = vertices[(i + 1) % vertices.size()];
																	  vec3 edge = v1 - v0;
																	  vec3 edgeNormal = crossProduct(faceMath.normal, edge).normalize();

																	  if (dotProduct(edgeNormal, pos - v0) > 0)
																	  {
																		  isInsideFace = false;
																		  break;
																	  }
																  }

																  if (!isInsideFace)
																  {
																	  continue;
																  }

																  if (dot < minDist)
																  {
																	  minDist = dot;
																	  minFace = faceIndices[f];
																  }
															  }
														  }
														  else
														  {
															  float minDist = cell_size * 3.0f;
															  // more fast search
															  for (size_t f = 0; f < faceIndices.size(); f++)
															  {
																  if (pos.dist(rend->faceMaths[faceIndices[f]].center) < minDist)
																  {
																	  if (map->texinfos[map->faces[faceIndices[f]].iTextureInfo].nFlags & TEX_SPECIAL)
																	  {
																		  continue;
																	  }

																	  minFace = faceIndices[f];
																	  break;
																  }
															  }
														  }

														  /*int minFace = -1;
														  float minDist = 1000.0f;

														  for (size_t f = 0; f < faceIndices.size(); f++)
															  {
															  float tmpDist = std::fabs(rend->faceMaths[faceIndices[f]].center.dist(pos));
															  if (tmpDist < minDist)
															  {
																  minDist = tmpDist;
																  minFace = faceIndices[f];
															  }
														  }*/

														  if (minFace >= 0)
														  {
															  BSPFACE32& face = map->faces[minFace];
															  if (face.iTextureInfo >= 0)
															  {
																  BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
																  BSPMIPTEX* tex = NULL;

																  if (texinfo.iMiptex >= 0 && texinfo.iMiptex < map->textureCount)
																  {
																	  int texOffset = ((int*)map->textures)[texinfo.iMiptex + 1];
																	  if (texOffset >= 0)
																	  {
																		  tex = ((BSPMIPTEX*)(map->textures + texOffset));
																		  std::lock_guard<std::mutex> lock(paralel_muta);
																		  bool hasTex = false;
																		  for (size_t t = 0; t < umdTextures.size(); t++)
																		  {
																			  if (umdTextures[t] == tex->szName)
																			  {
																				  if (t <= 0xFF)
																				  {
																					  texid = (unsigned char)t;
																					  hasTex = true;
																					  break;
																				  }
																			  }
																		  }

																		  if (!hasTex && umdTextures.size() < 0xFF)
																		  {
																			  umdTextures.push_back(tex->szName);
																			  texid = (unsigned char)(umdTextures.size() - 1);
																		  }
																	  }
																  }
															  }
														  }

														  if (minFace >= 0 || fill_all_space)
														  {
															  if (worldspawn)
															  {
																  cur_cell = {100, 0, texid, cell_brush};
															  }
															  else if (minFace >= 0)
															  {
																  if (entity->classname == "func_wall")
																  {
																	  cur_cell = {100, 0, texid, cell_wall};
																  }
																  else if (entity->classname == "func_water")
																  {
																	  cur_cell = {100, 0, texid, cell_waterzone};
																  }
																  else if (entity->classname == "func_buyzone")
																  {
																	  cur_cell = {100, 0, texid, cell_buyzone};
																  }
																  else if (entity->classname == "func_bomb_target")
																  {
																	  cur_cell = {100, 0, texid, cell_bombzone};
																  }
																  else
																  {
																	  cur_cell = {100, 0, texid, cell_wall};
																  }
															  }
														  }
													  }
												  });
								}
							}
						}
						else
						{
							// get face list
							auto faces = map->get_faces_from_model(modelIdx);
							for (auto f : faces)
							{
								// convert face to Polygon3D
								std::vector<vec3> face_verts = map->get_face_verts(f);

								Polygon3D poly(face_verts);

								// 2D mins/maxs
								vec2 fmins = poly.localMins;
								fmins -= cell_size * 1.0f;
								vec2 fmaxs = poly.localMaxs;
								fmaxs += cell_size * 1.0f;

								// Normalize plane normal
								vec3 plane_z_normalized = map->getPlaneFromFace(&map->faces[f]).vNormal.normalize();

								// Scan in 2D
								for (float x = fmins.x; x <= fmaxs.x;)
								{
									bool foundall = true;
									for (float y = fmins.y; y <= fmaxs.y;)
									{
										vec2 point = {x, y};
										if (poly.isInside(point))
										{
											y += cell_size / 1.1f;
											// convert 2D to 3D
											vec3 point_3d = poly.unproject(point);
											//// move point to back face
											point_3d -= plane_z_normalized * 0.1f;

											point_3d += map->ents[entIdx]->origin;

											//// clamp to mins/maxs
											point_3d.x = std::max(mins.x, std::min(maxs.x, point_3d.x));
											point_3d.y = std::max(mins.y, std::min(maxs.y, point_3d.y));
											point_3d.z = std::max(mins.z, std::min(maxs.z, point_3d.z));

											int leafIdx = 0;
											int planeIdx = -1;
											int content = map->pointLeaf(headNode, point_3d, hull, leafIdx, planeIdx);
											if (CONTENTS_SOLID == content || (modelIdx > 0 && content == CONTENTS_WATER))
											{
											}
											else
												continue;

											// Process...
											int index = cell_idx(point_3d, mins, cell_size * 1.0f, cell_x, cell_y, cell_layers, 0);

											if ((size_t)index >= cell_list.size())
											{
												print_log("Fatal crash[#2], index {} out of bounds {}\n", index, cell_list.size());
												print_log("Point : {}/{}/{}\n", point_3d.x, point_3d.y, point_3d.z);
												continue;
											}

											cell& cur_cell = cell_list[index];
											expandBoundingBox(point_3d, pos_debug_mins, pos_debug_maxs);

											unsigned char texid = 0;
											int minFace = f;

											if (minFace >= 0)
											{
												BSPFACE32& face = map->faces[minFace];
												if (face.iTextureInfo >= 0)
												{
													BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
													BSPMIPTEX* tex = NULL;

													if (texinfo.iMiptex >= 0 && texinfo.iMiptex < map->textureCount)
													{
														int texOffset = ((int*)map->textures)[texinfo.iMiptex + 1];
														if (texOffset >= 0)
														{
															tex = ((BSPMIPTEX*)(map->textures + texOffset));
															std::lock_guard<std::mutex> lock(paralel_muta);
															bool hasTex = false;
															for (size_t t = 0; t < umdTextures.size(); t++)
															{
																if (umdTextures[t] == tex->szName)
																{
																	if (t <= 0xFF)
																	{
																		texid = (unsigned char)t;
																		hasTex = true;
																		break;
																	}
																}
															}

															if (!hasTex && umdTextures.size() < 0xFF)
															{
																umdTextures.push_back(tex->szName);
																texid = (unsigned char)(umdTextures.size() - 1);
															}
														}
													}
												}
											}

											if (minFace >= 0 || fill_all_space)
											{
												if (worldspawn)
												{
													cur_cell = {100, 0, texid, cell_brush};
												}
												else if (minFace >= 0)
												{
													if (entity->classname == "func_wall")
													{
														cur_cell = {100, 0, texid, cell_wall};
													}
													else if (entity->classname == "func_water")
													{
														cur_cell = {100, 0, texid, cell_waterzone};
													}
													else if (entity->classname == "func_buyzone")
													{
														cur_cell = {100, 0, texid, cell_buyzone};
													}
													else if (entity->classname == "func_bomb_target")
													{
														cur_cell = {100, 0, texid, cell_bombzone};
													}
													else
													{
														cur_cell = {100, 0, texid, cell_wall};
													}
												}
											}
										}
										else
										{
											y += 1.5f;
											foundall = false;
										}
									}
									if (foundall)
									{
										x += cell_size / 1.1f;
									}
									else
									{
										x += 1.5f;
									}
								}
							}
						}
					}

					if (umdTextures.empty())
					{
						umdTextures.push_back("SKY");
					}
					createDir(g_working_dir + "exported_umd/");

					std::ofstream tmpmap(g_working_dir + "exported_umd/exported.umd", std::ios::out | std::ios::binary);

					print_log("\nSaved .umd map to {} path\n", g_working_dir + "exported_umd/exported.umd");

					if (tmpmap.is_open())
					{
						tmpmap.write((const char*)(&UMD_MAGIC), 4);

						int zero = 0;
						tmpmap.write((const char*)(&zero), 4);
						tmpmap.write((const char*)(&zero), 4);
						tmpmap.write((const char*)(&zero), 4);
						tmpmap.write((const char*)(&zero), 4);

						tmpmap.write((const char*)(&cell_x), 4);
						tmpmap.write((const char*)(&cell_y), 4);
						tmpmap.write((const char*)(&cell_size), 4);
						tmpmap.write((const char*)(&cell_size), 4);
						tmpmap.write((const char*)(&cell_levels), 4);
						tmpmap.write((const char*)(&cell_layers), 4);

						for (const auto& tmpcell : cell_list)
						{
							tmpmap.write((const char*)(&tmpcell.height), 1);
							tmpmap.write((const char*)(&tmpcell.height_offset), 1);
							tmpmap.write((const char*)(&tmpcell.texid), 1);
							tmpmap.write((const char*)(&tmpcell.type), 1);
						}

						int skybool = 0;
						tmpmap.write((const char*)(&skybool), 4);

						unsigned int options = 0;
						if (texture_support)
						{
							options |= umd_flags::UMD_TEXTURES_SKIP_OPTIMIZE;
						}
						if (NO_OPTIMIZE)
						{
							options |= umd_flags::UMD_OPTIMIZE_DISABLED;
						}

						tmpmap.write((const char*)(&options), 4);

						// textures
						int textureCount = (int)umdTextures.size();
						tmpmap.write((const char*)(&textureCount), 4);

						for (const auto& texture : umdTextures)
						{
							int length = (int)texture.length();
							tmpmap.write((const char*)(&length), 4);
							tmpmap.write(texture.c_str(), length);
						}

						tmpmap.close();
					}

					print_log("Success! Pos debug mins/maxs {},{},{} / {},{},{}!\n", pos_debug_mins.x, pos_debug_mins.y, pos_debug_mins.z, pos_debug_maxs.x, pos_debug_maxs.y, pos_debug_maxs.z);

					rend->pushUndoState("Export to .umd", EDIT_MODEL_LUMPS | FL_ENTITIES);
					rend->undo();
				}
				ImGui::EndMenu();
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("TEST FEATURE WITH CELL SIZE 16\nNOW ONLY FOR WORLDSPAWN");
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string("LANG_DUMP_TEX").c_str(), NULL, false, map))
			{
				std::string dumpPath = g_working_dir + "dumped_textures/" + map->bsp_name + "/";
				createDir(g_working_dir + "dumped_textures/");
				createDir(dumpPath);

				{
					std::lock_guard<std::mutex> lock(Sync::TexturesList);
					if (g_all_Textures.size() && rend)
					{
						for (const auto& tex : g_all_Textures)
						{
							if (tex != missingTex)
							{
								if (tex->format == GL_RGBA)
									lodepng_encode32_file((dumpPath + std::string(tex->texName) + ".png").c_str(), (const unsigned char*)tex->getData(), tex->width, tex->height);
								else
									lodepng_encode24_file((dumpPath + std::string(tex->texName) + ".png").c_str(), (const unsigned char*)tex->getData(), tex->width, tex->height);
							}
						}
					}
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string("LANG_DUMP_TEX_DESC").c_str());
				ImGui::EndTooltip();
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(get_localized_string(LANG_0543).c_str(), !app->isLoading))
		{
			if (ImGui::MenuItem("Add Map to Scene / Renderer...", NULL, false, !app->isLoading))
			{
				showImportMapWidget_Type = SHOW_IMPORT_ADD_NEW;
				showImportMapWidget = true;
			}
			IMGUI_TOOLTIP(g, "Load an additional BSP map into the renderer workspace without closing current maps");

			if (ImGui::MenuItem(get_localized_string(LANG_0544).c_str(), NULL, false, map && !map->is_mdl_model))
			{
				showImportMapWidget_Type = SHOW_IMPORT_MODEL_BSP;
				showImportMapWidget = true;
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0545).c_str(), NULL, false, map && !map->is_mdl_model))
			{
				showImportMapWidget_Type = SHOW_IMPORT_MODEL_ENTITY;
				showImportMapWidget = true;
			}

			if (ImGui::MenuItem(get_localized_string(LANG_1079).c_str(), NULL, false, map && !map->is_mdl_model))
			{
				map->ImportLightFile(map->bsp_path);
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0546).c_str());
				ImGui::EndTooltip();
			}

			/*
				if (ImGui::MenuItem("Create .BSP from .JMF"))
				{
					// import all brushes
					// generate nodes
					// ... ?
				}
			*/

			if (ImGui::MenuItem(get_localized_string(LANG_1080).c_str(), NULL, false, map && !map->is_mdl_model))
			{
				if (map)
				{
					std::string entFilePath;
					if (g_settings.same_dir_for_ent)
					{
						std::string bspFilePath = map->bsp_path;
						if (bspFilePath.size() < 4 || bspFilePath.rfind(".bsp") != bspFilePath.size() - 4)
						{
							entFilePath = bspFilePath + ".ent";
						}
						else
						{
							entFilePath = bspFilePath.substr(0, bspFilePath.size() - 4) + ".ent";
						}
					}
					else
					{
						entFilePath = g_working_dir + "exported_entities/" + (map->bsp_name + ".ent");
					}

					if (fileExists(entFilePath))
					{
						std::vector<unsigned char> entDat;
						if (readFile(entFilePath, entDat))
						{
							map->replace_lump(LUMP_ENTITIES, entDat.data(), entDat.size());
							print_log(get_localized_string(LANG_1052), entFilePath);
						}
						map->reload_ents();
						g_app->updateEnts();
						app->reloading = true;
						for (size_t i = 0; i < mapRenderers.size(); i++)
						{
							BspRenderer* mapRender = mapRenderers[i];
							mapRender->reload();
						}
						app->reloading = false;
						g_app->reloadBspModels();
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0348));
					}
				}
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0547).c_str(), NULL, false, map && !map->is_mdl_model))
			{
				if (map)
				{
					ifd::FileDialog::Instance().Open("WadOpenDialog", "Open .wad", "Wad file (*.wad){.wad},.*", false, g_settings.lastdir);
				}

				if (map && ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(fmt::format(fmt::runtime(get_localized_string(LANG_0349)), g_working_dir + "exported_wads/", map->bsp_name + ".wad").c_str());
					ImGui::EndTooltip();
				}
			}

			if (ImGui::BeginMenu("Embedded##import", map && !map->is_mdl_model))
			{
				if (ImGui::MenuItem(get_localized_string(LANG_0549).c_str(), 0, ditheringEnabled))
					ditheringEnabled = !ditheringEnabled;

				if (ImGui::MenuItem("From .png files"))
				{
					if (map)
					{
						ifd::FileDialog::Instance().Open("PngDirOpenDialog", "Open .png dir", std::string(), false, g_settings.lastdir);
					}

					if (map && ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(fmt::format(fmt::runtime(get_localized_string(LANG_0349)), g_working_dir + "exported_wads/", map->bsp_name + ".wad").c_str());
						ImGui::EndTooltip();
					}
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(get_localized_string(LANG_0548).c_str(), map && !map->is_mdl_model))
			{
				if (ImGui::MenuItem(get_localized_string(LANG_0549).c_str(), 0, ditheringEnabled))
					ditheringEnabled = !ditheringEnabled;

				std::string hash = "##1";
				for (auto& wad : rend->wads)
				{
					if (wad->dirEntries.size() == 0)
						continue;
					hash += "1";
					if (ImGui::MenuItem((basename(wad->filename) + hash).c_str()))
					{
						print_log(get_localized_string(LANG_0350), basename(wad->filename));
						std::string importPath = g_working_dir + "exported_wads/pngs/" + basename(wad->filename);
						if (!map->import_textures_to_wad(wad->filename, importPath, ditheringEnabled))
						{
							//
						}
					}
				}
				ImGui::EndMenu();
			}

			ImGui::EndMenu();
		}

		if (map && dirExists(g_game_dir + "svencoop_addon/maps/"))
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0550).c_str()))
			{
				std::string mapPath = g_game_dir + "svencoop_addon/maps/" + map->bsp_name + ".bsp";
				std::string entPath = g_game_dir + "svencoop_addon/scripts/maps/bspguy/maps/" + map->bsp_name + ".ent";

				map->update_ent_lump(true); // strip nodes before writing (to skip slow node graph generation)
				map->write(mapPath);
				map->update_ent_lump(false); // add the nodes back in for conditional loading in the ent file

				if (map->export_entities(entPath))
				{
					print_log(get_localized_string(LANG_1053), entPath);
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0356), entPath);
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0357));
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0551).c_str());
				ImGui::EndTooltip();
			}
		}

		if (ImGui::MenuItem(get_localized_string(LANG_0825).c_str(), NULL, showMergeMapWidget, !app->isLoading))
		{
			showMergeMapWidget = !showMergeMapWidget;
		}
		IMGUI_TOOLTIP(g, "Open multi-map merger tool window to combine, stack, or align multiple BSP maps");
		if (ImGui::BeginMenu("Recent Files", g_settings.lastOpened.size()))
		{
			for (auto& file : g_settings.lastOpened)
			{
				std::string smallPath = file;
				if (smallPath.length() > 61)
				{
					smallPath = smallPath.substr(0, 18) + "..." + smallPath.substr(smallPath.length() - 42);
				}
				if (ImGui::MenuItem(smallPath.c_str(), NULL, false, fileExists(file)))
				{
					OpenFile(file);
				}
			}

			ImGui::EndMenu();
		}

		if (ImGui::MenuItem(get_localized_string(LANG_0552).c_str(), 0, false, map && !map->is_mdl_model && !app->isLoading))
		{
			app->reloadMaps();
			map = NULL;
			rend = NULL;
		}
		if (ImGui::MenuItem(get_localized_string(LANG_0553).c_str(), 0, false, map && !map->is_mdl_model && !app->isLoading))
		{
			if (map)
			{
				print_log(get_localized_string(LANG_0358), map->bsp_name);
				if (!map->validate())
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
				}
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem(get_localized_string(LANG_0554).c_str(), 0, false, !app->isLoading))
		{
			if (!showSettingsWidget)
			{
				reloadSettings = true;
			}
			showSettingsWidget = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem(get_localized_string(LANG_0555).c_str(), NULL))
		{
			g_app->is_closing = true;
		}
		ImGui::EndMenu();
	}
}

void Gui::drawMenu_Edit()
{
	ImGuiContext& g = *GImGui;
	Bsp* map = app->getSelectedMap();
	BspRenderer* rend = map ? map->getBspRender() : NULL;

	if (ImGui::BeginMenu(get_localized_string(LANG_0556).c_str(), (map && !map->is_mdl_model)))
	{
		EditBspCommand* undoCmd = !rend->undoHistory.empty() ? rend->undoHistory[rend->undoHistory.size() - 1] : NULL;
		EditBspCommand* redoCmd = !rend->redoHistory.empty() ? rend->redoHistory[rend->redoHistory.size() - 1] : NULL;
		std::string undoTitle = undoCmd ? "Undo " + undoCmd->desc : "Can't undo";
		std::string redoTitle = redoCmd ? "Redo " + redoCmd->desc : "Can't redo";
		bool canUndo = undoCmd && (!app->isLoading);
		bool canRedo = redoCmd && (!app->isLoading);
		bool entSelected = app->pickInfo.selectedEnts.size();
		bool nonWorldspawnEntSelected = entSelected;

		if (nonWorldspawnEntSelected)
		{
			for (auto& ent : app->pickInfo.selectedEnts)
			{
				if (map->ents[ent]->isWorldSpawn())
				{
					nonWorldspawnEntSelected = false;
					break;
				}
			}
		}

		if (ImGui::MenuItem(undoTitle.c_str(), get_localized_string(LANG_0557).c_str(), false, canUndo))
		{
			rend->undo();
		}
		IMGUI_TOOLTIP(g, "Revert the last geometry, entity, or texture modification");

		if (ImGui::MenuItem(redoTitle.c_str(), get_localized_string(LANG_0558).c_str(), false, canRedo))
		{
			rend->redo();
		}
		IMGUI_TOOLTIP(g, "Reapply the previously undone operation");

		ImGui::Separator();

		if (ImGui::MenuItem(get_localized_string(LANG_1081).c_str(), get_localized_string(LANG_1082).c_str(), false, nonWorldspawnEntSelected && app->pickInfo.selectedEnts.size()))
		{
			app->cutEnt();
		}
		IMGUI_TOOLTIP(g, "Cut selected entity to the internal clipboard");

		if (ImGui::MenuItem(get_localized_string(LANG_1083).c_str(), get_localized_string(LANG_1084).c_str(), false, app->pickInfo.selectedFaces.size() || (nonWorldspawnEntSelected && app->pickInfo.selectedEnts.size())))
		{
			if (app->pickInfo.selectedEnts.size())
				app->copyEnt();
			if (app->pickInfo.selectedFaces.size())
				copyTexture();
		}
		IMGUI_TOOLTIP(g, "Copy selected entity or face texture attributes to the clipboard");

		if (ImGui::BeginMenu((get_localized_string(LANG_0449) + "###BeginPaste2").c_str()))
		{
			if (ImGui::MenuItem((get_localized_string(LANG_0449) + "###BEG2_PASTE1").c_str(), get_localized_string(LANG_0441).c_str(), false))
			{
				app->pasteEnt(false);
			}
			IMGUI_TOOLTIP(g, "Paste entity from clipboard in front of the 3D camera");

			if (ImGui::MenuItem((get_localized_string(LANG_0450) + "###BEG2_OPASTE1").c_str(), 0, false))
			{
				app->pasteEnt(true);
			}
			IMGUI_TOOLTIP(g, "Paste clipboard entity preserving its original coordinates");

			if (ImGui::MenuItem("Paste with bspmodel###BEG2_PASTE2", get_localized_string(LANG_0441).c_str(), false))
			{
				app->pasteEnt(false, true);
			}
			IMGUI_TOOLTIP(g, "Paste entity while duplicating and attaching its underlying BSP model data");

			if (ImGui::MenuItem("Paste at Selected Entity Origin", 0, false, map && app->hasCopiedEnt() && app->pickInfo.selectedEnts.size() > 0))
			{
				vec3 pivot = vec3();
				for (int i : app->pickInfo.selectedEnts)
				{
					pivot += map->getEntOrigin(map->ents[i]);
				}
				pivot /= (float)app->pickInfo.selectedEnts.size();
				app->pasteEntAtOrigin(pivot);
			}
			IMGUI_TOOLTIP(g, "Paste clipboard entity directly at the pivot origin of current selection");

			ImGui::EndMenu();
		}
		if (ImGui::MenuItem(get_localized_string(LANG_1085).c_str(), get_localized_string(LANG_1086).c_str(), false, nonWorldspawnEntSelected))
		{
			app->deleteEnts();
		}
		IMGUI_TOOLTIP(g, "Delete selected entity or picked face polygons from the map");

		if (ImGui::BeginMenu("Select"))
		{
			if (ImGui::MenuItem("Select All", "Ctrl+A"))
			{
				if (app->pickMode == PICK_OBJECT)
				{
					app->pickInfo.selectedEnts.clear();
					for (size_t i = 1; i < map->ents.size(); i++)
					{
						app->pickInfo.AddSelectedEnt((int)i);
					}
				}
			}
			IMGUI_TOOLTIP(g, "Select all non-worldspawn entities in the active map");

			if (ImGui::MenuItem("Deselect All", "Esc"))
			{
				app->deselectFaces();
				app->deselectObject();
			}
			IMGUI_TOOLTIP(g, "Clear all active entity and face selections");

			if (ImGui::MenuItem("Faces with Same Texture", "Ctrl+Alt+A", false, map && app->pickInfo.selectedFaces.size() > 0))
			{
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
			}
			IMGUI_TOOLTIP(g, "Select all map faces sharing the active face texture");

			ImGui::EndMenu();
		}

		ImGui::Separator();

		bool allowDuplicate = app->pickInfo.selectedEnts.size() > 0;

		if (ImGui::MenuItem(get_localized_string("LANG_DUPLICATE_BSP").c_str(), "Ctrl+D", false, !app->isLoading && allowDuplicate))
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

		if (ImGui::MenuItem("Merge Selected Models (WIP)", 0, false, !app->isLoading && IsValidForMerge))
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
			map->remove_unused_model_structures();

			g_app->pickInfo.selectedEnts.clear();
			rend->loadLightmaps();
			rend->pushUndoState("MERGE BSP ENTITIES", EDIT_MODEL_LUMPS | FL_ENTITIES);
			rend->preRenderEnts();
		}
		IMGUI_TOOLTIP(g, "Merge multiple selected brush models into a single combined BSP model");

		if (ImGui::MenuItem("Split Face", "F", false, !app->isLoading && app->pickMode != PICK_OBJECT))
		{
			app->splitModelFace();
		}
		IMGUI_TOOLTIP(g, "Split the selected polygon face along picked edge vertex points");

		ImGui::Separator();

		if (ImGui::MenuItem(app->movingEnt ? "Ungrab" : "Grab", get_localized_string(LANG_1088).c_str(), false, nonWorldspawnEntSelected))
		{
			if (!app->movingEnt)
				app->grabEnt();
			else
			{
				app->ungrabEnt();
			}
		}
		IMGUI_TOOLTIP(g, "Toggle interactive mouse movement of selected entity in 3D space");

		if (ImGui::MenuItem(get_localized_string(LANG_1089).c_str(), get_localized_string(LANG_1090).c_str(), false, entSelected))
		{
			showTransformWidget = !showTransformWidget;
		}
		IMGUI_TOOLTIP(g, "Open numeric translation, rotation, and scaling transform widget");

		ImGui::Separator();

		if (ImGui::MenuItem(get_localized_string(LANG_1091).c_str(), "Ctrl+G / Alt+Enter", false, entSelected))
		{
			showKeyvalueWidget = !showKeyvalueWidget;
		}
		IMGUI_TOOLTIP(g, "Open SmartEdit Entity Keyvalues and Spawnflags inspector");

		ImGui::EndMenu();
	}
}

void Gui::drawMenu_View()
{
	ImGuiContext& g = *GImGui;
	Bsp* map = app->getSelectedMap();
	BspRenderer* rend = map ? map->getBspRender() : NULL;

	if (ImGui::BeginMenu(get_localized_string(LANG_MENU_VIEW).c_str()))
	{
		if (ImGui::MenuItem("Command Palette...", "Ctrl+K"))
		{
			GuiCommandPalette::getInstance().open();
		}
		IMGUI_TOOLTIP(g, "Search and execute any editor command or action");
		ImGui::Separator();

		if (ImGui::BeginMenu(get_localized_string(LANG_0566).c_str(), map))
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0567).c_str(), NULL, app->clipnodeRenderHull == -1))
				app->clipnodeRenderHull = -1;
			IMGUI_TOOLTIP(g, "Automatically choose best collision hull display based on entity type");

			if (ImGui::MenuItem(get_localized_string(LANG_0568).c_str(), NULL, app->clipnodeRenderHull == 0))
				app->clipnodeRenderHull = 0;
			IMGUI_TOOLTIP(g, "Display Hull 0 point/raycast collision wireframe");

			if (ImGui::MenuItem(get_localized_string(LANG_0569).c_str(), NULL, app->clipnodeRenderHull == 1))
				app->clipnodeRenderHull = 1;
			IMGUI_TOOLTIP(g, "Display Hull 1 standard standing player collision box wireframe (32x32x72)");

			if (ImGui::MenuItem(get_localized_string(LANG_0570).c_str(), NULL, app->clipnodeRenderHull == 2))
				app->clipnodeRenderHull = 2;
			IMGUI_TOOLTIP(g, "Display Hull 2 large monster/vehicle collision box wireframe (64x64x72)");

			if (ImGui::MenuItem(get_localized_string(LANG_0571).c_str(), NULL, app->clipnodeRenderHull == 3))
				app->clipnodeRenderHull = 3;
			IMGUI_TOOLTIP(g, "Display Hull 3 ducking player collision box wireframe (32x32x36)");

			ImGui::EndMenu();
		}

		if (ImGui::MenuItem(get_localized_string(LANG_0559).c_str(), get_localized_string(LANG_0560).c_str(), false, map != NULL))
		{
			map->hideEnts(false);
			if (rend)
				rend->preRenderEnts();
			app->updateEntConnections();
			pickCount++;
		}
		IMGUI_TOOLTIP(g, "Restore visibility to all temporarily hidden entities and geometry");

		ImGui::Separator();

		if (ImGui::MenuItem(get_localized_string(LANG_1095).c_str(), "Ctrl+G", &showGOTOWidget))
		{
			showGOTOWidget_update = true;
		}
		IMGUI_TOOLTIP(g, "Teleport the 3D camera to specific coordinates or jump directly to an entity index");

		if (ImGui::MenuItem(get_localized_string(LANG_0563).c_str(), "F2", &showLimitsWidget))
		{
		}
		IMGUI_TOOLTIP(g, "Toggle engine lump and memory usage limit gauges");

		bool skyboxEnabled = (g_render_flags & RENDER_SKYBOX) != 0;
		if (ImGui::MenuItem("Skybox", "Ctrl+Alt+S", skyboxEnabled))
		{
			g_render_flags ^= RENDER_SKYBOX;
		}
		IMGUI_TOOLTIP(g, "Toggle 3D environment skybox cubemap background in the viewport");

		if (ImGui::MenuItem("Take Viewport Screenshot", "F12"))
		{
			app->save_viewport_screenshot();
		}
		IMGUI_TOOLTIP(g, "Capture full-resolution viewport image and save to screenshots folder");

		ImGui::EndMenu();
	}
}

void Gui::drawMenu_Map()
{
	ImGuiContext& g = *GImGui;
	Bsp* map = app->getSelectedMap();
	BspRenderer* rend = map ? map->getBspRender() : NULL;

	static bool ScaleOnlySelected = false;
	static float scale_val = 1.0f;

	if (ImGui::BeginMenu(get_localized_string(LANG_0561).c_str(), (map && !map->is_mdl_model)))
	{
		if (ImGui::MenuItem(get_localized_string(LANG_0562).c_str(), "F3"))
		{
			showEntityReport = true;
		}
		IMGUI_TOOLTIP(g, "Open searchable table of all map entities and classnames");

		if (ImGui::MenuItem(get_localized_string(LANG_0563).c_str(), "F2"))
		{
			showLimitsWidget = true;
		}
		IMGUI_TOOLTIP(g, "Open engine lump memory gauges and limit statistics");

		ImGui::Separator();

		if (ImGui::MenuItem(get_localized_string(LANG_0564).c_str(), 0, false, !app->isLoading && map))
		{
			print_log(get_localized_string(LANG_0296), map->bsp_name);
			map->remove_unused_model_structures().print_delete_stats(1); // buffer overflow?
			map->validate();
			rend->pushUndoState("Clean " + map->bsp_name, EDIT_MODEL_LUMPS);
		}
		IMGUI_TOOLTIP(g, "Remove unused clipnodes, orphaned model structures, and empty leaves");

		if (ImGui::MenuItem(get_localized_string(LANG_0565).c_str(), 0, false, !app->isLoading && map))
		{
			map->update_ent_lump();

			print_log(get_localized_string(LANG_0297), map->bsp_name);
			if (!map->has_hull2_ents())
			{
				print_log(get_localized_string(LANG_0298));
				map->delete_hull(2, 1);
			}

			bool oldVerbose = g_settings.verboseLogs;
			g_settings.verboseLogs = true;
			auto removestats = map->delete_unused_hulls(true);

			removestats.print_delete_stats(1);
			g_settings.verboseLogs = oldVerbose;

			map->validate();

			rend->pushUndoState("Optimize " + map->bsp_name, EDIT_MODEL_LUMPS | FL_ENTITIES);
		}
		IMGUI_TOOLTIP(g, "Compact vertex tables, merge collinear edges, and sort marksurfaces");

		if (ImGui::MenuItem(get_localized_string(LANG_0825).c_str(), NULL, showMergeMapWidget, !app->isLoading))
		{
			showMergeMapWidget = !showMergeMapWidget;
		}
		IMGUI_TOOLTIP(g, "Open multi-map merger tool window to combine, stack, or align multiple BSP maps");

		if (ImGui::BeginMenu("MAP TRANSFORMATION [WIP]", map))
		{
			if (ImGui::MenuItem("Shift / Move Map (X, Y, Z)...", NULL, showShiftMapDialog, map && !map->is_mdl_model))
			{
				showShiftMapDialog = !showShiftMapDialog;
			}
			IMGUI_TOOLTIP(g, "Translate entire map geometry, models, and entities by specified delta offsets");
			ImGui::Separator();

			if (ImGui::MenuItem("Mirror map x/y", NULL, false, map))
			{
				for (int i = 0; i < map->vertCount; i++)
				{
					std::swap(map->verts[i].x, map->verts[i].y);
				}

				for (int i = 0; i < map->faceCount; i++)
				{
					int* start = &map->surfedges[map->faces[i].iFirstEdge];
					int* end = &map->surfedges[map->faces[i].iFirstEdge + map->faces[i].nEdges];
					std::reverse(start, end);
				}

				for (int i = 0; i < map->planeCount; i++)
				{
					std::swap(map->planes[i].vNormal.x, map->planes[i].vNormal.y);
					map->planes[i].update_plane(false);
				}

				for (int i = 0; i < map->texinfoCount; i++)
				{
					std::swap(map->texinfos[i].vS.x, map->texinfos[i].vS.y);
					std::swap(map->texinfos[i].vT.x, map->texinfos[i].vT.y);
				}

				for (size_t i = 0; i < map->ents.size(); i++)
				{
					Entity* mapEnt = map->ents[i];
					if (!mapEnt->origin.IsZero())
					{
						std::swap(mapEnt->origin.x, mapEnt->origin.y);
						mapEnt->setOrAddKeyvalue("origin", mapEnt->origin.toKeyvalueString());
					}

					if (mapEnt->isBspModel())
					{
						continue;
					}

					if (mapEnt->hasKey("angle"))
					{
						float angle = str_to_float(mapEnt->keyvalues["angle"]);
						angle = 90.0f - angle;
						mapEnt->setOrAddKeyvalue("angle", std::to_string(fullnormalizeangle(angle)));
					}

					if (mapEnt->hasKey("angles"))
					{
						vec3 angles = parseVector(mapEnt->keyvalues["angles"]);
						angles[1] = 90.0f - angles[1];
						mapEnt->setOrAddKeyvalue("angles", angles.normalize_angles().toKeyvalueString());
					}
					else if (!mapEnt->hasKey("angle"))
					{
						vec3 angles = vec3();
						angles[1] = 90.0f - angles[1];
						mapEnt->setOrAddKeyvalue("angles", angles.normalize_angles().toKeyvalueString());
					}
				}

				for (int i = 0; i < map->leafCount; i++)
				{
					std::swap(map->leaves[i].nMins.x, map->leaves[i].nMins.y);
					std::swap(map->leaves[i].nMaxs.x, map->leaves[i].nMaxs.y);
				}

				for (int i = 0; i < map->modelCount; i++)
				{
					std::swap(map->models[i].nMins.x, map->models[i].nMins.y);
					std::swap(map->models[i].nMaxs.x, map->models[i].nMaxs.y);
				}

				for (int i = 0; i < map->nodeCount; i++)
				{
					std::swap(map->nodes[i].nMins.x, map->nodes[i].nMins.y);
					std::swap(map->nodes[i].nMaxs.x, map->nodes[i].nMaxs.y);
				}

				map->update_ent_lump();
				app->reloading = true;
				rend->reload();
				app->reloading = false;
			}
			IMGUI_TOOLTIP(g, "Mirror entire map geometry along selected axis and invert face normals");

			if (ImGui::MenuItem("Rotate Counter Clockwise 90", NULL, false, map))
			{
				for (int i = 0; i < map->vertCount; i++)
				{
					std::swap(map->verts[i].x, map->verts[i].y);
					map->verts[i].x *= -1;
				}

				std::set<int> flipped;

				for (int i = 0; i < map->planeCount; i++)
				{
					std::swap(map->planes[i].vNormal.x, map->planes[i].vNormal.y);
					map->planes[i].vNormal.x *= -1;

					bool flip = map->planes[i].update_plane(true);

					if (flip)
					{
						flipped.insert(i);
					}
				}

				for (int i = 0; i < map->faceCount; i++)
				{
					if (flipped.count(map->faces[i].iPlane))
						map->faces[i].nPlaneSide = map->faces[i].nPlaneSide ? 0 : 1;
				}

				for (int i = 0; i < map->texinfoCount; i++)
				{
					std::swap(map->texinfos[i].vS.x, map->texinfos[i].vS.y);
					std::swap(map->texinfos[i].vT.x, map->texinfos[i].vT.y);

					map->texinfos[i].vS.x *= -1;
					map->texinfos[i].vT.x *= -1;
				}

				for (size_t i = 0; i < map->ents.size(); i++)
				{
					if (map->ents[i]->hasKey("origin"))
					{
						map->ents[i]->origin = parseVector(map->ents[i]->keyvalues["origin"]);

						std::swap(map->ents[i]->origin.x, map->ents[i]->origin.y);
						map->ents[i]->origin.x *= -1;

						map->ents[i]->setOrAddKeyvalue("origin", map->ents[i]->origin.toKeyvalueString());
					}

					if (map->ents[i]->isBspModel())
					{
						continue;
					}

					if (map->ents[i]->hasKey("angle"))
					{
						float angle = str_to_float(map->ents[i]->keyvalues["angle"]);
						angle += 90.0f;

						map->ents[i]->setOrAddKeyvalue("angle", std::to_string(fullnormalizeangle(angle)));
					}

					if (map->ents[i]->hasKey("angles"))
					{
						vec3 angles = parseVector(map->ents[i]->keyvalues["angles"]);
						angles[1] += 90.0f;

						map->ents[i]->setOrAddKeyvalue("angles", angles.normalize_angles().toKeyvalueString());
					}
					else if (!map->ents[i]->hasKey("angle"))
					{
						vec3 angles = vec3();
						angles[1] += 90.0f;
						map->ents[i]->setOrAddKeyvalue("angles", angles.normalize_angles().toKeyvalueString());
					}
				}

				for (int i = 0; i < map->leafCount; i++)
				{
					std::swap(map->leaves[i].nMins.y, map->leaves[i].nMaxs.y);

					std::swap(map->leaves[i].nMins.x, map->leaves[i].nMins.y);
					map->leaves[i].nMins.x *= -1;
					std::swap(map->leaves[i].nMaxs.x, map->leaves[i].nMaxs.y);
					map->leaves[i].nMaxs.x *= -1;
				}

				for (int i = 0; i < map->modelCount; i++)
				{
					std::swap(map->models[i].nMins.y, map->models[i].nMaxs.y);

					std::swap(map->models[i].nMins.x, map->models[i].nMins.y);
					map->models[i].nMins.x *= -1;
					std::swap(map->models[i].nMaxs.x, map->models[i].nMaxs.y);
					map->models[i].nMaxs.x *= -1;
				}

				for (int i = 0; i < map->nodeCount; i++)
				{
					std::swap(map->nodes[i].nMins.y, map->nodes[i].nMaxs.y);

					std::swap(map->nodes[i].nMins.x, map->nodes[i].nMins.y);
					map->nodes[i].nMins.x *= -1;
					std::swap(map->nodes[i].nMaxs.x, map->nodes[i].nMaxs.y);
					map->nodes[i].nMaxs.x *= -1;

					if (flipped.count(map->nodes[i].iPlane))
					{
						std::swap(map->nodes[i].iChildren[0], map->nodes[i].iChildren[1]);
					}
				}

				for (int i = 0; i < map->clipnodeCount; i++)
				{
					if (flipped.count(map->clipnodes[i].iPlane))
					{
						std::swap(map->clipnodes[i].iChildren[0], map->clipnodes[i].iChildren[1]);
					}
				}

				map->update_ent_lump();
				app->reloading = true;
				rend->reload();
				app->reloading = false;
			}
			IMGUI_TOOLTIP(g, "Rotate world geometry and entities 90 degrees counter-clockwise");

			if (ImGui::MenuItem("Rotate Clockwise 90", NULL, false, map))
			{
				for (int i = 0; i < map->vertCount; i++)
				{
					std::swap(map->verts[i].x, map->verts[i].y);
					map->verts[i].y *= -1;
				}

				std::set<int> flipped;

				for (int i = 0; i < map->planeCount; i++)
				{
					std::swap(map->planes[i].vNormal.x, map->planes[i].vNormal.y);
					map->planes[i].vNormal.y *= -1;

					bool flip = map->planes[i].update_plane(true);

					if (flip)
					{
						flipped.insert(i);
					}
				}

				for (int i = 0; i < map->faceCount; i++)
				{
					if (flipped.count(map->faces[i].iPlane))
						map->faces[i].nPlaneSide = map->faces[i].nPlaneSide ? 0 : 1;
				}

				for (int i = 0; i < map->texinfoCount; i++)
				{
					std::swap(map->texinfos[i].vS.x, map->texinfos[i].vS.y);
					std::swap(map->texinfos[i].vT.x, map->texinfos[i].vT.y);

					map->texinfos[i].vS.y *= -1;
					map->texinfos[i].vT.y *= -1;
				}

				for (size_t i = 0; i < map->ents.size(); i++)
				{
					if (map->ents[i]->hasKey("origin"))
					{
						map->ents[i]->origin = parseVector(map->ents[i]->keyvalues["origin"]);

						std::swap(map->ents[i]->origin.x, map->ents[i]->origin.y);
						map->ents[i]->origin.y *= -1;

						map->ents[i]->setOrAddKeyvalue("origin", map->ents[i]->origin.toKeyvalueString());
					}

					if (map->ents[i]->isBspModel())
					{
						continue;
					}

					if (map->ents[i]->hasKey("angle"))
					{
						float angle = str_to_float(map->ents[i]->keyvalues["angle"]);
						angle -= 90.0f;

						map->ents[i]->setOrAddKeyvalue("angle", std::to_string(fullnormalizeangle(angle)));
					}

					if (map->ents[i]->hasKey("angles"))
					{
						vec3 angles = parseVector(map->ents[i]->keyvalues["angles"]);
						angles[1] -= 90.0f;

						map->ents[i]->setOrAddKeyvalue("angles", angles.normalize_angles().toKeyvalueString());
					}
					else if (!map->ents[i]->hasKey("angle"))
					{
						vec3 angles = vec3();
						angles[1] -= 90.0f;
						map->ents[i]->setOrAddKeyvalue("angles", angles.normalize_angles().toKeyvalueString());
					}
				}

				for (int i = 0; i < map->leafCount; i++)
				{
					std::swap(map->leaves[i].nMins.x, map->leaves[i].nMaxs.x);

					std::swap(map->leaves[i].nMins.x, map->leaves[i].nMins.y);
					map->leaves[i].nMins.y *= -1;
					std::swap(map->leaves[i].nMaxs.x, map->leaves[i].nMaxs.y);
					map->leaves[i].nMaxs.y *= -1;
				}

				for (int i = 0; i < map->modelCount; i++)
				{
					std::swap(map->models[i].nMins.x, map->models[i].nMaxs.x);

					std::swap(map->models[i].nMins.x, map->models[i].nMins.y);
					map->models[i].nMins.y *= -1;
					std::swap(map->models[i].nMaxs.x, map->models[i].nMaxs.y);
					map->models[i].nMaxs.y *= -1;
				}

				for (int i = 0; i < map->nodeCount; i++)
				{
					std::swap(map->nodes[i].nMins.x, map->nodes[i].nMaxs.x);

					std::swap(map->nodes[i].nMins.x, map->nodes[i].nMins.y);
					map->nodes[i].nMins.y *= -1;
					std::swap(map->nodes[i].nMaxs.x, map->nodes[i].nMaxs.y);
					map->nodes[i].nMaxs.y *= -1;

					if (flipped.count(map->nodes[i].iPlane))
					{
						std::swap(map->nodes[i].iChildren[0], map->nodes[i].iChildren[1]);
					}
				}

				for (int i = 0; i < map->clipnodeCount; i++)
				{
					if (flipped.count(map->clipnodes[i].iPlane))
					{
						std::swap(map->clipnodes[i].iChildren[0], map->clipnodes[i].iChildren[1]);
					}
				}

				map->update_ent_lump();
				app->reloading = true;
				rend->reload();
				app->reloading = false;
			}
			IMGUI_TOOLTIP(g, "Rotate world geometry and entities 90 degrees clockwise");

			if (ImGui::BeginMenu("Scale map", map))
			{
				static bool ScaleOnlySelected = false;

				if (ImGui::MenuItem("Scale selected", NULL, &ScaleOnlySelected))
				{
					// ScaleOnlySelected = !ScaleOnlySelected;
				}
				IMGUI_TOOLTIP(g, "Apply scale factor only to selected entities/geometry rather than the entire map");

				for (float scale_val = 0.25f; scale_val <= 2.0f; scale_val += 0.25f)
				{
					if (std::fabs(scale_val - 1.0f) > EPSILON && ImGui::MenuItem(fmt::format("Scale {:2}X", scale_val).c_str()))
					{
						if (ScaleOnlySelected)
						{
							STRUCTUSAGE modelUsage = STRUCTUSAGE(map);
							std::set<int> models;

							for (auto s : app->pickInfo.selectedEnts)
							{
								int modelIdx = map->ents[s]->getBspModelIdx();
								if (modelIdx >= 0)
								{
									models.insert(modelIdx);
									map->mark_model_structures(modelIdx, &modelUsage, true);
								}
							}

							for (int i = 0; i < map->modelCount; i++)
							{
								if (models.count(i))
								{
									map->models[i].nMaxs *= scale_val;
									map->models[i].nMins *= scale_val;

									vec3 neworigin = map->models[i].vOrigin * scale_val;
									map->models[i].vOrigin = neworigin;
								}
							}
							for (int i = 0; i < map->vertCount; i++)
							{
								if (modelUsage.verts[i])
								{
									map->verts[i] *= scale_val;
								}
							}
							for (int i = 0; i < map->texinfoCount; i++)
							{
								if (modelUsage.texInfo[i])
								{
									map->texinfos[i].vS /= scale_val;
									map->texinfos[i].vT /= scale_val;
								}
							}
							for (int i = 0; i < (int)map->ents.size(); i++)
							{
								if (app->pickInfo.IsSelectedEnt(i))
								{
									vec3 neworigin = map->ents[i]->origin * scale_val;
									neworigin.z += std::fabs(neworigin.z - map->ents[i]->origin.z) * scale_val;
									map->ents[i]->setOrAddKeyvalue("origin", neworigin.toKeyvalueString());
								}
							}
							for (int i = 0; i < map->nodeCount; i++)
							{
								if (modelUsage.nodes[i])
								{
									map->nodes[i].nMaxs *= scale_val;
									map->nodes[i].nMins *= scale_val;
								}
							}
							for (int i = 0; i < map->leafCount; i++)
							{
								if (modelUsage.leaves[i])
								{
									map->leaves[i].nMaxs *= scale_val;
									map->leaves[i].nMins *= scale_val;
								}
							}
							for (int i = 0; i < map->planeCount; i++)
							{
								if (modelUsage.planes[i])
								{
									map->planes[i].fDist *= scale_val;
								}
							}
						}
						else
						{
							for (int i = 0; i < map->modelCount; i++)
							{
								map->models[i].nMaxs *= scale_val;
								map->models[i].nMins *= scale_val;

								vec3 neworigin = map->models[i].vOrigin * scale_val;
								map->models[i].vOrigin = neworigin;
							}
							for (int i = 0; i < map->vertCount; i++)
							{
								map->verts[i] *= scale_val;
							}
							for (int i = 0; i < map->texinfoCount; i++)
							{
								map->texinfos[i].vS /= scale_val;
								map->texinfos[i].vT /= scale_val;
							}
							for (size_t i = 0; i < map->ents.size(); i++)
							{
								vec3 neworigin = map->ents[i]->origin * scale_val;
								neworigin.z += std::fabs(neworigin.z - map->ents[i]->origin.z) * scale_val;
								map->ents[i]->setOrAddKeyvalue("origin", neworigin.toKeyvalueString());
							}
							for (int i = 0; i < map->nodeCount; i++)
							{
								map->nodes[i].nMaxs *= scale_val;
								map->nodes[i].nMins *= scale_val;
							}
							for (int i = 0; i < map->leafCount; i++)
							{
								map->leaves[i].nMaxs *= scale_val;
								map->leaves[i].nMins *= scale_val;
							}
							for (int i = 0; i < map->planeCount; i++)
							{
								// map->planes[i].update_plane(map->planes[i].vNormal, map->planes[i].fDist *= scale_val);
								map->planes[i].fDist *= scale_val;
							}
						}
						map->resize_all_lightmaps();

						rend->loadLightmaps();
						rend->preRenderEnts();
						rend->reloadClipnodes();

						rend->pushUndoState(fmt::format("MAP SCALE TO {:2}", scale_val), EDIT_MODEL_LUMPS | FL_ENTITIES);
					}
					IMGUI_TOOLTIP(g, "Scale world geometry, planes, and entity coordinates by specified factor");
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}

		bool hasAnyCollision = anyHullValid[1] || anyHullValid[2] || anyHullValid[3];

		if (ImGui::MenuItem("Recompile lighting", NULL, false, map && !map->is_mdl_model && !app->isLoading))
		{
			recompileLighting();
		}
		IMGUI_TOOLTIP(g, "Recalculate lightmaps using external HLRAD compiler with error handling and live reload");

		if (ImGui::MenuItem("Generate nav mesh", NULL, false, (!rend->debugNavMesh || !g_app->debugLeafNavMesh)))
		{
			rend->generateNavMeshBuffer();
			rend->generateLeafNavMeshBuffer();
		}
		IMGUI_TOOLTIP(g, "Generate AI navigation mesh graph from walkable BSP floor leaves for bot pathfinding");

		ImGui::EndMenu();
	}
}

void Gui::drawMenu_Tools()
{
	ImGuiContext& g = *GImGui;
	Bsp* map = app->getSelectedMap();
	BspRenderer* rend = map ? map->getBspRender() : NULL;

	static int generateClipnodes = 1;
	static bool meshToBrush = false;

	bool hasAnyCollision = false;
	if (map)
	{
		for (int i = 0; i < MAX_MAP_HULLS; i++)
		{
			if (anyHullValid[i])
			{
				hasAnyCollision = true;
				break;
			}
		}
	}

	if (ImGui::BeginMenu(get_localized_string(LANG_MENU_TOOLS).c_str()))
	{
		if (ImGui::BeginMenu(get_localized_string(LANG_1093).c_str(), hasAnyCollision && !app->isLoading && map))
		{
			for (int i = 1; i < MAX_MAP_HULLS; i++)
			{
				if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str(), NULL, false, anyHullValid[i]))
				{
					// for (size_t k = 0; k < mapRenderers.size(); k++) {
					//	Bsp* map = mapRenderers[k]->map;
					map->delete_hull(i, -1);
					rend->reloadClipnodes();
					//	mapRenderers[k]->reloadClipnodes();
					print_log(get_localized_string(LANG_0360), i, map->bsp_name);
					//}
					checkValidHulls();
				}
				IMGUI_TOOLTIP(g, "Strip collision hull clipnodes for specified hull index across all models");
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(get_localized_string(LANG_1094).c_str(), hasAnyCollision && !app->isLoading && map))
		{
			for (int i = 1; i < MAX_MAP_HULLS; i++)
			{
				if (ImGui::BeginMenu(("Hull " + std::to_string(i)).c_str()))
				{
					for (int k = 1; k < MAX_MAP_HULLS; k++)
					{
						if (i == k)
							continue;
						if (ImGui::MenuItem(("Hull " + std::to_string(k)).c_str(), "", false, anyHullValid[k]))
						{
							// for (size_t j = 0; j < mapRenderers.size(); j++) {
							//	Bsp* map = mapRenderers[j]->map;
							map->delete_hull(i, k);
							rend->reloadClipnodes();
							//	mapRenderers[j]->reloadClipnodes();
							print_log(get_localized_string(LANG_0361), i, k, map->bsp_name);
							//}
							checkValidHulls();
						}
						IMGUI_TOOLTIP(g, "Redirect physics collision from this hull to the target hull index");
					}
					ImGui::EndMenu();
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(get_localized_string(LANG_1185).c_str(), map))
		{
			int cullCount = 0;
			for (auto& r : mapRenderers)
			{
				for (Entity* ent : r->map->ents)
				{
					if (ent->hasKey("classname") && ent->keyvalues["classname"] == "cull")
					{
						cullCount++;
					}
				}
			}

			if (ImGui::MenuItem(get_localized_string(LANG_1186).c_str(), 0, false, app->getSelectedMap() && cullCount < 2))
			{
				Entity* newEnt = new Entity();
				vec3 origin = (cameraOrigin + app->cameraForward * 100);
				if (app->gridSnappingEnabled)
					origin = app->snapToGrid(origin);
				newEnt->addKeyvalue("origin", origin.toKeyvalueString());
				newEnt->addKeyvalue("classname", "cull");
				map->ents.push_back(newEnt);
				rend->pushUndoState("Cull Entity", FL_ENTITIES);
				app->updateCullBox();
			}
			IMGUI_TOOLTIP(g, get_localized_string(LANG_1187).c_str());

			if (ImGui::MenuItem(get_localized_string(LANG_1203).c_str(), 0, false, !app->isLoading && app->getSelectedMap() && cullCount > 0))
			{
				rend->pushUndoState("Delete Cull Entities", FL_ENTITIES);
				app->deselectObject();
				for (int i = (int)map->ents.size() - 1; i >= 0; i--)
				{
					if (map->ents[i]->hasKey("classname") && map->ents[i]->keyvalues["classname"] == "cull")
					{
						delete map->ents[i];
						map->ents.erase(map->ents.begin() + i);
					}
				}
				app->updateEnts();
				app->updateCullBox();
			}
			IMGUI_TOOLTIP(g, get_localized_string(LANG_1204).c_str());

			ImGui::Separator();

			if (ImGui::MenuItem(get_localized_string(LANG_1188).c_str(), 0, false, !app->isLoading && app->getSelectedMap() && rend))
			{
				if (!g_app->hasCullbox)
				{
					print_log("Create at least 2 entities with \"cull\" as a classname first!\n");
				}
				else
				{
					map->delete_box_data(g_app->cullMins - rend->mapOffset, g_app->cullMaxs - rend->mapOffset);
					rend->pushUndoState("Delete Boxed Data", EDIT_MODEL_LUMPS | FL_ENTITIES);
				}
			}
			IMGUI_TOOLTIP(g, get_localized_string(LANG_1189).c_str());

			if (ImGui::MenuItem(get_localized_string(LANG_1190).c_str(), 0, false, !app->isLoading && app->getSelectedMap() && rend))
			{
				if (!g_app->hasCullbox)
				{
					print_log("Create at least 2 entities with \"cull\" as a classname first!\n");
				}
				else
				{
					app->selectBoxEntities();
				}
			}
			IMGUI_TOOLTIP(g, get_localized_string(LANG_1191).c_str());

			if (ImGui::MenuItem(get_localized_string(LANG_1194).c_str(), 0, false, !app->isLoading && app->getSelectedMap() && rend))
			{
				if (!g_app->hasCullbox)
				{
					print_log("Create at least 2 entities with \"cull\" as a classname first!\n");
				}
				else
				{
					app->selectBoxFaces();
				}
			}
			IMGUI_TOOLTIP(g, get_localized_string(LANG_1195).c_str());

			ImGui::Separator();

			if (ImGui::MenuItem("Create Solid Entity in Box (func_wall)", 0, false, !app->isLoading && app->getSelectedMap() && rend && g_app->hasCullbox))
			{
				vec3 mins = g_app->cullMins - rend->mapOffset;
				vec3 maxs = g_app->cullMaxs - rend->mapOffset;
				vec3 origin = (mins + maxs) * 0.5f;
				vec3 localMins = mins - origin;
				vec3 localMaxs = maxs - origin;

				Entity* newEnt = new Entity();
				newEnt->addKeyvalue("origin", origin.toKeyvalueString());
				newEnt->addKeyvalue("classname", "func_wall");

				int aaatriggerIdx = map->GetTriggerTexture();
				unsigned int dupLumps = FL_MARKSURFACES | FL_EDGES | FL_FACES | FL_NODES | FL_PLANES | FL_CLIPNODES | FL_SURFEDGES | FL_TEXINFO | FL_VERTICES | FL_LIGHTING | FL_MODELS | FL_LEAVES | FL_ENTITIES;
				if (aaatriggerIdx == -1)
				{
					dupLumps |= FL_TEXTURES;
					aaatriggerIdx = map->AddTriggerTexture();
				}

				int modelIdx = map->create_solid(localMins, localMaxs, aaatriggerIdx, false);

				map->resize_all_lightmaps();
				rend->loadLightmaps();
				rend->refreshModel(modelIdx);
				rend->refreshModelClipnodes(modelIdx);
				rend->pushUndoState("Create Solid Entity in Box", dupLumps);
				app->updateEnts();
				print_log(PRINT_GREEN, "Created solid entity (func_wall, model *{}) in cull box\n", modelIdx);
			}
			IMGUI_TOOLTIP(g, "Create a solid brush model entity (func_wall) with collision covering the cull box");

			if (ImGui::MenuItem("Create Monsterclip Entity in Box", 0, false, !app->isLoading && app->getSelectedMap() && rend && g_app->hasCullbox))
			{
				vec3 mins = g_app->cullMins - rend->mapOffset;
				vec3 maxs = g_app->cullMaxs - rend->mapOffset;
				vec3 origin = (mins + maxs) * 0.5f;
				vec3 localMins = mins - origin;
				vec3 localMaxs = maxs - origin;

				Entity* newEnt = new Entity();
				newEnt->addKeyvalue("origin", origin.toKeyvalueString());
				newEnt->addKeyvalue("classname", "func_monsterclip");

				int aaatriggerIdx = map->GetTriggerTexture();
				unsigned int dupLumps = FL_MARKSURFACES | FL_EDGES | FL_FACES | FL_NODES | FL_PLANES | FL_CLIPNODES | FL_SURFEDGES | FL_TEXINFO | FL_VERTICES | FL_LIGHTING | FL_MODELS | FL_LEAVES | FL_ENTITIES;
				if (aaatriggerIdx == -1)
				{
					dupLumps |= FL_TEXTURES;
					aaatriggerIdx = map->AddTriggerTexture();
				}

				int modelIdx = map->create_solid(localMins, localMaxs, aaatriggerIdx, false);
				newEnt->addKeyvalue("model", "*" + std::to_string(modelIdx));

				BSPMODEL& model = map->models[modelIdx];
				model.iFirstFace = 0;
				model.nFaces = 0;
				map->remove_unused_model_structures(CLEAN_FACES | CLEAN_MARKSURFACES);
				map->ents.push_back(newEnt);

				map->resize_all_lightmaps();
				rend->refreshModel(modelIdx);
				rend->refreshModelClipnodes(modelIdx);
				rend->pushUndoState("Create Monsterclip Entity in Box", dupLumps);
				app->updateEnts();
				print_log(PRINT_GREEN, "Created monsterclip entity (func_monsterclip, model *{}) in cull box\n", modelIdx);
			}
			IMGUI_TOOLTIP(g, "Create an invisible monsterclip barrier solid brush entity covering the cull box");

			ImGui::Separator();

			if (ImGui::BeginMenu(get_localized_string(LANG_1192).c_str(), !app->isLoading && app->getSelectedMap() && rend && g_app->hasCullbox))
			{
				for (int i = 0; i < MAX_MAP_HULLS; i++)
				{
					if (ImGui::MenuItem((get_localized_string(LANG_0568 + i)).c_str()))
					{
						rend->pushUndoState("Delete Hull In Box", EDIT_MODEL_LUMPS);
						map->delete_hull_in_box(i, g_app->cullMins - rend->mapOffset, g_app->cullMaxs - rend->mapOffset, CONTENTS_EMPTY);
						rend->reload();
						pickCount++;
					}
				}
				ImGui::Separator();
				if (ImGui::MenuItem(get_localized_string(LANG_0458).c_str()))
				{
					rend->pushUndoState("Delete Clipnodes In Box", EDIT_MODEL_LUMPS);
					for (int i = 1; i < MAX_MAP_HULLS; i++)
					{
						map->delete_hull_in_box(i, g_app->cullMins - rend->mapOffset, g_app->cullMaxs - rend->mapOffset, CONTENTS_EMPTY);
					}
					rend->reload();
					pickCount++;
				}
				if (ImGui::MenuItem(get_localized_string(LANG_0460).c_str()))
				{
					rend->pushUndoState("Delete Hulls In Box", EDIT_MODEL_LUMPS);
					for (int i = 0; i < MAX_MAP_HULLS; i++)
					{
						map->delete_hull_in_box(i, g_app->cullMins - rend->mapOffset, g_app->cullMaxs - rend->mapOffset, CONTENTS_EMPTY);
					}
					rend->reload();
					pickCount++;
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(get_localized_string(LANG_1193).c_str(), !app->isLoading && app->getSelectedMap() && rend && g_app->hasCullbox))
			{
				for (int i = 0; i < MAX_MAP_HULLS; i++)
				{
					if (ImGui::MenuItem((get_localized_string(LANG_0568 + i)).c_str()))
					{
						rend->pushUndoState("Create Hull In Box", EDIT_MODEL_LUMPS);
						map->delete_hull_in_box(i, g_app->cullMins - rend->mapOffset, g_app->cullMaxs - rend->mapOffset, CONTENTS_SOLID);
						rend->reload();
						pickCount++;
					}
				}
				ImGui::Separator();
				if (ImGui::MenuItem(get_localized_string(LANG_0458).c_str()))
				{
					rend->pushUndoState("Create Clipnodes In Box", EDIT_MODEL_LUMPS);
					for (int i = 1; i < MAX_MAP_HULLS; i++)
					{
						map->delete_hull_in_box(i, g_app->cullMins - rend->mapOffset, g_app->cullMaxs - rend->mapOffset, CONTENTS_SOLID);
					}
					rend->reload();
					pickCount++;
				}
				if (ImGui::MenuItem(get_localized_string(LANG_0460).c_str()))
				{
					rend->pushUndoState("Create Hulls In Box", EDIT_MODEL_LUMPS);
					for (int i = 0; i < MAX_MAP_HULLS; i++)
					{
						map->delete_hull_in_box(i, g_app->cullMins - rend->mapOffset, g_app->cullMaxs - rend->mapOffset, CONTENTS_SOLID);
					}
					rend->reload();
					pickCount++;
				}
				ImGui::EndMenu();
			}

			ImGui::Separator();

			if (ImGui::BeginMenu(get_localized_string(LANG_1196).c_str(), map))
			{
				if (ImGui::MenuItem(get_localized_string(LANG_1197).c_str()))
				{
					map->remove_faces_by_content(CONTENTS_SKY);

					map->save_undo_lightmaps();
					map->resize_all_lightmaps();

					rend->pushUndoState("REMOVE FACES FROM SKY", EDIT_MODEL_LUMPS);
				}
				IMGUI_TOOLTIP(g, get_localized_string(LANG_1198).c_str());

				if (ImGui::MenuItem(get_localized_string(LANG_1199).c_str()))
				{
					map->remove_faces_by_content(CONTENTS_SOLID);

					map->save_undo_lightmaps();
					map->resize_all_lightmaps();

					rend->pushUndoState("REMOVE FACES FROM SOLID", EDIT_MODEL_LUMPS);
				}
				IMGUI_TOOLTIP(g, get_localized_string(LANG_1200).c_str());

				if (rend->curLeafIdx > 0 && app->clipnodeRenderHull <= 0)
				{
					if (ImGui::MenuItem(fmt::format(fmt::runtime(get_localized_string(LANG_1201)), rend->curLeafIdx).c_str()))
					{
						map->cull_leaf_faces(rend->curLeafIdx);

						map->resize_all_lightmaps();

						rend->loadLightmaps();
						rend->preRenderFaces();

						rend->pushUndoState(fmt::format("REMOVE FACES FROM {} LEAF", rend->curLeafIdx), EDIT_MODEL_LUMPS);
					}
					IMGUI_TOOLTIP(g, get_localized_string(LANG_1202).c_str());
				}
				ImGui::EndMenu();
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(get_localized_string(LANG_0572).c_str(), !app->isLoading && map))
		{
			if (ImGui::MenuItem("Fix Transparent Rendering"))
			{
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
				pickCount++;
			}
			IMGUI_TOOLTIP(g, "Fix black outline and alpha blending artifacts on transparent masked textures");

			if (ImGui::MenuItem("Missing entities classes"))
			{
				for (auto& ent : map->ents)
				{
					if (!app->fgd->getFgdClass(ent->classname))
					{
						print_log(PRINT_RED, "Found missing {} classname! Renamed to info_target\n", ent->classname);
						ent->setOrAddKeyvalue("classname", "info_target");
					}
				}
			}
			IMGUI_TOOLTIP(g, "Replace missing or unprecached entity classnames with fallback info_target point entities");
			if (ImGui::MenuItem(get_localized_string(LANG_0573).c_str()))
			{
				for (int i = 0; i < map->faceCount; i++)
				{
					BSPFACE32& face = map->faces[i];
					BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
					if (info.nFlags & TEX_SPECIAL)
					{
						continue;
					}
					int bmins[2];
					int bmaxs[2];
					if (!map->GetFaceExtents(i, bmins, bmaxs))
					{
						info.nFlags |= TEX_SPECIAL;
					}
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0574).c_str());
				ImGui::EndTooltip();
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0575).c_str()))
			{
				for (int i = 0; i < map->leafCount; i++)
				{
					for (int n = 0; n < 3; n++)
					{
						if (map->leaves[i].nMins[n] > map->leaves[i].nMaxs[n])
						{
							print_log(get_localized_string(LANG_0362), i);
							std::swap(map->leaves[i].nMins[n], map->leaves[i].nMaxs[n]);
						}
					}
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0576).c_str());
				ImGui::EndTooltip();
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0577).c_str()))
			{
				rend->pushUndoState("Fix swapped mins/maxs", FL_MODELS);
				for (int i = 0; i < map->modelCount; i++)
				{
					for (int n = 0; n < 3; n++)
					{
						if (map->models[i].nMins[n] > map->models[i].nMaxs[n])
						{
							print_log(get_localized_string(LANG_0363), i);
							std::swap(map->models[i].nMins[n], map->models[i].nMaxs[n]);
						}
					}
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0578).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0579).c_str()))
			{
				for (int i = 0; i < map->marksurfCount; i++)
				{
					if (map->marksurfs[i] >= map->faceCount)
					{
						map->marksurfs[i] = 0;
					}
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0580).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_1183).c_str()))
			{
				rend->pushUndoState("Fix model face ranges", EDIT_MODEL_LUMPS);
				map->fix_invalid_model_face_ranges();
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_1184).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0581).c_str()))
			{
				std::set<int> used_models; // Protected map
				used_models.insert(0);

				for (auto const& s : map->ents)
				{
					int ent_mdl_id = s->getBspModelIdx();
					if (ent_mdl_id >= 0)
					{
						if (!used_models.count(ent_mdl_id))
						{
							used_models.insert(ent_mdl_id);
						}
					}
				}

				for (int i = 0; i < map->modelCount; i++)
				{
					if (!used_models.count(i))
					{
						Entity* ent = new Entity("func_wall");
						ent->setOrAddKeyvalue("model", "*" + std::to_string(i));
						ent->setOrAddKeyvalue("origin", map->models[i].vOrigin.toKeyvalueString());
						map->ents.push_back(ent);
					}
				}

				map->update_ent_lump();
				if (rend)
				{
					app->reloading = true;
					rend->reload();
					app->reloading = false;
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0582).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem("Fix bad leaf count"))
			{
				int totalLeaves = 1;
				for (int i = 0; i < map->modelCount; i++)
				{
					totalLeaves += map->models[i].nVisLeafs;
				}
				if (totalLeaves > map->leafCount)
				{
					while (totalLeaves > map->leafCount)
						map->create_leaf(CONTENTS_EMPTY);
				}
				else if (totalLeaves < map->leafCount)
				{
					while (totalLeaves < map->leafCount)
					{
						map->models[0].nVisLeafs++;
						totalLeaves++;
					}
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Create empty leafs. ");
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0583).c_str()))
			{
				bool foundfixes = false;
				for (int i = 0; i < map->textureCount; i++)
				{
					int texOffset = ((int*)map->textures)[i + 1];
					if (texOffset >= 0)
					{
						int texlen = map->getBspTextureSize(i);
						int dataOffset = (map->textureCount + 1) * sizeof(int);
						BSPMIPTEX* tex = (BSPMIPTEX*)(map->textures + texOffset);
						if (tex->szName[0] == '\0' || strlen(tex->szName) >= MAXTEXTURENAME)
						{
							print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1055), i);
						}
						if (tex->nOffsets[0] > 0 && dataOffset + texOffset + texlen > map->bsp_header.lump[LUMP_TEXTURES].nLength)
						{
							print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0364), i, map->bsp_header.lump[LUMP_TEXTURES].nLength, dataOffset + texOffset + texlen);

							char* newlump = new char[dataOffset + texOffset + texlen];
							memset(newlump, 0, dataOffset + texOffset + texlen);
							memcpy(newlump, map->textures, map->bsp_header.lump[LUMP_TEXTURES].nLength);
							map->replace_lump(LUMP_TEXTURES, newlump, dataOffset + texOffset + texlen);
							delete[] newlump;
							tex = (BSPMIPTEX*)(map->textures + texOffset);
							foundfixes = true;
						}
						int texdata = (int)(((unsigned char*)tex) - map->textures) + tex->nOffsets[0] + texlen - sizeof(BSPMIPTEX);
						if (texdata > map->bsp_header.lump[LUMP_TEXTURES].nLength)
						{
							print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0364), i, map->bsp_header.lump[LUMP_TEXTURES].nLength, texdata);

							char* newlump = new char[texdata];
							memset(newlump, 0, texdata);
							memcpy(newlump, map->textures, map->bsp_header.lump[LUMP_TEXTURES].nLength);
							map->replace_lump(LUMP_TEXTURES, newlump, texdata);
							delete[] newlump;
							foundfixes = true;
						}
					}
				}
				if (foundfixes)
				{
					map->update_lump_pointers();
				}
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0584).c_str()))
			{
				std::set<std::string> textureset = std::set<std::string>();

				for (int i = 0; i < map->faceCount; i++)
				{
					BSPFACE32& face = map->faces[i];
					BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
					if (info.iMiptex >= 0 && info.iMiptex < map->textureCount)
					{
						int texOffset = ((int*)map->textures)[info.iMiptex + 1];
						if (texOffset >= 0)
						{
							BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
							if (tex.nOffsets[0] <= 0 && tex.szName[0] != '\0')
							{
								if (textureset.count(tex.szName))
									continue;
								textureset.insert(tex.szName);
								bool textureFoundInWad = false;
								for (auto& s : rend->wads)
								{
									if (s->hasTexture(tex.szName))
									{
										textureFoundInWad = true;
										break;
									}
								}
								if (!textureFoundInWad)
								{
									COLOR3* imageData = new COLOR3[tex.nWidth * tex.nHeight];
									memset(imageData, 255, tex.nWidth * tex.nHeight * sizeof(COLOR3));
									map->add_texture(tex.szName, (unsigned char*)imageData, tex.nWidth, tex.nHeight);
									delete[] imageData;
								}
							}
							else if (tex.nOffsets[0] <= 0)
							{
								print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0365), i);
								memset(tex.szName, 0, MAXTEXTURENAME);
								memcpy(tex.szName, "aaatrigger", 10);
							}
						}
					}
				}
				rend->reuploadTextures();
				rend->preRenderFaces();
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0585).c_str());
				ImGui::TextUnformatted(get_localized_string(LANG_0586).c_str());
				ImGui::EndTooltip();
			}

			// face_fix_duplicate_edges(i);
			ImGui::BeginDisabled();
			if (ImGui::MenuItem("Fix light entities[+TEXTURE]"))
			{
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Fill map with light entities for '+' textures");
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem("Fix light entities"))
			{
				mapFixLightEnts(map);
				g_app->updateEnts();
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Fill map with light entities");
				ImGui::EndTooltip();
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Additional tools", map && !map->is_mdl_model && rend))
		{
			if (ImGui::BeginMenu("Delete OOB Data", !app->isLoading && app->getSelectedMap() && rend))
			{

				static const char* optionNames[10] = {
					"All Axes",
					"X Axis",
					"X Axis (positive only)",
					"X Axis (negative only)",
					"Y Axis",
					"Y Axis (positive only)",
					"Y Axis (negative only)",
					"Z Axis",
					"Z Axis (positive only)",
					"Z Axis (negative only)",
				};

				static int clipFlags[10] = {
					-1,
					OOB_CLIP_X | OOB_CLIP_X_NEG,
					OOB_CLIP_X,
					OOB_CLIP_X_NEG,
					OOB_CLIP_Y | OOB_CLIP_Y_NEG,
					OOB_CLIP_Y,
					OOB_CLIP_Y_NEG,
					OOB_CLIP_Z | OOB_CLIP_Z_NEG,
					OOB_CLIP_Z,
					OOB_CLIP_Z_NEG,
				};

				for (int i = 0; i < 10; i++)
				{
					if (ImGui::MenuItem(optionNames[i], 0, false, !app->isLoading && app->getSelectedMap()))
					{
						if (map->ents[0]->hasKey("origin"))
						{
							vec3 ori = map->ents[0]->origin;
							print_log("Moved worldspawn origin by {} {} {}\n", ori.x, ori.y, ori.z);
							map->move(ori);
							map->ents[0]->removeKeyvalue("origin");
						}
						map->delete_oob_data(clipFlags[i]);
						rend->pushUndoState("Delete OOB Data", EDIT_MODEL_LUMPS | FL_ENTITIES);
					}
					IMGUI_TOOLTIP(g, "Deletes BSP data and entities outside of the "
									 "max map boundary.\n\n"
									 "This is useful for splitting maps to run in an engine with stricter map limits.");
				}

				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Delete internal textures", 0, false, !app->isLoading && app->getSelectedMap() && rend))
			{
				rend->pushUndoState("Delete internal textures", FL_TEXTURES);
				int deleted = map->delete_embedded_textures();
				rend->reloadTextures();
				rend->reload();
				print_log("Deleted {} embedded textures\n", deleted);
			}
			IMGUI_TOOLTIP(g, "Strip embedded MIP texture pixel data, converting textures to external WAD references");

			if (ImGui::MenuItem("Deduplicate Models", 0, false, rend && !app->isLoading && app->getSelectedMap()))
			{
				map->deduplicate_models();
				rend->pushUndoState("Deduplicate Models", EDIT_MODEL_LUMPS | FL_ENTITIES);
			}
			IMGUI_TOOLTIP(g, "Scans for duplicated BSP models and updates entity model keys to reference only one model in set of duplicated models. "
							 "This lowers the model count and allows more game models to be precached.\n\n"
							 "This does not delete BSP data structures unless you run the Clean command afterward.");
			if (ImGui::MenuItem("Downscale Invalid Textures", "(WIP)", false, rend && !app->isLoading && app->getSelectedMap()))
			{
				map->downscale_invalid_textures();
				rend->pushUndoState("Downscale Invalid Textures", FL_TEXINFO | FL_TEXTURES);
			}
			IMGUI_TOOLTIP(g, "Shrinks textures that exceed the max texture size and adjusts texture coordinates accordingly. Does not work with WAD textures yet.\n");
			if (ImGui::BeginMenu("Fix Bad Surface Extents", !app->isLoading && app->getSelectedMap()))
			{
				if (ImGui::MenuItem("Shrink Textures (512)", 0, false, !app->isLoading && app->getSelectedMap()))
				{
					map->fix_bad_surface_extents(false, true, 512);
					rend->pushUndoState("Shrink Textures (512)", FL_TEXINFO | FL_TEXTURES | FL_FACES);
				}
				IMGUI_TOOLTIP(g, "Downscales embedded textures on bad faces to a max resolution of 512x512 pixels. "
								 "This alone will likely not be enough to fix all faces with bad surface extents."
								 "You may also have to apply the Subdivide or Scale methods.");

				if (ImGui::MenuItem("Shrink Textures (256)", 0, false, !app->isLoading && app->getSelectedMap()))
				{
					map->fix_bad_surface_extents(false, true, 256);
					rend->pushUndoState("Shrink Textures (256)", FL_TEXINFO | FL_TEXTURES | FL_FACES);
				}
				IMGUI_TOOLTIP(g, "Downscales embedded textures on bad faces to a max resolution of 256x256 pixels. "
								 "This alone will likely not be enough to fix all faces with bad surface extents."
								 "You may also have to apply the Subdivide or Scale methods.");

				if (ImGui::MenuItem("Shrink Textures (128)", 0, false, !app->isLoading && app->getSelectedMap()))
				{
					map->fix_bad_surface_extents(false, true, 128);
					rend->pushUndoState("Shrink Textures (128)", FL_TEXINFO | FL_TEXTURES | FL_FACES);
				}
				IMGUI_TOOLTIP(g, "Downscales embedded textures on bad faces to a max resolution of 128x128 pixels. "
								 "This alone will likely not be enough to fix all faces with bad surface extents."
								 "You may also have to apply the Subdivide or Scale methods.");

				if (ImGui::MenuItem("Shrink Textures (64)", 0, false, !app->isLoading && app->getSelectedMap()))
				{
					map->fix_bad_surface_extents(false, true, 512);
					rend->pushUndoState("Shrink Textures (64)", FL_TEXINFO | FL_TEXTURES | FL_FACES);
				}
				IMGUI_TOOLTIP(g, "Downscales embedded textures to a max resolution of 64x64 pixels. "
								 "This alone will likely not be enough to fix all faces with bad surface extents."
								 "You may also have to apply the Subdivide or Scale methods.");

				ImGui::Separator();

				if (ImGui::MenuItem("Scale", 0, false, !app->isLoading && app->getSelectedMap()))
				{
					map->fix_bad_surface_extents(true, false, 0);
					rend->pushUndoState("Scale Textures", FL_TEXINFO | FL_TEXTURES | FL_FACES);
				}
				IMGUI_TOOLTIP(g, "Scales up face textures until they have valid extents. The drawback to this method is shifted texture coordinates and lower apparent texture quality.");

				if (ImGui::MenuItem("Subdivide", 0, false, !app->isLoading && app->getSelectedMap()))
				{
					map->fix_bad_surface_extents(false, false, 0);
					rend->pushUndoState("Subdivide Textures", FL_TEXINFO | FL_TEXTURES | FL_FACES);
				}
				IMGUI_TOOLTIP(g, "Subdivides faces until they have valid extents. The drawback to this method is reduced in-game performace from higher poly counts.");

				ImGui::MenuItem("[WARNING]", "WIP");
				IMGUI_TOOLTIP(g, "Anything you choose here will break lightmaps. "
								 "Run the map through a RAD compiler to fix, and pray that the mapper didn't "
								 "customize compile settings much.");
				ImGui::EndMenu();
			}

			if (ImGui::MenuItem("Make map overlay", NULL, false, map && !map->is_mdl_model && rend))
			{
				for (int m = map->modelCount - 1; m >= 1; m--)
				{
					int e = map->get_ent_from_model(m);
					if (e >= 0 && !starts_with(map->ents[e]->classname, "func_wa") &&
						!starts_with(map->ents[e]->classname, "func_ill"))
					{
						map->delete_model(m);
					}
				}

				map->remove_faces_by_content(CONTENTS_SKY);
				map->remove_faces_by_content(CONTENTS_SOLID);

				for (int f = map->faceCount - 1; f >= 0; f--)
				{
					BSPFACE32 face = map->faces[f];

					if (face.iTextureInfo >= 0)
					{
						BSPTEXTUREINFO texinfo = map->texinfos[face.iTextureInfo];
						if (texinfo.iMiptex >= 0)
						{
							int texOffset = ((int*)map->textures)[texinfo.iMiptex + 1];
							if (texOffset >= 0)
							{
								BSPMIPTEX tex = *((BSPMIPTEX*)(map->textures + texOffset));
								std::string texname = toLowerCase(tex.szName);
								if (starts_with(texname, "sky"))
								{
									map->remove_face(f);
								}
							}
						}
					}
				}

				map->remove_unused_model_structures();

				for (int i = map->modelCount - 1; i >= 1; i--)
				{
					int e = map->get_ent_from_model(i);

					map->duplicate_model_structures(i);
					auto offset = map->ents[e]->origin;
					auto verts = map->getModelVertsIds(i);
					for (int v : verts)
					{
						map->verts[v] += offset;
					}
				}

				map->remove_unused_model_structures();

				map->save_undo_lightmaps();

				// MAGIC! :)
				map->fix_all_duplicate_vertices();

				for (int f = 0; f < map->faceCount; f++)
				{
					auto verts = map->get_face_verts_idx(f);
					vec3 plane_z_normalized = map->getPlaneFromFace(&map->faces[f]).vNormal.normalize();

					for (auto v : verts)
					{
						map->verts[v] += plane_z_normalized * 0.15f;
					}
				}

				map->remove_unused_model_structures();
				map->resize_all_lightmaps();

				rend->loadLightmaps();
				rend->preRenderFaces();

				BSPMODEL tmpMdl{};
				tmpMdl.iFirstFace = 0;
				tmpMdl.nFaces = map->faceCount;
				map->get_bounding_box(tmpMdl.nMins, tmpMdl.nMaxs);

				tmpMdl.vOrigin = map->models[0].vOrigin;
				tmpMdl.nVisLeafs = 0;
				tmpMdl.iHeadnodes[0] = tmpMdl.iHeadnodes[1] = tmpMdl.iHeadnodes[2] = tmpMdl.iHeadnodes[3] = -1;
				map->replace_lump(LUMP_MODELS, &tmpMdl, sizeof(BSPMODEL));

				tmpMdl.iHeadnodes[0] = map->create_node_box(map->models[0].nMins, map->models[0].nMaxs, &map->models[0], true, 0);

				map->ents.erase(map->ents.begin() + 1, map->ents.end());
				map->update_ent_lump();

				map->remove_unused_model_structures(CLEAN_LIGHTMAP | CLEAN_PLANES | CLEAN_NODES | CLEAN_CLIPNODES | CLEAN_MARKSURFACES | CLEAN_FACES | CLEAN_SURFEDGES | CLEAN_TEXINFOS |
													CLEAN_EDGES | CLEAN_VERTICES | CLEAN_TEXTURES | CLEAN_VISDATA | CLEAN_MODELS);

				BSPLEAF32 tmpLeaf{};
				tmpLeaf.iFirstMarkSurface = 0;
				tmpLeaf.nMarkSurfaces = map->marksurfCount;
				tmpLeaf.nContents = CONTENTS_EMPTY;
				tmpLeaf.nVisOffset = -1;
				tmpLeaf.nMins = tmpMdl.nMins;
				tmpLeaf.nMaxs = tmpMdl.nMaxs;
				map->replace_lump(LUMP_LEAVES, &tmpLeaf, sizeof(BSPLEAF32));

				rend->pushUndoState("Create map BSP model overlay", EDIT_MODEL_LUMPS);
			}

			IMGUI_TOOLTIP(g, "Create overlay for every map face.\n");

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Experimental / WIP Tools", map && !map->is_mdl_model && rend))
		{
			if (ImGui::BeginMenu("MDL to BSP (WIP)", app->pickInfo.selectedEnts.size() == 1 &&
														 rend && rend->renderEnts[app->pickInfo.selectedEnts[0]].mdl))
			{
				if (ImGui::MenuItem("Bruteforce clipnodes", NULL, generateClipnodes == 1))
				{
					generateClipnodes = 1;
				}
				IMGUI_TOOLTIP(g, "Generate collision clipnodes by bruteforce plane decomposition of model meshes");

				if (ImGui::MenuItem("Compile clipnodes", NULL, generateClipnodes == 2, false))
				{
					generateClipnodes = 2;
				}
				IMGUI_TOOLTIP(g, "Compile collision hull clipnodes from model triangles using QBSP compiler");

				if (ImGui::MenuItem("Meshes to brushes", NULL, meshToBrush))
				{
					meshToBrush = !meshToBrush;
				}
				IMGUI_TOOLTIP(g, "Convert StudioModel polygon mesh data into solid BSP convex brush solids");

				ImGui::Separator();

				if (ImGui::MenuItem("Convert selected to BSP"))
				{
					for (auto ent : app->pickInfo.selectedEnts)
					{
						map->import_mdl_to_bsp(ent, generateClipnodes);
					}

					map->remove_unused_model_structures();

					map->save_undo_lightmaps();
					map->resize_all_lightmaps();

					rend->reuploadTextures();
					rend->loadLightmaps();

					rend->preRenderFaces();
					rend->preRenderEnts();

					rend->pushUndoState("CREATE MDL->BSP MODEL", EDIT_MODEL_LUMPS | FL_ENTITIES);
				}
				ImGui::EndMenu();
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Convert selected ent .MDL model to .BSP and add to map.");
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem("PROTECT MAP!(WIP)", NULL, false, map && !map->is_protected && rend))
			{
				map->merge_all_verts(1.f);

				bool partial_swap = false;
				for (int i = 0; i < map->edgeCount; i++)
				{
					std::swap(map->edges[i].iVertex[0], map->edges[i].iVertex[1]);
					map->surfedges[i] = -map->surfedges[i];
				}

				for (int m = 0; m < map->modelCount; m++)
				{
					BSPMODEL mdl = map->models[m];
					partial_swap = !partial_swap;
					if (mdl.iFirstFace >= 0 && mdl.nFaces > 1)
					{
						std::swap(map->faces[mdl.iFirstFace], map->faces[mdl.iFirstFace + 1]);
						for (int s = 0; s < map->marksurfCount; s++)
						{
							if (map->marksurfs[s] == mdl.iFirstFace)
							{
								map->marksurfs[s] = mdl.iFirstFace + 1;
							}
							else if (map->marksurfs[s] == mdl.iFirstFace + 1)
							{
								map->marksurfs[s] = mdl.iFirstFace;
							}
						}
					}
				}

				map->resize_all_lightmaps();
				rend->loadLightmaps();

				map->is_protected = true;

				rend->pushUndoState("PROTECT MAP FROM DECOMPILER", EDIT_MODEL_LUMPS);
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Protect map against decompilers.");
				ImGui::EndTooltip();
			}

			ImGui::EndMenu();
		}

		ImGui::Separator();
		if (ImGui::MenuItem(get_localized_string(LANG_0825).c_str(), NULL, showMergeMapWidget, !app->isLoading))
		{
			showMergeMapWidget = !showMergeMapWidget;
		}
		IMGUI_TOOLTIP(g, "Open multi-map merger tool window to combine, stack, or align multiple BSP maps");

		if (ImGui::MenuItem("Batch Entity Query (modent)...", NULL, showModentDialog, map && !map->is_mdl_model))
		{
			showModentDialog = !showModentDialog;
		}
		IMGUI_TOOLTIP(g, "Search, modify, or delete entities across active map using query expressions");

		ImGui::EndMenu();
	}
}

void Gui::drawMenu_Create()
{
	ImGuiContext& g = *GImGui;
	Bsp* map = app->getSelectedMap();
	BspRenderer* rend = map ? map->getBspRender() : NULL;

	if (ImGui::BeginMenu(get_localized_string(LANG_0587).c_str(), (map && !map->is_mdl_model)))
	{
		if (ImGui::MenuItem(get_localized_string(LANG_0588).c_str(), 0, false, map))
		{
			Entity* newEnt = new Entity();
			vec3 origin = (cameraOrigin + app->cameraForward * 100);
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "info_player_deathmatch");

			map->ents.push_back(newEnt);
			rend->pushUndoState("Create Entity", FL_ENTITIES);
		}
		IMGUI_TOOLTIP(g, "Create a point entity (info_player_start, light, ambient_generic) at camera target");

		if (ImGui::MenuItem(get_localized_string(LANG_0589).c_str(), 0, false, !app->isLoading && map))
		{
			vec3 origin = cameraOrigin + app->cameraForward * 100;
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);

			Entity* newEnt = new Entity();
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "func_illusionary");

			float mdl_size = 64.0f;

			int aaatriggerIdx = map->GetTriggerTexture();
			unsigned int dupLumps = FL_MARKSURFACES | FL_EDGES | FL_FACES | FL_NODES | FL_PLANES | FL_CLIPNODES | FL_SURFEDGES | FL_TEXINFO | FL_VERTICES | FL_LIGHTING | FL_MODELS | FL_LEAVES | FL_ENTITIES;

			if (aaatriggerIdx == -1)
			{
				dupLumps |= FL_TEXTURES;
				aaatriggerIdx = map->AddTriggerTexture();
			}

			vec3 mins = vec3(-mdl_size, -mdl_size, -mdl_size);
			vec3 maxs = vec3(mdl_size, mdl_size, mdl_size);
			int modelIdx = map->create_solid(mins, maxs, aaatriggerIdx, true);
			newEnt->addKeyvalue("model", "*" + std::to_string(modelIdx));
			map->ents.push_back(newEnt);

			BSPMODEL& model = map->models[modelIdx];
			for (int i = 0; i < model.nFaces; i++)
			{
				map->faces[model.iFirstFace + i].nStyles[0] = 0;
			}

			map->resize_all_lightmaps();
			rend->loadLightmaps();
			rend->refreshModel(modelIdx);
			rend->pushUndoState(get_localized_string(LANG_0589), dupLumps);
		}
		IMGUI_TOOLTIP(g, "Create a non-solid brush model entity (func_illusionary) at camera target");

		if (ImGui::MenuItem(get_localized_string(LANG_0591).c_str(), 0, false, !app->isLoading && map))
		{
			vec3 origin = cameraOrigin + app->cameraForward * 100;
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);

			Entity* newEnt = new Entity();
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "func_wall");

			float mdl_size = 64.0f;

			int aaatriggerIdx = map->GetTriggerTexture();
			unsigned int dupLumps = FL_MARKSURFACES | FL_EDGES | FL_FACES | FL_NODES | FL_PLANES | FL_CLIPNODES | FL_SURFEDGES | FL_TEXINFO | FL_VERTICES | FL_LIGHTING | FL_MODELS | FL_LEAVES | FL_ENTITIES;
			if (aaatriggerIdx == -1)
			{
				dupLumps |= FL_TEXTURES;
				aaatriggerIdx = map->AddTriggerTexture();
			}

			vec3 mins = vec3(-mdl_size, -mdl_size, -mdl_size);
			vec3 maxs = vec3(mdl_size, mdl_size, mdl_size);
			int modelIdx = map->create_solid(mins, maxs, aaatriggerIdx, false);
			newEnt->addKeyvalue("model", "*" + std::to_string(modelIdx));
			map->ents.push_back(newEnt);

			BSPMODEL& model = map->models[modelIdx];
			for (int i = 0; i < model.nFaces; i++)
			{
				map->faces[model.iFirstFace + i].nStyles[0] = 0;
			}

			map->resize_all_lightmaps();
			rend->loadLightmaps();
			rend->refreshModel(modelIdx);
			rend->pushUndoState(get_localized_string(LANG_0591), dupLumps);
		}
		IMGUI_TOOLTIP(g, "Create a solid brush model entity (func_wall) at camera target");

		if (ImGui::MenuItem(get_localized_string(LANG_0590).c_str(), 0, false, !app->isLoading && map))
		{
			vec3 origin = cameraOrigin + app->cameraForward * 100;
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);

			Entity* newEnt = new Entity();
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "trigger_once");

			float mdl_size = 64.0f;

			int aaatriggerIdx = map->GetTriggerTexture();
			unsigned int dupLumps = FL_MARKSURFACES | FL_EDGES | FL_FACES | FL_NODES | FL_PLANES | FL_CLIPNODES | FL_SURFEDGES | FL_TEXINFO | FL_VERTICES | FL_LIGHTING | FL_MODELS | FL_LEAVES | FL_ENTITIES;
			if (aaatriggerIdx == -1)
			{
				dupLumps |= FL_TEXTURES;
				aaatriggerIdx = map->AddTriggerTexture();
			}

			vec3 mins = vec3(-mdl_size, -mdl_size, -mdl_size);
			vec3 maxs = vec3(mdl_size, mdl_size, mdl_size);
			int modelIdx = map->create_solid(mins, maxs, aaatriggerIdx, true);
			newEnt->addKeyvalue("model", "*" + std::to_string(modelIdx));

			BSPMODEL& model = map->models[modelIdx];
			model.iFirstFace = 0;
			model.nFaces = 0;
			map->remove_unused_model_structures(CLEAN_FACES | CLEAN_MARKSURFACES);
			map->ents.push_back(newEnt);

			map->resize_all_lightmaps();
			rend->pushUndoState(get_localized_string(LANG_0590), dupLumps);
			rend->refreshModel(modelIdx);
		}
		IMGUI_TOOLTIP(g, "Create an invisible trigger volume brush entity (trigger_multiple) at camera target");

		if (ImGui::MenuItem("BSP Clip model", 0, false, !app->isLoading && map))
		{
			vec3 origin = cameraOrigin + app->cameraForward * 100;
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);

			Entity* newEnt = new Entity();
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "func_wall");

			float mdl_size = 64.0f;

			int aaatriggerIdx = map->GetTriggerTexture();
			unsigned int dupLumps = FL_MARKSURFACES | FL_EDGES | FL_FACES | FL_NODES | FL_PLANES | FL_CLIPNODES | FL_SURFEDGES | FL_TEXINFO | FL_VERTICES | FL_LIGHTING | FL_MODELS | FL_LEAVES | FL_ENTITIES;
			if (aaatriggerIdx == -1)
			{
				dupLumps |= FL_TEXTURES;
				aaatriggerIdx = map->AddTriggerTexture();
			}

			vec3 mins = vec3(-mdl_size, -mdl_size, -mdl_size);
			vec3 maxs = vec3(mdl_size, mdl_size, mdl_size);
			int modelIdx = map->create_solid(mins, maxs, aaatriggerIdx, true);
			newEnt->addKeyvalue("model", "*" + std::to_string(modelIdx));

			BSPMODEL& model = map->models[modelIdx];
			model.iFirstFace = 0;
			model.nFaces = 0;
			map->remove_unused_model_structures(CLEAN_FACES | CLEAN_MARKSURFACES);
			map->ents.push_back(newEnt);

			map->resize_all_lightmaps();
			rend->pushUndoState("BSP Clip model", dupLumps);
		}
		IMGUI_TOOLTIP(g, "Create a player-clipping barrier solid brush entity (func_monsterclip) at camera target");

		if (DebugKeyPressed)
		{
			if (ImGui::BeginMenu("Other"))
			{
				if (ImGui::MenuItem("Random DM spawn points"))
				{
					for (int i = (int)map->ents.size() - 1; i >= 0; i--)
					{
						if (map->ents[i]->classname == "info_player_deathmatch" ||
							map->ents[i]->classname == "info_player_start")
						{
							map->ents.erase(map->ents.begin() + i);
						}
					}
					// todo....

					g_app->pickInfo.selectedEnts.clear();
				}
				IMGUI_TOOLTIP(g, "Distribute random info_player_deathmatch spawn entities across walkable floor surfaces");
				ImGui::EndMenu();
			}
		}
		ImGui::EndMenu();
	}
}

void Gui::drawMenu_Windows()
{
	ImGuiContext& g = *GImGui;
	Bsp* map = app->getSelectedMap();

	if (ImGui::BeginMenu(get_localized_string(LANG_0601).c_str()))
	{
		ImGui::TextDisabled("Primary Panels:");
		if (ImGui::MenuItem("Entity Keyvalues", "Alt+Enter", &showKeyvalueWidget, map != nullptr))
		{
		}
		IMGUI_TOOLTIP(g, "Open Entity Keyvalue & SmartEdit inspector panel");

		if (ImGui::MenuItem("3D Transform Tool", "Ctrl+M", &showTransformWidget, map != nullptr))
		{
		}
		IMGUI_TOOLTIP(g, "Open 3D coordinate transform manipulator panel");

		if (ImGui::MenuItem("Face Editor", "F6", &showFaceEditWidget, map != nullptr))
		{
		}
		IMGUI_TOOLTIP(g, "Open texture alignment, scaling, and face manipulation panel");

		if (ImGui::MenuItem("Texture Browser", "F4", &showTextureBrowser, map != nullptr))
		{
		}
		IMGUI_TOOLTIP(g, "Browse embedded and WAD textures");

		if (ImGui::MenuItem("Lightmap Editor", NULL, &showLightmapEditorWidget, map != nullptr))
		{
		}
		IMGUI_TOOLTIP(g, "Open face lightmap luminance and RGB color editor panel");

		ImGui::Separator();
		ImGui::TextDisabled("Tools & Inspection:");

		if (ImGui::MenuItem("Map Limits & Statistics", "F2", &showLimitsWidget, map != nullptr))
		{
		}
		IMGUI_TOOLTIP(g, "Open engine limits and lump usage analyzer window");

		if (ImGui::MenuItem("Entity Report Table", "F3", &showEntityReport, map != nullptr))
		{
		}
		IMGUI_TOOLTIP(g, "Open full searchable entity report table window");

		if (ImGui::MenuItem("Map Merger", NULL, &showMergeMapWidget))
		{
		}
		IMGUI_TOOLTIP(g, "Open multi-map merger tool window");

		if (ImGui::MenuItem("Map Overview (2D Radar)", NULL, &showOverviewWidget, map != nullptr))
		{
		}
		IMGUI_TOOLTIP(g, "Open 2D radar/overview map rendering controls");

		if (ImGui::MenuItem("Go to Coordinates (GOTO)", "Ctrl+Shift+G", &showGOTOWidget, map != nullptr))
		{
		}
		IMGUI_TOOLTIP(g, "Teleport camera to specific coordinates or entity index");

		if (ImGui::MenuItem("Batch Entity Query (modent)", NULL, &showModentDialog, map != nullptr))
		{
		}
		IMGUI_TOOLTIP(g, "Open headless query runner for batch entity operations");

		if (ImGui::MenuItem("Log Console", "F5", &showLogWidget))
		{
		}
		IMGUI_TOOLTIP(g, "Open bspguy diagnostic output log window");

		if (ImGui::MenuItem("Debug PVS / Engine Inspector", NULL, &showDebugWidget, map != nullptr))
		{
		}
		IMGUI_TOOLTIP(g, "Open low-level BSP engine debug and PVS inspection panel");

#ifdef WIN32
		if (ImGui::MenuItem("Developer Console", NULL, &g_console_visible))
		{
			showConsoleWindow(g_console_visible);
		}
		IMGUI_TOOLTIP(g, "Toggle embedded developer output console window");
#endif

		if (mapRenderers.size() > 0)
		{
			ImGui::Separator();
			ImGui::TextDisabled("Open Map Tabs:");
			Bsp* selectedMap = app->getSelectedMap();
			for (BspRenderer* bspRend : mapRenderers)
			{
				if (bspRend->map && !bspRend->map->is_bsp_model)
				{
					if (ImGui::MenuItem(bspRend->map->bsp_name.c_str(), NULL, selectedMap == bspRend->map))
					{
						if (selectedMap && selectedMap->getBspRender())
						{
							selectedMap->getBspRender()->renderCameraAngles = cameraAngles;
							selectedMap->getBspRender()->renderCameraOrigin = cameraOrigin;
						}
						app->deselectObject();
						app->clearSelection();
						app->selectMap(bspRend->map);
						cameraAngles = bspRend->renderCameraAngles;
						cameraOrigin = bspRend->renderCameraOrigin;
						makeVectors(cameraAngles, app->cameraForward, app->cameraRight, app->cameraUp);
					}
					IMGUI_TOOLTIP(g, "Switch active editor view to this map tab");
				}
			}
		}
		ImGui::EndMenu();
	}
}

void Gui::drawMenu_Help()
{
	ImGuiContext& g = *GImGui;

	if (ImGui::BeginMenu(get_localized_string(LANG_0602).c_str()))
	{
		if (ImGui::MenuItem(get_localized_string(LANG_0603).c_str(), "F1"))
		{
			showHelpWidget = true;
		}
		IMGUI_TOOLTIP(g, "Open keyboard shortcuts reference guide and documentation dialog");

		if (ImGui::MenuItem(get_localized_string(LANG_0604).c_str()))
		{
			showAboutWidget = true;
		}
		IMGUI_TOOLTIP(g, "Display application version, build compiler, and developer credits");

		ImGui::EndMenu();
	}
}

void Gui::drawMenu_Debug()
{
	ImGuiContext& g = *GImGui;
	Bsp* map = app->getSelectedMap();
	BspRenderer* rend = map ? map->getBspRender() : NULL;

	if (ImGui::BeginMenu(get_localized_string(LANG_0605).c_str(), map && !map->is_mdl_model))
	{
		if (ImGui::MenuItem("Print textures"))
		{
			for (int i = 0; i < map->textureCount; i++)
			{
				int mip_offset = ((int*)map->textures)[i + 1];
				const char* name = "";
				int data_offset = 0;
				if (mip_offset >= 0)
				{
					BSPMIPTEX* tex = (BSPMIPTEX*)(map->textures + mip_offset);
					data_offset = tex->nOffsets[0];
					name = tex->szName;
					int colors = -1;
					if (tex->nOffsets[0] > 0)
					{
						int w = tex->nWidth;
						int h = tex->nHeight;

						int szAll = calcMipsSize(w, h);

						unsigned char* texdata = (unsigned char*)(((unsigned char*)tex) + tex->nOffsets[0]);
						colors = (int)*(unsigned short*)(texdata + szAll);
					}
					print_log("mip name \"{}\" offset {} data offset {}-{}-{}-{} size {}x{} colors {}\n", name, mip_offset, tex->nOffsets[0],
							  tex->nOffsets[1], tex->nOffsets[2], tex->nOffsets[3], tex->nWidth, tex->nHeight, colors);
				}
				else
				{
					print_log("mip name \"BAD NAME\" offset {} data offset NO DATA OFFSET\n", name, mip_offset, data_offset);
				}
			}
		}
		IMGUI_TOOLTIP(g, "Print all loaded texture memory addresses and dimensions to debug log");

		if (ImGui::MenuItem("CREATE SKYBOX"))
		{
			map->remove_faces_by_content(CONTENTS_SOLID);
			map->remove_faces_by_content(CONTENTS_SKY);

			for (int f = map->faceCount - 1; f >= 0; f--)
			{
				BSPFACE32 face = map->faces[f];

				if (face.iTextureInfo >= 0)
				{
					BSPTEXTUREINFO texinfo = map->texinfos[face.iTextureInfo];
					if (texinfo.iMiptex >= 0)
					{
						int texOffset = ((int*)map->textures)[texinfo.iMiptex + 1];
						if (texOffset >= 0)
						{
							BSPMIPTEX tex = *((BSPMIPTEX*)(map->textures + texOffset));
							std::string texname = toLowerCase(tex.szName);
							if (starts_with(texname, "sky"))
							{
								map->remove_face(f);
							}
						}
					}
				}
			}

			map->save_undo_lightmaps();
			map->resize_all_lightmaps();

			rend->loadLightmaps();
			rend->preRenderFaces();

			map->update_ent_lump();
			map->update_lump_pointers();

			vec3 org_mins = vec3(-256.0f, -256.0f, -256.0f), org_maxs = vec3(256.0f, 256.0f, 256.0f);
			float scale_val = ((g_limits.fltMaxCoord - 2.0f) / 256.0f);

			// map->get_bounding_box(mins, maxs);
			int newModelIdx = ImportModel(map, "./primitives/skybox.bsp", true);

			Entity* newEnt = new Entity("func_wall");

			newEnt->addKeyvalue("model", "*" + std::to_string(newModelIdx));
			map->ents.push_back(newEnt);

			for (auto& ent : map->ents)
			{
				if (ent->isWorldSpawn())
				{
					ent->setOrAddKeyvalue("MaxRange", std::to_string((int)(g_limits.fltMaxCoord * 2.0f + 1.0f)));
				}
			}

			map->update_ent_lump();

			if (map->ents.size() > 0)
			{
				rend->refreshEnt((int)(map->ents.size()) - 1);
			}

			//./primitives/skytest/sky_up.png
			//./primitives/skytest/sky_dn.png
			//./primitives/skytest/sky_ft.png
			//./primitives/skytest/sky_bk.png
			//./primitives/skytest/sky_fl.png
			//./primitives/skytest/sky_rt.png

			// up
			{
				unsigned char* sky_malloc = NULL;
				unsigned int w, h;
				lodepng_decode24_file(&sky_malloc, &w, &h, "./primitives/skytest/sky_up.png");
				if (sky_malloc)
				{
					int out_w, out_h;
					auto images = splitImage((COLOR3*)sky_malloc, w, h, 4, 4, out_w, out_h);
					const int new_w = 256, new_h = 256;
					for (auto& img : images)
					{
						std::vector<COLOR3> new_img;
						scaleImage(img.data(), new_img, out_w, out_h, new_w, new_h);
						img = new_img;
					}
					out_w = new_w;
					out_h = new_h;

					print_log("Split {}x{} to {} images with size {}x{}\n", w, h, images.size(), out_w, out_h);
					for (int x = 0; x < 4; x++)
					{
						for (int y = 0; y < 4; y++)
						{
							auto img = getSubImage(images, x, y, 4);
							lodepng_encode24_file(("test-" + std::to_string(x) + "-" + std::to_string(y) + ".png").c_str(), (unsigned char*)img.data(), out_w, out_h);
						}
					}

					for (int x = 0; x < 4; x++)
					{
						for (int y = 0; y < 4; y++)
						{
							std::string sky_side = "box_up_" + std::to_string(x) + "x" + std::to_string(y);

							auto target_img = getSubImage(images, x, y, 4);
							if (GetImageColors(target_img.data(), new_w * new_h) > 256)
							{
								COLOR3 palette[256];
								unsigned int colorCount = 0;
								if (!map->is_texture_has_pal)
								{
									if (g_settings.pal_id >= 0)
									{
										colorCount = g_settings.palettes[g_settings.pal_id].colors;
										memcpy(palette, g_settings.palettes[g_settings.pal_id].data, g_settings.palettes[g_settings.pal_id].colors * sizeof(COLOR3));
									}
									else
									{
										colorCount = 256;
										memcpy(palette, g_settings.palette_default, 256 * sizeof(COLOR3));
									}
								}
								else
								{
									colorCount = 0;
								}

								COLOR3* newTex = new COLOR3[new_w * new_h];
								memcpy(newTex, target_img.data(), (new_w * new_h) * sizeof(COLOR3));

								Quantizer* tmpCQuantizer = new Quantizer(256, 8);
								if (colorCount != 0)
									tmpCQuantizer->SetColorTable(palette, 256);
								tmpCQuantizer->ApplyColorTable((COLOR3*)newTex, new_w * new_h);
								delete tmpCQuantizer;

								lodepng_encode24_file(("testQuantizer-" + std::to_string(x) + "-" + std::to_string(y) + ".png").c_str(), (unsigned char*)newTex, out_w, out_h);

								map->add_texture(sky_side.c_str(), (unsigned char*)newTex, new_w, new_h);
								delete[] newTex;
							}
							else
								map->add_texture(sky_side.c_str(), (unsigned char*)target_img.data(), new_w, new_h);
						}
					}
					free(sky_malloc);
				}
			}

			STRUCTUSAGE modelUsage = STRUCTUSAGE(map);
			map->mark_model_structures(newModelIdx, &modelUsage, true);

			map->models[newModelIdx].nMaxs *= scale_val;
			map->models[newModelIdx].nMins *= scale_val;

			for (int i = 0; i < map->vertCount; i++)
			{
				if (modelUsage.verts[i])
				{
					map->verts[i] *= scale_val;
				}
			}

			for (int i = 0; i < map->texinfoCount; i++)
			{
				if (modelUsage.texInfo[i])
				{
					mat4x4 scaleMat;
					scaleMat.loadIdentity();
					scaleMat.scale(1.0f / 2.0f, 1.0f / 2.0f, 1.0f / 2.0f);
					BSPTEXTUREINFO& info = map->texinfos[i];

					info.vS = (scaleMat * vec4(info.vS, 1)).xyz();
					info.vT = (scaleMat * vec4(info.vT, 1)).xyz();

					info.shiftS *= 2.0f;
					info.shiftT *= 2.0f;
				}
			}

			for (int i = 0; i < map->texinfoCount; i++)
			{
				if (modelUsage.texInfo[i])
				{
					mat4x4 scaleMat;
					scaleMat.loadIdentity();
					scaleMat.scale(1.0f / scale_val, 1.0f / scale_val, 1.0f / scale_val);
					BSPTEXTUREINFO& info = map->texinfos[i];

					info.vS = (scaleMat * vec4(info.vS, 1)).xyz();
					info.vT = (scaleMat * vec4(info.vT, 1)).xyz();

					// float shiftS = info.shiftS;
					// float shiftT = info.shiftT;

					//// magic guess-and-check code that somehow works some of the time
					//// also its shit
					// for (int k = 0; k < 3; k++)
					//{
					//	vec3 stretchDir;
					//	if (k == 0) stretchDir = vec3(1.0f, 0, 0).normalize();
					//	if (k == 1) stretchDir = vec3(0, 1.0f, 0).normalize();
					//	if (k == 2) stretchDir = vec3(0, 0, 1.0f).normalize();

					//	float refDist = 0;
					//	if (k == 0) refDist = scaleFromDist.x;
					//	if (k == 1) refDist = scaleFromDist.y;
					//	if (k == 2) refDist = scaleFromDist.z;

					//	vec3 texFromDir;
					//	if (k == 0) texFromDir = dir * vec3(1, 0, 0);
					//	if (k == 1) texFromDir = dir * vec3(0, 1, 0);
					//	if (k == 2) texFromDir = dir * vec3(0, 0, 1);

					//	float dotS = dotProduct(oldinfo.oldS.normalize(), stretchDir);
					//	float dotT = dotProduct(oldinfo.oldT.normalize(), stretchDir);

					//	float dotSm = dotProduct(texFromDir, info.vS) < 0 ? 1.0f : -1.0f;
					//	float dotTm = dotProduct(texFromDir, info.vT) < 0 ? 1.0f : -1.0f;

					//	// hurr dur oh god im fucking retarded huurr
					//	if (k == 0 && dotProduct(texFromDir, fromDir) < 0 != fromDir.x < 0)
					//	{
					//		dotSm *= -1.0f;
					//		dotTm *= -1.0f;
					//	}
					//	if (k == 1 && dotProduct(texFromDir, fromDir) < 0 != fromDir.y < 0)
					//	{
					//		dotSm *= -1.0f;
					//		dotTm *= -1.0f;
					//	}
					//	if (k == 2 && dotProduct(texFromDir, fromDir) < 0 != fromDir.z < 0)
					//	{
					//		dotSm *= -1.0f;
					//		dotTm *= -1.0f;
					//	}

					//	float vsdiff = info.vS.length() - oldinfo.oldS.length();
					//	float vtdiff = info.vT.length() - oldinfo.oldT.length();

					//	shiftS += (refDist * vsdiff * abs(dotS)) * dotSm;
					//	shiftT += (refDist * vtdiff * abs(dotT)) * dotTm;
					//}

					// info.shiftS = shiftS;
					// info.shiftT = shiftT;

					// map->texinfos[i].vS /= scale_val;
					// map->texinfos[i].vT /= scale_val;
				}
			}
			for (int i = 0; i < map->nodeCount; i++)
			{
				if (modelUsage.nodes[i])
				{
					map->nodes[i].nMaxs *= scale_val;
					map->nodes[i].nMins *= scale_val;
				}
			}
			for (int i = 0; i < map->leafCount; i++)
			{
				if (modelUsage.leaves[i])
				{
					map->leaves[i].nMaxs *= scale_val;
					map->leaves[i].nMins *= scale_val;
				}
			}
			for (int i = 0; i < map->planeCount; i++)
			{
				if (modelUsage.planes[i])
				{
					map->planes[i].fDist *= scale_val;
				}
			}

			rend->reuploadTextures();
			rend->preRenderFaces();
			rend->pushUndoState("CREATE SKYBOX", EDIT_MODEL_LUMPS | FL_ENTITIES);
		}
		IMGUI_TOOLTIP(g, "Generate a 6-sided bounding skybox solid around the entire map perimeter");

		ImGui::EndMenu();
	}
}

void Gui::recompileLighting()
{
	Bsp* map = app->getSelectedMap();
	BspRenderer* rend = map ? map->getBspRender() : NULL;
	if (!map || map->is_mdl_model || !rend)
	{
		return;
	}

	if (g_settings.rad_path.empty())
	{
		radErrorMessage = "No RAD compiler configured!\n\nPlease configure the RAD executable path in Settings (Paths tab).";
		radLogFilePath = "";
		showRadErrorModal = true;
		print_log(PRINT_RED, "No hlrad executable configured in settings!\n");
		return;
	}

	std::string resolved_rad_path = g_settings.rad_path;
	FindPathInAssets(map, g_settings.rad_path, resolved_rad_path);
	if (!fileExists(resolved_rad_path))
	{
		radErrorMessage = fmt::format("HLRAD executable not found at:\n{}\n\nPlease verify the path in Settings (Paths tab).", resolved_rad_path);
		radLogFilePath = "";
		showRadErrorModal = true;
		print_log(PRINT_RED, "No hlrad executable found at: {}\n", resolved_rad_path);
		return;
	}

	g_settings.save_cam = true;
	map->save_cam_pos = cameraOrigin;
	map->save_cam_angles = cameraAngles;

	map->update_ent_lump();
	map->update_lump_pointers();
	map->validate();
	map->write(map->bsp_path);

	std::string args = g_settings.rad_options;
	if (args.empty())
	{
		args = "\"{map_path}\"";
	}
	else if (args.find("{map_path}") == std::string::npos)
	{
		args += " \"{map_path}\"";
	}

	std::string bsp_path;
	std::string old_bsp_path = map->bsp_path;
	map->ExportExtFile(old_bsp_path, bsp_path);

	size_t old_bsp_size = fileSize(bsp_path);
	if (old_bsp_size == 0 || !fileExists(bsp_path))
	{
		radErrorMessage = "Failed to export intermediate sidecar files (.ext, .wa_, _nolight.bsp) for RAD compilation.";
		radLogFilePath = "";
		showRadErrorModal = true;
		print_log(PRINT_RED, "Error exporting old rad lighting files!\n");
		return;
	}

	std::string delfileprefix = bsp_path.substr(0, bsp_path.size() - 4);
	std::string logFilePath = delfileprefix + ".log";
	std::string errFilePath = delfileprefix + ".err";

	replaceAll(args, "{map_path}", bsp_path);
	showConsoleWindow(true);

	print_log(PRINT_BLUE, "Running RAD compiler: {} {}\n", resolved_rad_path, args);

	Process* tmpProc = new Process(resolved_rad_path);
	tmpProc->arg(args);
	int exitCode = tmpProc->executeAndWait(0, 0, 0);
	delete tmpProc;

	// Check if lighting lump was written and has non-zero length
	bool validLighting = false;
	size_t new_bsp_size = fileSize(bsp_path);
	if (new_bsp_size > 0)
	{
		std::ifstream checkFile(bsp_path, std::ios::binary);
		if (checkFile.is_open())
		{
			BSPHEADER checkHeader;
			checkFile.read((char*)&checkHeader, sizeof(BSPHEADER));
			if (checkFile.gcount() == sizeof(BSPHEADER))
			{
				if (checkHeader.nVersion == 30 && checkHeader.lump[LUMP_LIGHTING].nLength > 0)
				{
					validLighting = true;
				}
			}
			checkFile.close();
		}
	}

	if (exitCode != 0 || !validLighting || new_bsp_size == old_bsp_size)
	{
		print_log(PRINT_RED, "Failed rad compiler (exit code {})!\n", exitCode);
		radErrorMessage = fmt::format(
			"HLRAD compilation failed with exit code {}.\n\n"
			"Executable:\n{}\n\n"
			"Arguments:\n{}\n\n"
			"Lighting data was {} generated.\n\n"
			"Check the compiler log for detailed error output.",
			exitCode, resolved_rad_path, args,
			validLighting ? "partially" : "not"
		);
		radLogFilePath = fileExists(logFilePath) ? logFilePath : (fileExists(errFilePath) ? errFilePath : "");
		showRadErrorModal = true;

		// Clean up intermediate files, but keep .log and .err for diagnostics
		removeFile(bsp_path);
		removeFile(delfileprefix + ".wa_");
		removeFile(delfileprefix + ".ext");
		return;
	}

	// Succeeded: Hot reload map in editor
	print_log(PRINT_GREEN, "HLRAD compilation successful! Reloading map lightmaps...\n");

	vec3 savedOrigin = cameraOrigin;
	vec3 savedAngles = cameraAngles;

	int mapRenderId = map->getBspRenderId();
	if (mapRenderId >= 0)
	{
		BspRenderer* mapRender = map->getBspRender();
		if (mapRender)
		{
			map->setBspRender(NULL);
			app->deselectObject();
			app->clearSelection();
			app->deselectMap();
			mapRenderers.erase(mapRenderers.begin() + mapRenderId);
			delete mapRender;
			map = NULL;
			app->selectMapId(0);
		}
	}

	removeFile(old_bsp_path);
	copyFile(bsp_path, old_bsp_path);

	map = new Bsp(old_bsp_path);
	app->addMap(map);

	cameraOrigin = savedOrigin;
	cameraAngles = savedAngles;

	// Remove temporary files
	removeFile(bsp_path);
	removeFile(delfileprefix + ".wa_");
	removeFile(delfileprefix + ".ext");
	removeFile(delfileprefix + ".log");
	removeFile(delfileprefix + ".err");
}

