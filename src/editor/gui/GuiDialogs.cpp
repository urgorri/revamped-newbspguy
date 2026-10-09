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
#include "cli/EntityQuery.h"
#include <filesystem>
#include <algorithm>
#include <cmath>

extern float g_tooltip_delay;

void Gui::drawSettings()
{
	ImGui::SetNextWindowSize(ImVec2(790.f, 340.f), ImGuiCond_FirstUseEver);

	bool oldShowSettings = showSettingsWidget;
	bool apply_settings_pressed = false;
	static std::string langForSelect = g_settings.selected_lang;
	static std::string palForSelect = toUpperCase(g_settings.palette_name);
	static std::string engForSelect = g_limits.engineName;
	static BSPLimits prevLimits = g_limits;

	if (ImGui::Begin(fmt::format("{}###SETTING_WIDGET", get_localized_string(LANG_1114)).c_str(), &showSettingsWidget))
	{
		ImGuiContext& g = *GImGui;
		const int settings_tabs = 7;

		static int resSelected = 0;
		static int fgdSelected = 0;

		std::string tab_titles[settings_tabs] = {
			get_localized_string("LANG_SETTINGS_GENERAL"),
			get_localized_string("LANG_SETTINGS_FGDPATH"),
			get_localized_string("LANG_SETTINGS_WADPATH"),
			get_localized_string("LANG_SETTINGS_OPTIMIZE"),
			get_localized_string("LANG_SETTINGS_LIMITS"),
			get_localized_string("LANG_SETTINGS_RENDER"),
			get_localized_string("LANG_SETTINGS_CONTROL")};

		// left
		ImGui::BeginChild(get_localized_string(LANG_0709).c_str(), ImVec2(150, 0), true);

		for (int i = 0; i < settings_tabs; i++)
		{
			if (ImGui::Selectable(tab_titles[i].c_str(), settingsTab == i))
				settingsTab = i;
		}

		ImGui::Separator();

		ImGui::Dummy(ImVec2(0, 60));
		ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
		bool clicked_1 = ImGui::Button(get_localized_string(LANG_0710).c_str());
		ImGui::PopStyleColor(1);
		if (clicked_1)
		{
			apply_settings_pressed = true;
		}

		ImGui::EndChild();

		ImGui::SameLine();

		// right

		ImGui::BeginGroup();
		float footerHeight = settingsTab <= 2 ? ImGui::GetFrameHeightWithSpacing() + 4.f : 0.f;
		ImGui::BeginChild(get_localized_string(LANG_0711).c_str(), ImVec2(0, -footerHeight)); // Leave room for 1 line below us
		ImGui::Text(tab_titles[settingsTab].c_str());
		ImGui::Separator();

		if (reloadSettings)
		{
			reloadSettings = false;
		}

		float pathWidth = ImGui::GetWindowWidth() - 60.f;
		float delWidth = 50.f;

		if (ifd::FileDialog::Instance().IsDone("GameDir"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				g_settings.gamedir = stripFileName(res.string());
				g_settings.lastdir = stripFileName(res.string());
			}
			ifd::FileDialog::Instance().Close();
		}

		if (ifd::FileDialog::Instance().IsDone("WorkingDir"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				g_settings.workingdir = stripFileName(res.string());
				FixupAllSystemPaths();
				g_settings.lastdir = stripFileName(res.string());
			}
			ifd::FileDialog::Instance().Close();
		}

		if (ifd::FileDialog::Instance().IsDone("fgdOpen"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				g_settings.fgdPaths[fgdSelected].path = res.string();
				g_settings.fgdPaths[fgdSelected].enabled = true;
				g_settings.lastdir = stripFileName(res.string());
			}
			ifd::FileDialog::Instance().Close();
		}

		if (ifd::FileDialog::Instance().IsDone("radPath"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				g_settings.rad_path = res.string();
				g_settings.lastdir = stripFileName(res.string());
			}
			ifd::FileDialog::Instance().Close();
		}

		if (ifd::FileDialog::Instance().IsDone("resOpen"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				g_settings.resPaths[resSelected].path = res.string();
				g_settings.resPaths[resSelected].enabled = true;
				g_settings.lastdir = stripFileName(res.string());
			}
			ifd::FileDialog::Instance().Close();
		}

		if (ifd::FileDialog::Instance().IsDone("SkyboxDir"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				g_settings.skybox_dir = stripFileName(res.string());
				g_settings.lastdir = stripFileName(res.string());
			}
			ifd::FileDialog::Instance().Close();
		}

		ImGui::BeginChild(get_localized_string(LANG_0712).c_str());
		if (settingsTab == 0)
		{
			ImGui::Text(get_localized_string(LANG_0713).c_str());
			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_2 = ImGui::Button("Auto detect fgd/wad");
			ImGui::PopStyleColor(1);
			if (clicked_2)
			{
				if (!dirExists(g_settings.gamedir))
				{
					print_log("No gamedir found!\n");
				}
				else
				{
					std::vector<std::string> wadDirList;
					std::vector<std::string> fgdFileList;
					findDirsWithHasFileExtension(g_settings.gamedir, ".wad", wadDirList, true);
					findFilesWithExtension(g_settings.gamedir, ".fgd", fgdFileList, true);

					g_settings.fgdPaths.clear();
					PathToggleStruct tmpPath("", true);
					for (auto& f : fgdFileList)
					{
						tmpPath.path = f;
						g_settings.fgdPaths.push_back(tmpPath);
					}

					g_settings.resPaths.clear();
					for (auto& w : wadDirList)
					{
						tmpPath.path = w;
						g_settings.resPaths.push_back(tmpPath);
					}
				}
			}
			ImGui::SetNextItemWidth(pathWidth);
			ImGui::InputText(get_localized_string(LANG_0714).c_str(), &g_settings.gamedir);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay && g_settings.gamedir.size())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(g_settings.gamedir.c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(delWidth);
			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_3 = ImGui::Button(get_localized_string(LANG_0715).c_str());
			ImGui::PopStyleColor(1);
			if (clicked_3)
			{
				ifd::FileDialog::Instance().Open("GameDir", "Select game dir", std::string(), false, g_settings.lastdir);
			}
			ImGui::Text(get_localized_string(LANG_0716).c_str());
			ImGui::SetNextItemWidth(pathWidth);
			if (ImGui::InputText(get_localized_string(LANG_0717).c_str(), &g_settings.workingdir))
			{
				FixupAllSystemPaths();
			}
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "This directory will be used for all generated content (exports, backups, etc.)");
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay && g_settings.workingdir.size())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(g_settings.workingdir.c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(delWidth);
			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_4 = ImGui::Button(get_localized_string(LANG_0718).c_str());
			ImGui::PopStyleColor(1);
			if (clicked_4)
			{
				ifd::FileDialog::Instance().Open("WorkingDir", "Select working dir", std::string(), false, g_settings.lastdir);
			}

			ImGui::Text("Skybox directory (optional, defaults to <GameDir>/gfx/env/):");
			ImGui::SetNextItemWidth(pathWidth);
			ImGui::InputText("##skybox_dir", &g_settings.skybox_dir);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay && g_settings.skybox_dir.size())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(g_settings.skybox_dir.c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(delWidth);
			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_skybox_dir = ImGui::Button("Browse##skybox");
			ImGui::PopStyleColor(1);
			if (clicked_skybox_dir)
			{
				ifd::FileDialog::Instance().Open("SkyboxDir", "Select skybox directory", std::string(), false, g_settings.lastdir);
			}
			if (ImGui::DragFloat(get_localized_string(LANG_0719).c_str(), &fontSize, 0.1f, 8, 48, get_localized_string(LANG_0720).c_str()))
			{
				shouldReloadFonts = true;
			}
			ImGui::DragInt(get_localized_string(LANG_0721).c_str(), &g_settings.undoLevels, 0.05f, 0, 64);
#ifndef NDEBUG
			ImGui::BeginDisabled();
#endif
			ImGui::Checkbox(get_localized_string(LANG_0722).c_str(), &g_settings.verboseLogs);
#ifndef NDEBUG
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0723).c_str());
				ImGui::EndTooltip();
			}
#endif
			ImGui::SameLine();

			ImGui::Checkbox(get_localized_string(LANG_0724).c_str(), &g_settings.savebackup);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0725).c_str());
				ImGui::EndTooltip();
			}

			ImGui::Checkbox(get_localized_string(LANG_0726).c_str(), &g_settings.save_crc);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0727).c_str());
				ImGui::EndTooltip();
			}

			ImGui::SameLine();

			ImGui::Checkbox(get_localized_string(LANG_0728).c_str(), &g_settings.auto_import_ent);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0729).c_str());
				ImGui::EndTooltip();
			}

			ImGui::Checkbox(get_localized_string(LANG_0730).c_str(), &g_settings.same_dir_for_ent);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0731).c_str());
				ImGui::EndTooltip();
			}

			ImGui::SameLine();

			if (ImGui::Checkbox(get_localized_string(LANG_0732).c_str(), &g_settings.save_windows))
			{
				imgui_io->IniFilename = !g_settings.save_windows ? NULL : g_settings_path.c_str();
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0733).c_str());
				ImGui::EndTooltip();
			}

			ImGui::Checkbox(get_localized_string(LANG_0734).c_str(), &g_settings.default_is_empty);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0735).c_str());
				ImGui::TextUnformatted(get_localized_string(LANG_0736).c_str());
				ImGui::EndTooltip();
			}

			ImGui::SameLine();

			ImGui::Checkbox(get_localized_string(LANG_0737).c_str(), &g_settings.start_at_entity);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0738).c_str());
				ImGui::EndTooltip();
			}

			ImGui::Checkbox("Save map cam pos", &g_settings.save_cam);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Save camera position to map and load it at open.");
				ImGui::EndTooltip();
			}

			ImGui::Separator();
			ImGui::TextUnformatted("Language:");
			ImGui::SameLine();
			if (ImGui::BeginCombo("##lang", langForSelect.c_str()))
			{
				for (const auto& s : g_settings.languages)
				{
					if (ImGui::Selectable(s.c_str(), s == langForSelect))
					{
						langForSelect = s;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::Separator();
			ImGui::TextUnformatted("Palette:");
			ImGui::SameLine();
			if (ImGui::BeginCombo("##pal", palForSelect.c_str()))
			{
				for (const auto& s : g_settings.palettes)
				{
					if (ImGui::Selectable(s.name.c_str(), s.name == palForSelect))
					{
						palForSelect = s.name;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::Separator();

			ImGui::TextUnformatted("RAD Executable:");
			ImGui::SetNextItemWidth(pathWidth * 0.80f);
			ImGui::InputText("##hl_rad", &g_settings.rad_path);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay && g_settings.rad_path.size())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(g_settings.rad_path.c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}

			ImGui::SetNextItemWidth(delWidth);
			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_5 = ImGui::Button("...##hlrad_path");
			ImGui::PopStyleColor(1);
			if (clicked_5)
			{
				ifd::FileDialog::Instance().Open("radPath", "Select rad executable path", "*.*", false, g_settings.lastdir);
			}

			ImGui::Text("RAD options:");
			ImGui::SetNextItemWidth(pathWidth);
			ImGui::InputText("##hlrad_options", &g_settings.rad_options);

			ImGui::Separator();

			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_6 = ImGui::Button(get_localized_string(LANG_0739).c_str());
			ImGui::PopStyleColor(1);
			if (clicked_6)
			{
				g_settings.loadDefaultSettings();
				;
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0740).c_str());
				ImGui::EndTooltip();
			}
		}
		else if (settingsTab == 1)
		{
			for (size_t i = 0; i < g_settings.fgdPaths.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth * 0.20f);
				ImGui::Checkbox((std::string("##enablefgd") + std::to_string(i)).c_str(), &g_settings.fgdPaths[i].enabled);

				ImGui::SameLine();

				ImGui::SetNextItemWidth(pathWidth * 0.80f);
				ImGui::InputText(("##fgd" + std::to_string(i)).c_str(), &g_settings.fgdPaths[i].path);
				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay && g_settings.fgdPaths[i].path.size())
				{
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
					ImGui::TextUnformatted(g_settings.fgdPaths[i].path.c_str());
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
				bool clicked_7 = ImGui::Button(("...##fgdOpen" + std::to_string(i)).c_str());
				ImGui::PopStyleColor(1);
				if (clicked_7)
				{
					fgdSelected = (int)i;
					ifd::FileDialog::Instance().Open("fgdOpen", "Select fgd path", "fgd file (*.fgd){.fgd},.*", false, g_settings.lastdir);
				}

				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, COLOR_BLOOD_CRIMSON);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.12f, 0.22f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.15f, 0.28f, 1.0f));
				if (ImGui::Button((" X ##del_fgd" + std::to_string(i)).c_str()))
				{
					g_settings.fgdPaths.erase(g_settings.fgdPaths.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_8 = ImGui::Button(get_localized_string(LANG_0741).c_str());
			ImGui::PopStyleColor(1);
			if (clicked_8)
			{
				g_settings.fgdPaths.emplace_back(std::string(), true);
			}
		}
		else if (settingsTab == 2)
		{
			for (size_t i = 0; i < g_settings.resPaths.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth * 0.20f);
				ImGui::Checkbox((std::string("##enableres") + std::to_string(i)).c_str(), &g_settings.resPaths[i].enabled);

				ImGui::SameLine();

				ImGui::SetNextItemWidth(pathWidth * 0.80f);
				ImGui::InputText(("##res" + std::to_string(i)).c_str(), &g_settings.resPaths[i].path);
				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay && g_settings.resPaths[i].path.size())
				{
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
					ImGui::TextUnformatted(g_settings.resPaths[i].path.c_str());
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
				bool clicked_9 = ImGui::Button(("...##resOpen" + std::to_string(i)).c_str());
				ImGui::PopStyleColor(1);
				if (clicked_9)
				{
					resSelected = (int)i;
					ifd::FileDialog::Instance().Open("resOpen", "Select fgd path", std::string(), false, g_settings.lastdir);
				}

				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, COLOR_BLOOD_CRIMSON);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.12f, 0.22f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.15f, 0.28f, 1.0f));
				if (ImGui::Button((" X ##del_res" + std::to_string(i)).c_str()))
				{
					g_settings.resPaths.erase(g_settings.resPaths.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_10 = ImGui::Button(get_localized_string(LANG_0742).c_str());
			ImGui::PopStyleColor(1);
			if (clicked_10)
			{
				g_settings.resPaths.emplace_back(std::string(), true);
			}
		}
		else if (settingsTab == 3)
		{
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::Checkbox(get_localized_string(LANG_0743).c_str(), &g_settings.strip_wad_path);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0744).c_str());
				ImGui::EndTooltip();
			}
			ImGui::SameLine();

			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::Checkbox(get_localized_string(LANG_0745).c_str(), &g_settings.mark_unused_texinfos);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0746).c_str());
				ImGui::EndTooltip();
			}
			ImGui::Separator();

			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::Checkbox(get_localized_string(LANG_0747).c_str(), &g_settings.merge_verts);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0748).c_str());
				ImGui::EndTooltip();
			}

			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::Checkbox("Merge edges [WIP]", &g_settings.merge_edges);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Warning! This option can add visual glitches to map.");
				ImGui::EndTooltip();
			}

			ImGui::SetNextItemWidth(pathWidth);
			ImGui::Text(get_localized_string(LANG_0749).c_str());

			for (size_t i = 0; i < g_settings.conditionalPointEntTriggers.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##pointent" + std::to_string(i)).c_str(), &g_settings.conditionalPointEntTriggers[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, COLOR_BLOOD_CRIMSON);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.12f, 0.22f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.15f, 0.28f, 1.0f));
				if (ImGui::Button((" X ##pointent" + std::to_string(i)).c_str()))
				{
					g_settings.conditionalPointEntTriggers.erase(g_settings.conditionalPointEntTriggers.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_11 = ImGui::Button(get_localized_string(LANG_0750).c_str());
			ImGui::PopStyleColor(1);
			if (clicked_11)
			{
				g_settings.conditionalPointEntTriggers.emplace_back(std::string());
			}
			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0751).c_str());

			for (size_t i = 0; i < g_settings.entsThatNeverNeedAnyHulls.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##entnohull" + std::to_string(i)).c_str(), &g_settings.entsThatNeverNeedAnyHulls[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, COLOR_BLOOD_CRIMSON);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.12f, 0.22f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.15f, 0.28f, 1.0f));
				if (ImGui::Button((" X ##entnohull" + std::to_string(i)).c_str()))
				{
					g_settings.entsThatNeverNeedAnyHulls.erase(g_settings.entsThatNeverNeedAnyHulls.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_12 = ImGui::Button(get_localized_string(LANG_0752).c_str());
			ImGui::PopStyleColor(1);
			if (clicked_12)
			{
				g_settings.entsThatNeverNeedAnyHulls.emplace_back(std::string());
			}
			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0753).c_str());

			for (size_t i = 0; i < g_settings.entsThatNeverNeedCollision.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##entnocoll" + std::to_string(i)).c_str(), &g_settings.entsThatNeverNeedCollision[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, COLOR_BLOOD_CRIMSON);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.12f, 0.22f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.15f, 0.28f, 1.0f));
				if (ImGui::Button((" X ##entnocoll" + std::to_string(i)).c_str()))
				{
					g_settings.entsThatNeverNeedCollision.erase(g_settings.entsThatNeverNeedCollision.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_13 = ImGui::Button(get_localized_string(LANG_0754).c_str());
			ImGui::PopStyleColor(1);
			if (clicked_13)
			{
				g_settings.entsThatNeverNeedCollision.emplace_back(std::string());
			}
			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0755).c_str());

			for (size_t i = 0; i < g_settings.passableEnts.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##entpass" + std::to_string(i)).c_str(), &g_settings.passableEnts[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, COLOR_BLOOD_CRIMSON);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.12f, 0.22f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.15f, 0.28f, 1.0f));
				if (ImGui::Button((" X ##entpass" + std::to_string(i)).c_str()))
				{
					g_settings.passableEnts.erase(g_settings.passableEnts.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_14 = ImGui::Button(get_localized_string(LANG_0756).c_str());
			ImGui::PopStyleColor(1);
			if (clicked_14)
			{
				g_settings.passableEnts.emplace_back(std::string());
			}
			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0757).c_str());

			for (size_t i = 0; i < g_settings.playerOnlyTriggers.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##entpltrigg" + std::to_string(i)).c_str(), &g_settings.playerOnlyTriggers[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, COLOR_BLOOD_CRIMSON);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.12f, 0.22f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.15f, 0.28f, 1.0f));
				if (ImGui::Button((" X ##entpltrigg" + std::to_string(i)).c_str()))
				{
					g_settings.playerOnlyTriggers.erase(g_settings.playerOnlyTriggers.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_15 = ImGui::Button(get_localized_string(LANG_0758).c_str());
			ImGui::PopStyleColor(1);
			if (clicked_15)
			{
				g_settings.playerOnlyTriggers.emplace_back(std::string());
			}
			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0759).c_str());

			for (size_t i = 0; i < g_settings.monsterOnlyTriggers.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##entmonsterrigg" + std::to_string(i)).c_str(), &g_settings.monsterOnlyTriggers[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, COLOR_BLOOD_CRIMSON);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.12f, 0.22f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.15f, 0.28f, 1.0f));
				if (ImGui::Button((" X ##entmonsterrigg" + std::to_string(i)).c_str()))
				{
					g_settings.monsterOnlyTriggers.erase(g_settings.monsterOnlyTriggers.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_16 = ImGui::Button(get_localized_string(LANG_0760).c_str());
			ImGui::PopStyleColor(1);
			if (clicked_16)
			{
				g_settings.monsterOnlyTriggers.emplace_back(std::string());
			}
		}
		else if (settingsTab == 4)
		{
			if (ImGui::BeginCombo("##engines", engForSelect.c_str()))
			{
				for (const auto& s : limitsMap)
				{
					if (ImGui::Selectable(s.first.c_str(), s.first == engForSelect))
					{
						engForSelect = s.first;

						try
						{
							g_limits = limitsMap[engForSelect];
						}
						catch (...)
						{
							engForSelect = g_limits.engineName;
						}
					}
				}
				ImGui::EndCombo();
			}
			ImGui::Separator();

			ImGui::SetNextItemWidth(pathWidth / 2);
			static unsigned int vis_data_count = g_limits.maxMapVisdata / (1024 * 1024);
			static unsigned int light_data_count = g_limits.maxMapLightdata / (1024 * 1024);

			ImGui::DragFloat(get_localized_string(LANG_0761).c_str(), &g_limits.fltMaxCoord, 64.f, 512.f, 2147483647.f, "%.0f");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0762).c_str(), (int*)&g_limits.maxMapModels, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt("MAX SURFACE EXTENTS", (int*)&g_limits.maxSurfaceExtent, 1, 4, 1024, "%i");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0765).c_str(), (int*)&g_limits.maxMapNodes, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0766).c_str(), (int*)&g_limits.maxMapClipnodes, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0767).c_str(), (int*)&g_limits.maxMapLeaves, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			if (ImGui::DragInt(get_localized_string(LANG_0768).c_str(), (int*)&vis_data_count, 4, 128, 2147483647, get_localized_string(LANG_0769).c_str()))
			{
				g_limits.maxMapVisdata = vis_data_count * (1024 * 1024);
			}
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0763).c_str(), (int*)&g_limits.maxMapEnts, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0771).c_str(), (int*)&g_limits.maxMapSurfedges, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0770).c_str(), (int*)&g_limits.maxMapEdges, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0764).c_str(), (int*)&g_limits.maxMapTextures, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			if (ImGui::DragInt(get_localized_string(LANG_0772).c_str(), (int*)&light_data_count, 4, 128, 2147483647, get_localized_string(LANG_0769).c_str()))
			{
				g_limits.maxMapLightdata = light_data_count * (1024 * 1024);
			}
			ImGui::SetNextItemWidth(pathWidth / 2);
			if (ImGui::DragInt(get_localized_string(LANG_0773).c_str(), (int*)&g_limits.maxTextureDimension, 4, 32, 1048576, "%u"))
			{
				g_limits.maxTextureSize = ((g_limits.maxTextureDimension * g_limits.maxTextureDimension * 2 * 3) / 2);
			}
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragFloat("MAX_MAP_BOUNDARY", &g_limits.maxMapBoundary, 64.f, 512.f, 2147483647.f, "%.0f");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0774).c_str(), (int*)&g_limits.textureStep, 4, 4, 512, "%u");

			ImGui::SetNextItemWidth(pathWidth / 2);
			static std::string newEngine = "engine-name";
			ImGui::InputText("##engine-name", &newEngine);
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_17 = ImGui::Button("Add##NEW ENGINE");
			ImGui::PopStyleColor(1);
			if (clicked_17)
			{
				limitsMap[g_limits.engineName] = g_limits;
				engForSelect = newEngine;
				g_limits.engineName = newEngine;
				limitsMap[newEngine] = g_limits;
			}

			if (prevLimits != g_limits)
			{
				prevLimits = g_limits;
				limitsMap[g_limits.engineName] = g_limits;
			}
		}
		else if (settingsTab == 5)
		{
			ImGui::Text(get_localized_string(LANG_0775).c_str());
			ImGui::Checkbox(get_localized_string(LANG_1115).c_str(), &g_settings.vsync);
			if (!g_settings.vsync)
			{
				ImGui::SameLine();
				if (ImGui::DragInt("FPS LIMIT", &g_settings.fpslimit, 5, 30, 1000, "%u"))
				{
					if (g_settings.fpslimit > 2000)
						g_settings.fpslimit = 2000;
				}
				if (g_settings.fpslimit < 15)
					g_settings.fpslimit = 15;
			}
			ImGui::DragFloat(get_localized_string(LANG_0776).c_str(), &app->fov, 0.1f, 1.0f, 150.0f, get_localized_string(LANG_0777).c_str());
			ImGui::DragFloat(get_localized_string(LANG_0778).c_str(), &app->zFar, 10.0f, -g_limits.fltMaxCoord, g_limits.fltMaxCoord, "%.0f", ImGuiSliderFlags_Logarithmic);
			ImGui::Separator();

			bool renderTextures = g_render_flags & RENDER_TEXTURES;
			bool renderTexturesFilter = !(g_render_flags & RENDER_TEXTURES_NOFILTER);
			bool renderLightmapsFilter = !(g_render_flags & RENDER_LIGHTMAPS_NOFILTER);
			bool renderLightmaps = g_render_flags & RENDER_LIGHTMAPS;
			bool renderWireframe = g_render_flags & RENDER_WIREFRAME;
			bool renderEntities = g_render_flags & RENDER_ENTS;
			bool renderSpecial = g_render_flags & RENDER_SPECIAL;
			bool renderSpecialEnts = g_render_flags & RENDER_SPECIAL_ENTS;
			bool renderPointEnts = g_render_flags & RENDER_POINT_ENTS;
			bool renderOrigin = g_render_flags & RENDER_ORIGIN;
			bool renderWorldClipnodes = g_render_flags & RENDER_WORLD_CLIPNODES;
			bool renderEntClipnodes = g_render_flags & RENDER_ENT_CLIPNODES;
			bool renderEntConnections = g_render_flags & RENDER_ENT_CONNECTIONS;
			bool transparentNodes = g_render_flags & RENDER_TRANSPARENT;
			bool renderModels = g_render_flags & RENDER_MODELS;
			bool renderAnimatedModels = g_render_flags & RENDER_MODELS_ANIMATED;
			bool renderSelectedAtTop = g_render_flags & RENDER_SELECTED_AT_TOP;
			bool renderMapBoundary = g_render_flags & RENDER_MAP_BOUNDARY;

			ImGui::Text(get_localized_string(LANG_0779).c_str());

			ImGui::Columns(2, 0, false);

			if (ImGui::Checkbox(get_localized_string(LANG_0780).c_str(), &renderTextures))
			{
				g_render_flags ^= RENDER_TEXTURES;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_1205).c_str(), &renderTexturesFilter))
			{
				g_render_flags ^= RENDER_TEXTURES_NOFILTER;
				{
					std::lock_guard<std::mutex> lock(Sync::TexturesList);
					for (auto& tex : g_all_Textures)
					{
						bool filternoneed = g_render_flags & RENDER_TEXTURES_NOFILTER;
						if (tex->type >= 0 && tex->type != Texture::TYPE_LIGHTMAP && tex->type != Texture::TYPE_LIGHTMAP_NOFILTER)
						{
							tex->farFilter = tex->nearFilter = !filternoneed ? GL_LINEAR : GL_NEAREST;
							tex->upload(tex->type);
						}
					}
				}
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0781).c_str(), &renderLightmaps))
			{
				g_render_flags ^= RENDER_LIGHTMAPS;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_1206).c_str(), &renderLightmapsFilter))
			{
				g_render_flags ^= RENDER_LIGHTMAPS_NOFILTER;
				{
					std::lock_guard<std::mutex> lock(Sync::TexturesList);
					for (auto& tex : g_all_Textures)
					{
						if (tex->type == Texture::TYPE_LIGHTMAP)
						{
							tex->upload(tex->type);
						}
					}
				}
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0782).c_str(), &renderWireframe))
			{
				g_render_flags ^= RENDER_WIREFRAME;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_1116).c_str(), &renderOrigin))
			{
				g_render_flags ^= RENDER_ORIGIN;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0783).c_str(), &renderEntConnections))
			{
				g_render_flags ^= RENDER_ENT_CONNECTIONS;
				if (g_render_flags & RENDER_ENT_CONNECTIONS)
				{
					app->updateEntConnections();
				}
			}

			if (ImGui::Checkbox(get_localized_string(LANG_0784).c_str(), &renderPointEnts))
			{
				g_render_flags ^= RENDER_POINT_ENTS;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0785).c_str(), &renderEntities))
			{
				g_render_flags ^= RENDER_ENTS;
			}

			ImGui::NextColumn();
			if (ImGui::Checkbox(get_localized_string(LANG_0786).c_str(), &renderSpecialEnts))
			{
				g_render_flags ^= RENDER_SPECIAL_ENTS;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0787).c_str(), &renderSpecial))
			{
				g_render_flags ^= RENDER_SPECIAL;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0788).c_str(), &renderModels))
			{
				g_render_flags ^= RENDER_MODELS;
			}

			if (ImGui::Checkbox(get_localized_string(LANG_0789).c_str(), &renderAnimatedModels))
			{
				g_render_flags ^= RENDER_MODELS_ANIMATED;
			}

			if (ImGui::Checkbox("Selected at top", &renderSelectedAtTop))
			{
				g_render_flags ^= RENDER_SELECTED_AT_TOP;
			}

			if (ImGui::Checkbox(get_localized_string(LANG_0790).c_str(), &renderWorldClipnodes))
			{
				g_render_flags ^= RENDER_WORLD_CLIPNODES;
			}

			if (ImGui::Checkbox(get_localized_string(LANG_0791).c_str(), &renderEntClipnodes))
			{
				g_render_flags ^= RENDER_ENT_CLIPNODES;
			}

			if (ImGui::Checkbox(get_localized_string(LANG_0792).c_str(), &transparentNodes))
			{
				g_render_flags ^= RENDER_TRANSPARENT;
				for (size_t i = 0; i < mapRenderers.size(); i++)
				{
					mapRenderers[i]->updateClipnodeOpacity(transparentNodes ? 128 : 255);
				}
			}

			if (ImGui::Checkbox("Map boundary", &renderMapBoundary))
			{
				g_render_flags ^= RENDER_MAP_BOUNDARY;
			}

			bool renderSkybox = (g_render_flags & RENDER_SKYBOX) != 0;
			if (ImGui::Checkbox("Render Skybox", &renderSkybox))
			{
				g_render_flags ^= RENDER_SKYBOX;
			}

			ImGui::Columns(1);

			ImGui::Separator();

			float mapBoundCol[3] = {g_settings.mapBoundaryColor.r / 255.0f, g_settings.mapBoundaryColor.g / 255.0f, g_settings.mapBoundaryColor.b / 255.0f};
			if (ImGui::ColorEdit3("Map boundary color", mapBoundCol))
			{
				g_settings.mapBoundaryColor.r = (unsigned char)(mapBoundCol[0] * 255.0f);
				g_settings.mapBoundaryColor.g = (unsigned char)(mapBoundCol[1] * 255.0f);
				g_settings.mapBoundaryColor.b = (unsigned char)(mapBoundCol[2] * 255.0f);
				g_settings_changed = true;
			}
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_18 = ImGui::Button("Reset##resetMapBoundCol");
			ImGui::PopStyleColor(1);
			if (clicked_18)
			{
				g_settings.mapBoundaryColor = COLOR3(0, 255, 0);
				g_settings_changed = true;
			}

			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0793).c_str());

			for (size_t i = 0; i < g_settings.transparentTextures.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##transTex" + std::to_string(i)).c_str(), &g_settings.transparentTextures[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, COLOR_BLOOD_CRIMSON);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.12f, 0.22f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.15f, 0.28f, 1.0f));
				if (ImGui::Button((" X ##transTex" + std::to_string(i)).c_str()))
				{
					g_settings.transparentTextures.erase(g_settings.transparentTextures.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_19 = ImGui::Button(get_localized_string(LANG_0794).c_str());
			ImGui::PopStyleColor(1);
			if (clicked_19)
			{
				g_settings.transparentTextures.emplace_back(std::string());
			}

			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0795).c_str());

			for (size_t i = 0; i < g_settings.transparentEntities.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##transEnt" + std::to_string(i)).c_str(), &g_settings.transparentEntities[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, COLOR_BLOOD_CRIMSON);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.12f, 0.22f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.15f, 0.28f, 1.0f));
				if (ImGui::Button((" X ##transEnt" + std::to_string(i)).c_str()))
				{
					g_settings.transparentEntities.erase(g_settings.transparentEntities.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_20 = ImGui::Button(get_localized_string(LANG_0796).c_str());
			ImGui::PopStyleColor(1);
			if (clicked_20)
			{
				g_settings.transparentEntities.emplace_back(std::string());
			}

			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0797).c_str());

			for (size_t i = 0; i < g_settings.entsNegativePitchPrefix.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##invPitch" + std::to_string(i)).c_str(), &g_settings.entsNegativePitchPrefix[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, COLOR_BLOOD_CRIMSON);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.12f, 0.22f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.15f, 0.28f, 1.0f));
				if (ImGui::Button((" X ##invPitch" + std::to_string(i)).c_str()))
				{
					g_settings.entsNegativePitchPrefix.erase(g_settings.entsNegativePitchPrefix.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_21 = ImGui::Button(get_localized_string(LANG_0798).c_str());
			ImGui::PopStyleColor(1);
			if (clicked_21)
			{
				g_settings.entsNegativePitchPrefix.emplace_back(std::string());
			}
		}
		else if (settingsTab == 6)
		{
			ImGui::DragFloat(get_localized_string(LANG_0799).c_str(), &app->moveSpeed, 1.0f, 100.0f, 1000.0f, "%.1f");
			ImGui::DragFloat(get_localized_string(LANG_0800).c_str(), &app->rotationSpeed, 0.1f, 0.1f, 100.0f, "%.1f");
		}

		ImGui::EndChild();
		ImGui::EndChild();

		ImGui::EndGroup();
	}
	ImGui::End();

	if ((oldShowSettings && !showSettingsWidget) || apply_settings_pressed)
	{
		g_settings.selected_lang = langForSelect;
		g_settings.palette_name = palForSelect;
		set_localize_lang(g_settings.selected_lang);

		g_settings.saveSettings();
		if (!app->reloading)
		{
			app->reloading = true;
			app->loadFgds();
			app->postLoadFgds();
			for (size_t i = 0; i < mapRenderers.size(); i++)
			{
				BspRenderer* mapRender = mapRenderers[i];
				mapRender->reload();
			}
			app->reloading = false;
		}
		oldShowSettings = showSettingsWidget = apply_settings_pressed;
	}
}

void Gui::drawHelp()
{
	ImGui::SetNextWindowSize(ImVec2(600.f, 400.f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(fmt::format("{}###HELP_WIDGET", get_localized_string(LANG_1117)).c_str(), &showHelpWidget))
	{
		if (ImGui::BeginTabBar(get_localized_string(LANG_1118).c_str()))
		{
			if (ImGui::BeginTabItem(get_localized_string(LANG_0801).c_str()))
			{
				ImGui::Dummy(ImVec2(0, 10));

				// user guide from the demo
				ImGui::BulletText(get_localized_string(LANG_0802).c_str());
				ImGui::BulletText(get_localized_string(LANG_0803).c_str());
				ImGui::Indent();
				ImGui::BulletText(get_localized_string(LANG_0804).c_str());
				ImGui::BulletText(get_localized_string(LANG_0805).c_str());
				ImGui::Unindent();
				ImGui::BulletText(get_localized_string(LANG_0806).c_str());
				ImGui::Indent();
				ImGui::BulletText(get_localized_string(LANG_0807).c_str());
				ImGui::BulletText(get_localized_string(LANG_0808).c_str());
				ImGui::BulletText(get_localized_string(LANG_0809).c_str());
				ImGui::BulletText(get_localized_string(LANG_0810).c_str());
				ImGui::Unindent();

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(get_localized_string(LANG_0811).c_str()))
			{
				ImGui::Dummy(ImVec2(0, 10));
				ImGui::BulletText(get_localized_string(LANG_0812).c_str());
				ImGui::BulletText(get_localized_string(LANG_0813).c_str());
				ImGui::BulletText(get_localized_string(LANG_0814).c_str());
				ImGui::BulletText("Press CTRL+ALT+A to select all faces same texture.");
				ImGui::BulletText(get_localized_string(LANG_0815).c_str());
				ImGui::Indent();
				ImGui::BulletText(get_localized_string(LANG_0816).c_str());
				ImGui::BulletText(get_localized_string(LANG_0817).c_str());
				ImGui::Unindent();
				ImGui::BulletText(get_localized_string(LANG_0818).c_str());
				ImGui::Indent();
				ImGui::BulletText(get_localized_string(LANG_0819).c_str());
				ImGui::Unindent();

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(get_localized_string(LANG_0820).c_str()))
			{
				ImGui::Dummy(ImVec2(0, 10));
				ImGui::BulletText(get_localized_string(LANG_0821).c_str());
				ImGui::Unindent();

				ImGui::EndTabItem();
			}
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

void Gui::drawAbout()
{
	ImGui::SetNextWindowSize(ImVec2(650.f, 160.f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(fmt::format("{}###ABOUT_WIDGET", get_localized_string(LANG_1119)).c_str(), &showAboutWidget))
	{
		ImGui::InputText(get_localized_string(LANG_0822).c_str(), &g_version_string, ImGuiInputTextFlags_ReadOnly);

		static char author[] = "w00tguy(bspguy), karaulov(newbspguy)";
		ImGui::InputText(get_localized_string(LANG_0823).c_str(), author, strlen(author), ImGuiInputTextFlags_ReadOnly);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(author);
			ImGui::EndTooltip();
		}

		static char url[] = "https://github.com/wootguy/bspguy";
		ImGui::InputText(get_localized_string(LANG_0824).c_str(), url, strlen(url), ImGuiInputTextFlags_ReadOnly);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(url);
			ImGui::EndTooltip();
		}

		static char url2[] = "https://github.com/UnrealKaraulov/newbspguy";
		ImGui::InputText((get_localized_string(LANG_0824) + "##2").c_str(), url2, strlen(url2), ImGuiInputTextFlags_ReadOnly);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(url2);
			ImGui::EndTooltip();
		}

		static char url3[] = "https://github.com/urgorri/revamped-newbspguy";
		ImGui::InputText((get_localized_string(LANG_0824) + "##3").c_str(), url3, strlen(url3), ImGuiInputTextFlags_ReadOnly);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(url3);
			ImGui::EndTooltip();
		}

		static char help1[] = "https://github.com/Qwertyus3D\nhttps://t.me/AKG6669\nhttps://hlfx.ru/forum/member.php?action=getinfo&userid=3\ntwhl community\netc";
		ImGui::InputTextMultiline("Special thanks to:", help1, strlen(help1), ImVec2(0, 45), ImGuiInputTextFlags_ReadOnly);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(help1);
			ImGui::EndTooltip();
		}

		static char bad1[] = "Empty";
		ImGui::InputText("Very bad objects:", bad1, strlen(bad1), ImGuiInputTextFlags_ReadOnly);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(bad1);
			ImGui::EndTooltip();
		}
	}

	ImGui::End();
}

void Gui::drawMergeWindow()
{
	ImGui::SetNextWindowSize(ImVec2(1000.f, 250.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(1000.f, 250.f), ImVec2(1200.f, 500.f));
	static std::string outPath = "outbsp.bsp";
	static std::vector<std::string> inPaths;
	static std::vector<vec3> inOffsets;
	static bool DeleteUnusedInfo = true;
	static bool Optimize = false;
	static bool DeleteHull2 = false;
	static bool NoRipent = false;
	static bool NoStyles = false;
	static bool NoScript = false;

	bool addNew = false;

	static int select_path = 0;

	if (inPaths.size() < 1)
	{
		inPaths.emplace_back("");
		inOffsets.emplace_back(vec3());
	}

	if (inOffsets.size() != inPaths.size())
	{
		inOffsets.resize(inPaths.size());
	}

	if (ImGui::Begin(fmt::format("{}###MERGE_WIDGET", get_localized_string(LANG_0825)).c_str(), &showMergeMapWidget))
	{
		if (ifd::FileDialog::Instance().IsDone("BspMergeDialog"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				inPaths[select_path] = res.string();
				g_settings.lastdir = stripFileName(res.string());
			}
			ifd::FileDialog::Instance().Close();
		}

		if (ImGui::Button("Add Open Maps"))
		{
			for (size_t k = 0; k < mapRenderers.size(); k++)
			{
				if (mapRenderers[k] && mapRenderers[k]->map && !mapRenderers[k]->map->is_bsp_model)
				{
					std::string path = mapRenderers[k]->map->bsp_path;
					if (path.size())
					{
						bool exists = false;
						for (auto& existing : inPaths)
						{
							if (existing == path)
							{
								exists = true;
								break;
							}
						}
						if (!exists)
						{
							if (inPaths.size() == 1 && inPaths[0].empty())
							{
								inPaths[0] = path;
							}
							else
							{
								inPaths.push_back(path);
								inOffsets.push_back(vec3());
							}
						}
					}
				}
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Add Row"))
		{
			inPaths.push_back("");
			inOffsets.push_back(vec3());
		}
		ImGui::SameLine();
		if (ImGui::Button("Stack Vertically (512 Z)"))
		{
			for (size_t k = 0; k < inOffsets.size(); k++)
			{
				inOffsets[k] = vec3(0.0f, 0.0f, 512.0f * (float)k);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear All"))
		{
			inPaths.clear();
			inPaths.push_back("");
			inOffsets.clear();
			inOffsets.push_back(vec3());
		}
		ImGui::Spacing();

		for (size_t i = 0; i < inPaths.size(); i++)
		{
			std::string& s = inPaths[i];
			ImGui::SetNextItemWidth(350);
			ImGui::InputText(fmt::format(fmt::runtime("##inpath{}"), i).c_str(), &s);
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool clicked_22 = ImGui::Button((get_localized_string(LANG_0834) + "##" + std::to_string(i)).c_str());
			ImGui::PopStyleColor(1);
			if (clicked_22)
			{
				select_path = (int)i;
				ifd::FileDialog::Instance().Open("BspMergeDialog", "Opep bsp model", "BSP file (*.bsp){.bsp},.*", false, g_settings.lastdir);
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(fmt::format(fmt::runtime(get_localized_string(LANG_0826)), i).c_str());

			ImGui::SameLine();
			ImGui::SetNextItemWidth(250);
			ImGui::InputFloat3(fmt::format("##offset{}", i).c_str(), &inOffsets[i].x);

			if (inPaths.size() > 1)
			{
				ImGui::SameLine();
				if (ImGui::Button(fmt::format("X##delrow{}", i).c_str()))
				{
					inPaths.erase(inPaths.begin() + i);
					inOffsets.erase(inOffsets.begin() + i);
					break;
				}
			}

			if (s.length() > 1 && i + 1 == inPaths.size())
			{
				addNew = true;
			}
		}

		ImGui::SetNextItemWidth(280);
		ImGui::InputText(get_localized_string(LANG_0828).c_str(), &outPath);

		ImGui::Checkbox(get_localized_string(LANG_0829).c_str(), &DeleteUnusedInfo);
		ImGui::Checkbox(get_localized_string(LANG_1121).c_str(), &Optimize);
		ImGui::Checkbox(get_localized_string(LANG_0830).c_str(), &DeleteHull2);
		ImGui::Checkbox(get_localized_string(LANG_0831).c_str(), &NoRipent);
		ImGui::Checkbox(get_localized_string(LANG_0832).c_str(), &NoScript);
		ImGui::Checkbox("Skip lightstyles merging", &NoStyles);

		ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
		bool clicked_23 = ImGui::Button(get_localized_string(LANG_1122).c_str(), ImVec2(120, 0));
		ImGui::PopStyleColor(1);
		ImGui::SameLine();
		if (ImGui::Button("Close", ImVec2(100, 0)))
		{
			showMergeMapWidget = false;
		}
		if (clicked_23)
		{
			std::vector<Bsp*> maps;
			std::vector<vec3> mapsOffsets;
			for (size_t i = 0; i < inPaths.size(); i++)
			{
				if (inPaths[i].size())
				{
					if (fileExists(inPaths[i]))
					{
						Bsp* tmpMap = new Bsp(inPaths[i]);
						if (tmpMap->bsp_valid)
						{
							maps.push_back(tmpMap);
							mapsOffsets.push_back(inOffsets[i]);
						}
						else
						{
							delete tmpMap;
						}
					}
				}
			}
			if (maps.size() < 2)
			{
				for (auto& map : maps)
					delete map;
				maps.clear();
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1056));
			}
			else
			{
				for (size_t i = 0; i < maps.size(); i++)
				{
					print_log(get_localized_string(LANG_1057), maps[i]->bsp_name);
					if (DeleteUnusedInfo)
					{
						print_log(get_localized_string(LANG_1058));
						STRUCTCOUNT removed = maps[i]->remove_unused_model_structures();
						g_progress.clear();
						g_progress = ProgressMeter();
						removed.print_delete_stats(2);
					}

					if (DeleteHull2 || (Optimize && !maps[i]->has_hull2_ents()))
					{
						print_log(get_localized_string(LANG_1059));
						maps[i]->delete_hull(2, 1);
						maps[i]->remove_unused_model_structures().print_delete_stats(2);
					}

					if (Optimize)
					{
						print_log(get_localized_string(LANG_1060));
						maps[i]->delete_unused_hulls().print_delete_stats(2);
					}

					print_log("\n");
				}
				BspMerger merger;

				std::string finalOutPath = g_working_dir + "merged_maps/" + outPath;
				createDir(g_working_dir + "merged_maps/");

				std::string finalEntPath = g_working_dir + "exported_entities/" + stripExt(basename(outPath));
				createDir(g_working_dir + "exported_entities/");
				// Pass empty string for output_name to BspMerger::merge to avoid duplicate/misplaced entity export
				MergeResult result = merger.merge(maps, "", NoRipent, NoScript, NoStyles, mapsOffsets);

				print_log("\n");
				if (result.map && result.map->isValid())
				{
					result.map->write(finalOutPath);
					if (!NoScript)
					{
						result.map->export_entities(finalEntPath + ".ent");
					}
					print_log("\n");
					result.map->print_info(false, 0, 0);

					app->clearMaps();

					fixupPath(finalOutPath, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP);

					if (fileExists(finalOutPath))
					{
						app->addMap(new Bsp(finalOutPath));
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0398));
						app->addMap(new Bsp());
					}
				}

				for (auto& map : maps)
					delete map;
				maps.clear();
			}
			showMergeMapWidget = false;
		}
	}

	ImGui::End();

	if (addNew)
	{
		inPaths.emplace_back(std::string(""));
		inOffsets.emplace_back(vec3());
	}
}

void Gui::drawShiftMapDialog()
{
	ImGui::SetNextWindowSize(ImVec2(480.f, 230.f), ImGuiCond_FirstUseEver);
	Bsp* map = app->getSelectedMap();
	BspRenderer* rend = map ? map->getBspRender() : NULL;

	if (ImGui::Begin("Shift / Move Map (X, Y, Z)###SHIFT_MAP_DIALOG", &showShiftMapDialog))
	{
		if (!map || map->is_mdl_model)
		{
			ImGui::TextDisabled("No active BSP map loaded.");
		}
		else
		{
			ImGui::Text("Translate all map geometry and entities by delta offset:");
			ImGui::Spacing();
			ImGui::SetNextItemWidth(350);
			ImGui::InputFloat3("Offset (X, Y, Z)", &shiftMapDelta.x);

			ImGui::Spacing();
			if (ImGui::Button("+512 Z (Stack Above)"))
			{
				shiftMapDelta.z += 512.0f;
			}
			ImGui::SameLine();
			if (ImGui::Button("-512 Z (Stack Below)"))
			{
				shiftMapDelta.z -= 512.0f;
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset to Zero"))
			{
				shiftMapDelta = vec3(0.0f, 0.0f, 0.0f);
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			if (ImGui::Button("Apply Shift", ImVec2(120, 0)))
			{
				if (!shiftMapDelta.IsZero())
				{
					if (rend)
					{
						rend->pushUndoState("Shift Map", EDIT_MODEL_LUMPS | FL_ENTITIES);
					}
					map->move(shiftMapDelta);
					app->reloading = true;
					if (rend)
					{
						rend->reload();
					}
					app->reloading = false;
					print_log("Shifted map '{}' by ({}, {}, {})\n", map->bsp_name, shiftMapDelta.x, shiftMapDelta.y, shiftMapDelta.z);
					showShiftMapDialog = false;
				}
			}
			ImGui::PopStyleColor(1);
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 0)))
			{
				showShiftMapDialog = false;
			}
		}
	}
	ImGui::End();
}

void Gui::drawModentDialog()
{
	ImGui::SetNextWindowSize(ImVec2(620.f, 350.f), ImGuiCond_FirstUseEver);
	Bsp* map = app->getSelectedMap();
	BspRenderer* rend = map ? map->getBspRender() : NULL;

	if (ImGui::Begin("Batch Entity Query (modent)###MODENT_DIALOG", &showModentDialog))
	{
		if (!map || map->is_mdl_model)
		{
			ImGui::TextDisabled("No active BSP map loaded.");
		}
		else
		{
			static std::string queryString = "classname=monster_*";
			static int actionType = 0; // 0: Delete, 1: Set Keyvalues, 2: Remove Key
			static std::string actionArg = "";

			ImGui::Text("Batch headless query engine for entities in active map:");
			ImGui::Spacing();

			ImGui::SetNextItemWidth(500);
			ImGui::InputTextWithHint("Query", "e.g. classname=monster_* AND targetname=", &queryString);
			ImGui::TextDisabled("Query syntax: =, !=, AND, OR, wildcards (*)");

			ImGui::Spacing();
			ImGui::SetNextItemWidth(250);
			const char* actionNames[] = {"Delete matched entities", "Set / Add keyvalues", "Remove key"};
			ImGui::Combo("Action", &actionType, actionNames, 3);

			if (actionType == 1)
			{
				ImGui::SetNextItemWidth(500);
				ImGui::InputTextWithHint("Keyvalues", "e.g. targetname=monstruo, wait=5", &actionArg);
			}
			else if (actionType == 2)
			{
				ImGui::SetNextItemWidth(300);
				ImGui::InputTextWithHint("Key Name", "e.g. targetname", &actionArg);
			}

			ImGui::Spacing();
			EntityQuery query(queryString);
			int matchCount = 0;
			if (query.isValid())
			{
				for (size_t i = 1; i < map->ents.size(); i++)
				{
					if (query.evaluate(map->ents[i]))
						matchCount++;
				}
				ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%d entities match this query.", matchCount);
			}
			else
			{
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Invalid query syntax.");
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
			bool canExecute = query.isValid() && matchCount > 0;
			if (ImGui::Button("Execute Batch Action", ImVec2(160, 0)) && canExecute)
			{
				if (rend)
				{
					rend->pushEntityUndoStateDelay("Batch Entity Mod (modent)");
				}

				int modifiedCount = 0;
				if (actionType == 0) // Delete
				{
					for (int i = (int)map->ents.size() - 1; i >= 1; i--)
					{
						if (query.evaluate(map->ents[i]))
						{
							delete map->ents[i];
							map->ents.erase(map->ents.begin() + i);
							modifiedCount++;
						}
					}
				}
				else if (actionType == 1) // Set keyvalues
				{
					std::vector<std::string> kvs = splitString(actionArg, ",");
					for (size_t i = 1; i < map->ents.size(); i++)
					{
						if (query.evaluate(map->ents[i]))
						{
							for (auto& kv : kvs)
							{
								std::vector<std::string> parts = splitString(kv, "=");
								if (parts.size() == 2)
								{
									map->ents[i]->setOrAddKeyvalue(trimSpaces(parts[0]), trimSpaces(parts[1]));
								}
							}
							modifiedCount++;
						}
					}
				}
				else if (actionType == 2) // Remove key
				{
					std::string keyToRemove = trimSpaces(actionArg);
					for (size_t i = 1; i < map->ents.size(); i++)
					{
						if (query.evaluate(map->ents[i]))
						{
							map->ents[i]->removeKeyvalue(keyToRemove);
							modifiedCount++;
						}
					}
				}

				map->update_ent_lump();
				if (rend)
				{
					rend->preRenderEnts();
				}
				g_app->updateEnts();
				print_log("Batch entity query executed: {} entities modified.\n", modifiedCount);
				showModentDialog = false;
			}
			ImGui::PopStyleColor(1);
			ImGui::SameLine();
			if (ImGui::Button("Close", ImVec2(100, 0)))
			{
				showModentDialog = false;
			}
		}
	}
	ImGui::End();
}

void Gui::drawImportMapWidget()
{
	ImGui::SetNextWindowSize(ImVec2(500.f, 140.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(500.f, 140.f), ImVec2(500.f, 140.f));
	static std::string mapPath;
	const char* title = "Import .bsp model as func_breakable entity";

	if (showImportMapWidget_Type == SHOW_IMPORT_OPEN)
	{
		title = "Open map";
	}
	else if (showImportMapWidget_Type == SHOW_IMPORT_ADD_NEW)
	{
		title = "Add map to renderer";
	}
	else if (showImportMapWidget_Type == SHOW_IMPORT_MODEL_BSP)
	{
		title = "Copy BSP model to current map";
	}
	else if (showImportMapWidget_Type == SHOW_IMPORT_MODEL_ENTITY)
	{
		title = "Create func_breakable with bsp model path";
	}

	if (ImGui::Begin(fmt::format("{}###IMPORT_WIDGET", title).c_str(), &showImportMapWidget))
	{
		if (ifd::FileDialog::Instance().IsDone("BspOpenDialog"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				mapPath = res.string();
				g_settings.lastdir = stripFileName(res.string());
			}
			ifd::FileDialog::Instance().Close();
		}

		ImGui::InputText(get_localized_string(LANG_0833).c_str(), &mapPath);
		ImGui::SameLine();

		ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
		bool clicked_24 = ImGui::Button(get_localized_string(LANG_0834).c_str());
		ImGui::PopStyleColor(1);
		if (clicked_24)
		{
			ifd::FileDialog::Instance().Open("BspOpenDialog", "Opep bsp model", "BSP file (*.bsp){.bsp},.*", false, g_settings.lastdir);
		}

		ImGui::PushStyleColor(ImGuiCol_Button, COLOR_NIGHTMARE_PURPLE);
		bool clicked_25 = ImGui::Button(get_localized_string(LANG_0835).c_str(), ImVec2(120, 0));
		ImGui::PopStyleColor(1);
		if (clicked_25)
		{
			fixupPath(mapPath, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP);
			if (fileExists(mapPath))
			{
				print_log(get_localized_string(LANG_0399), mapPath);
				showImportMapWidget = false;
				if (showImportMapWidget_Type == SHOW_IMPORT_ADD_NEW)
				{
					app->addMap(new Bsp(mapPath));
				}
				else if (showImportMapWidget_Type == SHOW_IMPORT_OPEN)
				{
					app->clearMaps();
					app->addMap(new Bsp(mapPath));
				}
				else if (showImportMapWidget_Type == SHOW_IMPORT_MODEL_BSP)
				{
					Bsp* map = app->getSelectedMap();
					if (map)
					{
						int import_model = ImportModel(map, mapPath);
						Entity* newEnt = new Entity("func_wall");
						newEnt->addKeyvalue("model", "*" + std::to_string(import_model));
						map->ents.push_back(newEnt);
						map->getBspRender()->refreshEnt((int)(map->ents.size()) - 1);

						map->getBspRender()->pushUndoState("Import BSP", 0xFFFFFFFF);
					}
				}
				else if (showImportMapWidget_Type == SHOW_IMPORT_MODEL_ENTITY)
				{
					Bsp* map = app->getSelectedMap();
					if (map)
					{
						Bsp* model = new Bsp(mapPath);
						if (!model->ents.size())
						{
							print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0400));
						}
						else
						{
							print_log(get_localized_string(LANG_0401));
							app->deselectObject();
							map->ents.push_back(new Entity("func_breakable"));
							map->ents[map->ents.size() - 1]->setOrAddKeyvalue("gibmodel", std::string("models/") + basename(mapPath));
							map->ents[map->ents.size() - 1]->setOrAddKeyvalue("model", std::string("models/") + basename(mapPath));
							map->ents[map->ents.size() - 1]->setOrAddKeyvalue("spawnflags", "1");
							print_log(get_localized_string(LANG_0402), std::string("models/") + basename(mapPath));
							map->getBspRender()->pushUndoState("Import BSP", 0xFFFFFFFF);
							app->updateEnts();
							app->reloadBspModels();
						}
						delete model;
					}
				}
			}
			else
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0403));
			}
		}
	}
	ImGui::End();
}

void Gui::drawRadErrorModal()
{
	if (!showRadErrorModal)
		return;

	ImGui::OpenPopup("RAD Compilation Result");

	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal("RAD Compilation Result", &showRadErrorModal, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "RAD Compilation Error");
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
		ImGui::TextUnformatted(radErrorMessage.c_str());
		ImGui::PopTextWrapPos();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (!radLogFilePath.empty() && fileExists(radLogFilePath))
		{
			if (ImGui::Button("Open Compiler Log", ImVec2(150, 0)))
			{
				if (ImGui::GetPlatformIO().Platform_OpenInShellFn != NULL)
				{
					ImGui::GetPlatformIO().Platform_OpenInShellFn(ImGui::GetCurrentContext(), radLogFilePath.c_str());
				}
			}
			ImGui::SameLine();
		}

		if (ImGui::Button("Open Settings", ImVec2(120, 0)))
		{
			showSettingsWidget = true;
			showRadErrorModal = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Close", ImVec2(100, 0)))
		{
			showRadErrorModal = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

