#include "lang.h"
#include "Gui.h"
#include "Renderer.h"
#include "ShaderProgram.h"
#include "primitives.h"
#include "VertexBuffer.h"
#include "shaders.h"
#include "Settings.h"
#include "BspMerger.h"
#include "filedialog/ImFileDialog.h"
#include "imgui_stdlib.h"
#include "quantizer.h"
#include "vis.h"
#include "winding.h"
#include "util.h"
#include "log.h"

#include <lodepng.h>
#include <execution>
#include <unordered_set>
#include <algorithm>
#include "LeafNavMesh.h"
#include "as.h"
#include "gui/ActionRegistry.h"
#include "gui/GuiCommandPalette.h"
float g_tooltip_delay = 0.6f; // time in seconds before showing a IMGUI_TOOLTIP

bool filterNeeded = true;

std::string iniPath = "./imgui.ini";

enum umd_flags : unsigned int
{
	UMD_TEXTURES_SKIP_OPTIMIZE = 1 << 0,
	UMD_OPTIMIZE_DISABLED = 1 << 1
};

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

int UMD_MAGIC = 'umd2';

struct cell
{
	unsigned char height;
	unsigned char height_offset;
	unsigned char texid;
	cell_type type;
};

int cell_idx(const vec3& pos, const vec3& mins, float cell_size, int cell_x, int cell_y, int cell_layers, int layer)
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

void IMGUI_TOOLTIP(ImGuiContext& g, const std::string& IMGUI_TOOLTIP)
{
	if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(IMGUI_TOOLTIP.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

Gui::Gui(Renderer* app)
{
	guiHoverAxis = 0;
	this->app = app;
	allowExternalTextures = false;
}

void Gui::init()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	imgui_io = &ImGui::GetIO();

	imgui_io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
	// imgui_io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	imgui_io->IniFilename = !g_settings.save_windows ? NULL : iniPath.c_str();

	// Setup Dear ImGui style
	setupTheme();
	// ImGui::StyleColorsClassic();

	// Setup Platform/Renderer bindings
	ImGui_ImplGlfw_InitForOpenGL(app->window, true);
	ImGui_ImplOpenGL3_Init("#version 130");
	// ImFileDialog requires you to set the CreateTexture and DeleteTexture
	ifd::FileDialog::Instance().CreateTexture = [](unsigned char* data, int w, int h, char fmt) -> void*
	{
		GLuint tex;

		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, (fmt == 0) ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE, data);
		glBindTexture(GL_TEXTURE_2D, 0);
		return (void*)(size_t)tex;
	};
	ifd::FileDialog::Instance().DeleteTexture = [](void* tex)
	{
		GLuint texID = (GLuint)((uintptr_t)tex);
		glDeleteTextures(1, &texID);
	};

	loadFonts();

	imgui_io->ConfigWindowsMoveFromTitleBarOnly = true;

	auto loadIconHelper = [&](const char* path, const char* name, const char* fallbackPath = "./pictures/object.png")
	{
		unsigned char* img_malloc = NULL;
		unsigned int width = 0, height = 0;
		lodepng_decode32_file(&img_malloc, &width, &height, path);
		if (!img_malloc && fallbackPath)
		{
			lodepng_decode32_file(&img_malloc, &width, &height, fallbackPath);
		}
		unsigned char* img_new = NULL;
		if (img_malloc)
		{
			img_new = new unsigned char[width * height * 4];
			memcpy(img_new, img_malloc, width * height * 4);
			free(img_malloc);
		}
		else
		{
			width = 1;
			height = 1;
			img_new = new unsigned char[4]{255, 255, 255, 255};
		}
		return new Texture(width, height, img_new, name, true, true);
	};

	objectIconTexture = loadIconHelper("./pictures/object.png", "objIcon");
	objectIconTexture->upload();
	faceIconTexture = loadIconHelper("./pictures/face.png", "faceIcon");
	faceIconTexture->upload();
	leafIconTexture = loadIconHelper("./pictures/leaf.png", "leafIcon");
	leafIconTexture->upload();

	keyvaluesIconTexture = loadIconHelper("./pictures/keyvalues.png", "keyvaluesIcon");
	keyvaluesIconTexture->upload();
	transformIconTexture = loadIconHelper("./pictures/transform.png", "transformIcon");
	transformIconTexture->upload();
	faceEditorIconTexture = loadIconHelper("./pictures/face-editor.png", "faceEditorIcon", "./pictures/face.png");
	faceEditorIconTexture->upload();
	textureBrowserIconTexture = loadIconHelper("./pictures/texture_browser.png", "textureBrowserIcon");
	textureBrowserIconTexture->upload();
	lightmapIconTexture = loadIconHelper("./pictures/lightmap.png", "lightmapIcon");
	lightmapIconTexture->upload();
	logIconTexture = loadIconHelper("./pictures/log.png", "logIcon");
	logIconTexture->upload();
	debugIconTexture = loadIconHelper("./pictures/debug.png", "debugIcon");
	debugIconTexture->upload();
	overviewIconTexture = loadIconHelper("./pictures/overview.png", "overviewIcon");
	overviewIconTexture->upload();
	mergeIconTexture = loadIconHelper("./pictures/merge.png", "mergeIcon", "./pictures/object.png");
	mergeIconTexture->upload();

	RegisterAllAppActions(this, app);
}

ImVec4 imguiColorFromConsole(unsigned int colors)
{
	bool intensity = (colors & PRINT_INTENSITY) != 0;
	float red = (colors & PRINT_RED) ? (intensity ? 1.0f : 0.5f) : 0.0f;
	float green = (colors & PRINT_GREEN) ? (intensity ? 1.0f : 0.5f) : 0.0f;
	float blue = (colors & PRINT_BLUE) ? (intensity ? 1.0f : 0.5f) : 0.0f;
	return ImVec4(red, green, blue, 1.0f);
}

void Gui::draw()
{
	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	ImGui::PushFont(defaultFont);
	drawMenuBar();

	drawFpsOverlay();
	drawToolbar();
	drawPanelsToolbar();
	drawStatusMessage();
	drawStatusBar();

	if (showLogWidget)
	{
		drawLog();
	}

	if (showHelpWidget)
	{
		drawHelp();
	}

	if (showAboutWidget)
	{
		drawAbout();
	}

	if (showSettingsWidget)
	{
		drawSettings();
	}

	Bsp* map = app->getSelectedMap();
	if (map && map->is_mdl_model && map->map_mdl)
	{
		drawMDLWidget();
	}
	else
	{
		if (showDebugWidget)
		{
			drawDebugWidget();
		}
		if (showKeyvalueWidget)
		{
			drawKeyvalueEditor();
		}
		if (showTextureBrowser)
		{
			drawTextureBrowser();
		}
		bool inOverview = showOverviewWidget && orthoMode;
		if (inOverview && !wasInOverview)
		{
			oldCameraOrigin = cameraOrigin;
			oldCameraAngles = cameraAngles;
			wasInOverview = true;
		}
		else if (!inOverview && wasInOverview)
		{
			cameraOrigin = oldCameraOrigin;
			cameraAngles = oldCameraAngles;
			wasInOverview = false;
		}

		if (showOverviewWidget)
		{
			drawOverviewWidget();
		}
		if (showTransformWidget)
		{
			drawTransformWidget();
		}
		if (showImportMapWidget)
		{
			drawImportMapWidget();
		}
		if (showMergeMapWidget)
		{
			drawMergeWindow();
		}
		if (showShiftMapDialog)
		{
			drawShiftMapDialog();
		}
		if (showModentDialog)
		{
			drawModentDialog();
		}
		if (showLimitsWidget)
		{
			drawLimits();
		}
		if (showFaceEditWidget)
		{
			drawFaceEditorWidget();
		}
		if (showLightmapEditorWidget)
		{
			drawLightMapTool();
		}
		if (showEntityReport)
		{
			drawEntityReport();
		}
		if (showGOTOWidget)
		{
			drawGOTOWidget();
		}

		if (openEmptyContext != -2)
		{
			if (openEmptyContext == 0)
			{
				ImGui::OpenPopup("empty_context");
			}
			else if (openEmptyContext == 1)
			{
				ImGui::OpenPopup("ent_context");
			}
			else if (openEmptyContext == 2)
			{
				ImGui::OpenPopup("face_context");
			}
			else
			{
				if (app->pickMode == PICK_OBJECT)
				{
					ImGui::OpenPopup("ent_context");
				}
				else
				{
					ImGui::OpenPopup("face_context");
				}
			}
			openEmptyContext = -2;
		}

		drawBspContexMenu();
	}

	GuiCommandPalette::getInstance().draw(this);

	app->anyPopupOpened = imgui_io->WantCaptureMouse;

	ImGui::PopFont();

	// Rendering
	glViewport(0, 0, app->windowWidth, app->windowHeight);
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	if (shouldReloadFonts)
	{
		shouldReloadFonts = false;
		imgui_io->Fonts->Clear();
		loadFonts();
	}
}

void Gui::openContextMenu(int type)
{
	openEmptyContext = type;
}

void Gui::openContextMenu(bool empty)
{
	if (empty)
	{
		openEmptyContext = 0;
	}
	else
	{
		if (app->pickMode == PICK_OBJECT)
			openEmptyContext = 1;
		else
			openEmptyContext = 2;
	}
}

void Gui::copyTexture()
{
	Bsp* map = app->getSelectedMap();
	if (!map)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0313));
		return;
	}
	else if (app->pickInfo.selectedFaces.size() == 0)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0314));
		return;
	}

	std::string outfaces;
	for (const auto& f : app->pickInfo.selectedFaces)
	{
		outfaces += std::to_string(f) + " ";
	}
	if (outfaces.size())
	{
		outfaces.pop_back();
		ImGui::SetClipboardText(outfaces.c_str());
	}

	BSPTEXTUREINFO& texinfo = map->texinfos[map->faces[app->pickInfo.selectedFaces[0]].iTextureInfo];
	copiedMiptex = texinfo.iMiptex == -1 || texinfo.iMiptex >= map->textureCount ? 0 : texinfo.iMiptex;
}

void Gui::copyStyle()
{
	Bsp* map = app->getSelectedMap();
	if (!map || app->pickInfo.selectedFaces.empty())
		return;

	int faceIdx = (int)app->pickInfo.selectedFaces[0];
	BSPFACE32& face = map->faces[faceIdx];
	BSPPLANE plane = map->getPlaneFromFace(&face);
	BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];

	vec3 xv, yv;
	int bestplane = TextureAxisFromPlane(plane, xv, yv);

	copiedStyle.rotateX = AngleFromTextureAxis(texinfo.vS, true, bestplane);
	copiedStyle.rotateY = AngleFromTextureAxis(texinfo.vT, false, bestplane);
	copiedStyle.scaleX = texinfo.vS.length() > EPSILON ? (1.0f / texinfo.vS.length()) : 1.0f;
	copiedStyle.scaleY = texinfo.vT.length() > EPSILON ? (1.0f / texinfo.vT.length()) : 1.0f;
	copiedStyle.shiftX = texinfo.shiftS;
	copiedStyle.shiftY = texinfo.shiftT;
	copiedStyle.valid = true;

	print_log("Style copied: Scale({:.3f}, {:.3f}), Shift({:.3f}, {:.3f}), Rotate({:.3f}, {:.3f})\n",
			  copiedStyle.scaleX, copiedStyle.scaleY, copiedStyle.shiftX, copiedStyle.shiftY, copiedStyle.rotateX, copiedStyle.rotateY);
}

void Gui::pasteStyle()
{
	Bsp* map = app->getSelectedMap();
	if (!map || app->pickInfo.selectedFaces.empty() || !copiedStyle.valid)
		return;

	BspRenderer* mapRenderer = map->getBspRender();

	for (int faceIdx : app->pickInfo.selectedFaces)
	{
		BSPFACE32& face = map->faces[faceIdx];
		BSPTEXTUREINFO* texinfo = map->get_unique_texinfo(faceIdx);
		BSPPLANE plane = map->getPlaneFromFace(&face);

		vec3 xv, yv;
		int bestplane = TextureAxisFromPlane(plane, xv, yv);

		texinfo->vS = AxisFromTextureAngle(copiedStyle.rotateX, true, bestplane);
		texinfo->vT = AxisFromTextureAngle(copiedStyle.rotateY, false, bestplane);
		if (copiedStyle.scaleX > 0.00001f && texinfo->vS.length() > EPSILON)
			texinfo->vS = texinfo->vS.normalize(1.0f / copiedStyle.scaleX);
		if (copiedStyle.scaleY > 0.00001f && texinfo->vT.length() > EPSILON)
			texinfo->vT = texinfo->vT.normalize(1.0f / copiedStyle.scaleY);
		texinfo->shiftS = copiedStyle.shiftX;
		texinfo->shiftT = copiedStyle.shiftY;

		if (mapRenderer)
		{
			mapRenderer->updateFaceUVs(faceIdx);
			mapRenderer->highlightFace(faceIdx, 1);
		}
	}

	map->resize_all_lightmaps(true);
	if (mapRenderer)
	{
		mapRenderer->reloadLightmapsSync();
		mapRenderer->pushUndoState("Paste Style", EDIT_MODEL_LUMPS);
		for (int faceIdx : app->pickInfo.selectedFaces)
		{
			mapRenderer->highlightFace(faceIdx, 1);
		}
	}
	pickCount++;
	vertPickCount++;
}

void Gui::pasteTexture()
{
	Bsp* map = app->getSelectedMap();
	if (!map)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0313));
		return;
	}
	if (app->pickInfo.selectedFaces.empty())
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0314));
		return;
	}
	if (copiedMiptex < 0 || copiedMiptex >= map->textureCount)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, "No valid texture copied to paste.\n");
		return;
	}

	BspRenderer* mapRenderer = map->getBspRender();
	std::set<int> modelRefreshes;

	for (int faceIdx : app->pickInfo.selectedFaces)
	{
		if (faceIdx < 0 || faceIdx >= map->faceCount)
			continue;
		BSPTEXTUREINFO* texinfo = map->get_unique_texinfo(faceIdx);
		texinfo->iMiptex = copiedMiptex;

		int modelIdx = map->get_model_from_face(faceIdx);
		if (modelIdx >= 0)
			modelRefreshes.insert(modelIdx);

		if (mapRenderer)
		{
			mapRenderer->updateFaceUVs(faceIdx);
		}
	}

	if (mapRenderer)
	{
		for (int m : modelRefreshes)
		{
			mapRenderer->refreshModel(m);
		}
	}

	map->resize_all_lightmaps(true);
	if (mapRenderer)
	{
		mapRenderer->reloadLightmapsSync();
		mapRenderer->pushUndoState("Paste Texture", EDIT_MODEL_LUMPS);
		for (int faceIdx : app->pickInfo.selectedFaces)
		{
			mapRenderer->highlightFace(faceIdx, 1);
		}
	}
	pickCount++;
	vertPickCount++;
}

void Gui::copyLightmap()
{
	Bsp* map = app->getSelectedMap();

	if (!map)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1049));
		return;
	}
	else if (app->pickInfo.selectedFaces.size() == 0 || app->pickInfo.selectedFaces.size() > 1)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1050));
		return;
	}

	copiedLightmap.face = (int)app->pickInfo.selectedFaces[0];

	int size[2];
	map->GetFaceLightmapSize(copiedLightmap.face, size);
	copiedLightmap.width = size[0];
	copiedLightmap.height = size[1];
	copiedLightmap.layers = map->lightmap_count((int)app->pickInfo.selectedFaces[0]);

	copiedLightmapData.clear();
	if (copiedLightmap.layers > 0 && map->lightdata)
	{
		int totalSize = copiedLightmap.width * copiedLightmap.height * copiedLightmap.layers;
		int offset = map->faces[copiedLightmap.face].nLightmapOffset;
		if (offset >= 0 && offset + totalSize * (int)sizeof(COLOR3) <= map->lightDataLength)
		{
			COLOR3* srcData = (COLOR3*)(map->lightdata + offset);
			copiedLightmapData.assign(srcData, srcData + totalSize);
		}
	}
}

void Gui::pasteLightmap()
{
	Bsp* map = app->getSelectedMap();
	if (!map)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1149));
		return;
	}
	else if (app->pickInfo.selectedFaces.empty())
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1150));
		return;
	}

	if (copiedLightmapData.empty())
	{
		print_log(PRINT_RED | PRINT_INTENSITY, "No lightmap data copied or source face has no lightmap.\n");
		return;
	}

	BspRenderer* mapRenderer = map->getBspRender();
	map->save_undo_lightmaps();

	std::vector<COLOR3> accumulatedLighting;
	if (map->lightdata && map->lightDataLength > 0)
	{
		COLOR3* currentLighting = (COLOR3*)map->lightdata;
		accumulatedLighting.assign(currentLighting, currentLighting + (map->lightDataLength / sizeof(COLOR3)));
	}

	BSPFACE32& srcFace = map->faces[copiedLightmap.face];

	for (int faceIdx : app->pickInfo.selectedFaces)
	{
		BSPFACE32& dstFace = map->faces[faceIdx];
		BSPTEXTUREINFO* dstTexInfo = map->get_unique_texinfo(faceIdx);

		if (dstTexInfo && (dstTexInfo->nFlags & TEX_SPECIAL))
		{
			dstTexInfo->nFlags &= ~TEX_SPECIAL;
		}

		int dstSize[2];
		map->GetFaceLightmapSize(faceIdx, dstSize);
		if (dstSize[0] <= 0) dstSize[0] = 1;
		if (dstSize[1] <= 0) dstSize[1] = 1;

		dstFace.nLightmapOffset = (int)accumulatedLighting.size() * sizeof(COLOR3);

		for (int i = 0; i < copiedLightmap.layers; i++)
		{
			COLOR3* srcLayer = copiedLightmapData.data() + (i * copiedLightmap.width * copiedLightmap.height);
			std::vector<COLOR3> scaledLayer;
			scaleImage(srcLayer, scaledLayer, copiedLightmap.width, copiedLightmap.height, dstSize[0], dstSize[1]);
			accumulatedLighting.insert(accumulatedLighting.end(), scaledLayer.begin(), scaledLayer.end());
		}

		// Copy styles
		memcpy(dstFace.nStyles, srcFace.nStyles, MAX_LIGHTMAPS);
	}

	map->replace_lump(LUMP_LIGHTING, accumulatedLighting.data(), accumulatedLighting.size() * sizeof(COLOR3));
	map->remove_unused_model_structures(CLEAN_LIGHTMAP);
	if (mapRenderer)
	{
		mapRenderer->loadLightmaps();
		mapRenderer->pushUndoState("Paste Lightmap", FL_LIGHTING | FL_FACES);
	}
}

int ImportModel(Bsp* map, const std::string& mdl_path, bool noclip)
{
	if (!map || !map->getBspRender())
		return -1;
	if (!fileExists(mdl_path))
		return -1;
	Bsp* bspModel = new Bsp(mdl_path);
	bspModel->setBspRender(map->getBspRender());

	std::vector<BSPPLANE> newPlanes;
	std::vector<vec3> newVerts;
	std::vector<BSPEDGE32> newEdges;
	std::vector<int> newSurfedges;
	std::vector<BSPTEXTUREINFO> newTexinfo;
	std::vector<BSPFACE32> newFaces;
	std::vector<COLOR3> newLightmaps;
	std::vector<BSPNODE32> newNodes;
	std::vector<BSPCLIPNODE32> newClipnodes;
	std::vector<WADTEX> newTextures;
	std::vector<BSPLEAF32> newLeaves;
	std::vector<int> newMarkSurfaces;

	STRUCTREMAP remap(bspModel);
	STRUCTUSAGE usage(bspModel);
	bspModel->copy_bsp_model(0, map, remap, usage, newPlanes, newVerts, newEdges, newSurfedges, newTexinfo, newFaces,
							 newLightmaps, newNodes, newClipnodes, newTextures, newLeaves, newMarkSurfaces, true);

	if (!noclip && newClipnodes.size())
	{
		map->append_lump(LUMP_CLIPNODES, &newClipnodes[0], sizeof(BSPCLIPNODE32) * newClipnodes.size());
	}
	if (newEdges.size())
	{
		map->append_lump(LUMP_EDGES, &newEdges[0], sizeof(BSPEDGE32) * newEdges.size());
	}
	if (newFaces.size())
	{
		map->append_lump(LUMP_FACES, &newFaces[0], sizeof(BSPFACE32) * newFaces.size());
	}
	if (newLeaves.size())
	{
		map->append_lump(LUMP_LEAVES, &newLeaves[0], sizeof(BSPLEAF32) * newLeaves.size());
	}
	if (newMarkSurfaces.size())
	{
		map->append_lump(LUMP_MARKSURFACES, &newMarkSurfaces[0], sizeof(int) * newMarkSurfaces.size());
	}
	if (newNodes.size())
	{
		map->append_lump(LUMP_NODES, &newNodes[0], sizeof(BSPNODE32) * newNodes.size());
	}
	if (newPlanes.size())
	{
		map->append_lump(LUMP_PLANES, &newPlanes[0], sizeof(BSPPLANE) * newPlanes.size());
	}
	if (newSurfedges.size())
	{
		map->append_lump(LUMP_SURFEDGES, &newSurfedges[0], sizeof(int) * newSurfedges.size());
	}

	if (newTextures.size())
	{
		while (newTextures.size())
		{
			auto& tex = newTextures[newTextures.size() - 1];
			if (tex.data.size())
			{
				auto data = ConvertWadTexToRGB(tex);
				map->add_texture(tex.szName, (unsigned char*)data, tex.nWidth, tex.nHeight);
				delete[] data;
			}
			else
			{
				map->add_texture(tex.szName, NULL, tex.nWidth, tex.nHeight);
			}
			newTextures.pop_back();
		}
	}

	map->update_lump_pointers();

	if (newTexinfo.size())
	{
		for (auto& texinfo : newTexinfo)
		{
			if (texinfo.iMiptex < 0 || texinfo.iMiptex >= bspModel->textureCount)
			{
				texinfo.iMiptex = 0;
				texinfo.nFlags = TEX_SPECIAL;
				continue;
			}
			int newMiptex = -1;
			int texOffset = ((int*)bspModel->textures)[texinfo.iMiptex + 1];
			if (texOffset < 0)
			{
				texinfo.iMiptex = 0;
				texinfo.nFlags = TEX_SPECIAL;
				continue;
			}
			BSPMIPTEX& tex = *((BSPMIPTEX*)(bspModel->textures + texOffset));
			for (int i = map->textureCount - 1; i >= 0; i--)
			{
				int tex2Offset = ((int*)map->textures)[i + 1];
				if (tex2Offset >= 0)
				{
					BSPMIPTEX* tex2 = ((BSPMIPTEX*)(map->textures + tex2Offset));
					if (strcasecmp(tex.szName, tex2->szName) == 0)
					{
						newMiptex = i;
						break;
					}
				}
			}
			if (newMiptex < 0 && bspModel->getBspRender() && bspModel->getBspRender()->wads.size())
			{
				for (auto& s : bspModel->getBspRender()->wads)
				{
					if (s->hasTexture(tex.szName))
					{
						WADTEX wadTex = s->readTexture(tex.szName);
						COLOR3* imageData = ConvertWadTexToRGB(wadTex);

						newMiptex = map->add_texture(tex.szName, (unsigned char*)imageData, wadTex.nWidth, wadTex.nHeight);

						delete[] imageData;
						break;
					}
				}
			}
			texinfo.iMiptex = newMiptex;
			if (newMiptex < 0)
			{
				texinfo.iMiptex = 0;
				texinfo.nFlags = TEX_SPECIAL;
			}
		}
		map->append_lump(LUMP_TEXINFO, &newTexinfo[0], sizeof(BSPTEXTUREINFO) * newTexinfo.size());
	}

	if (newVerts.size())
	{
		map->append_lump(LUMP_VERTICES, &newVerts[0], sizeof(vec3) * newVerts.size());
	}
	if (newLightmaps.size())
	{
		map->append_lump(LUMP_LIGHTING, &newLightmaps[0], sizeof(COLOR3) * newLightmaps.size());
	}

	int newModelIdx = map->create_model();
	map->models[newModelIdx] = bspModel->models[0];

	if (map->models[newModelIdx].iFirstFace >= 0 && map->models[newModelIdx].iFirstFace < (int)remap.faces.size())
		map->models[newModelIdx].iFirstFace = remap.faces[map->models[newModelIdx].iFirstFace];

	auto getRemappedLeaf = [&](int leafIdx)
	{
		if (leafIdx >= 0 && leafIdx < (int)remap.leaves.size())
		{
			return ~(remap.leaves[leafIdx]);
		}
		return -1;
	};

	if (map->models[newModelIdx].iHeadnodes[0] < 0)
		map->models[newModelIdx].iHeadnodes[0] = getRemappedLeaf(~map->models[newModelIdx].iHeadnodes[0]);
	else if (map->models[newModelIdx].iHeadnodes[0] < (int)remap.nodes.size())
		map->models[newModelIdx].iHeadnodes[0] = remap.nodes[map->models[newModelIdx].iHeadnodes[0]];
	else
		map->models[newModelIdx].iHeadnodes[0] = -1;

	if (!noclip)
	{
		for (int i = 1; i < MAX_MAP_HULLS; i++)
		{
			if (map->models[newModelIdx].iHeadnodes[i] < 0)
				map->models[newModelIdx].iHeadnodes[i] = map->models[newModelIdx].iHeadnodes[i];
			else if (map->models[newModelIdx].iHeadnodes[i] < (int)remap.clipnodes.size())
				map->models[newModelIdx].iHeadnodes[i] = remap.clipnodes[map->models[newModelIdx].iHeadnodes[i]];
			else
				map->models[newModelIdx].iHeadnodes[i] = -1;
		}
	}
	else
	{
		for (int i = 1; i < MAX_MAP_HULLS; i++)
		{
			map->models[newModelIdx].iHeadnodes[i] = CONTENTS_EMPTY;
		}
	}

	// if (map->models[newModelIdx].nVisLeafs > 0 && map->models[newModelIdx].nVisLeafs > newLeaves.size())
	//{
	//	map->models[newModelIdx].nVisLeafs--;
	// }
	// else if (map->models[newModelIdx].nVisLeafs > newLeaves.size())
	//{
	//	map->leafCount--;
	//	map->bsp_header.lump[LUMP_LEAVES].nLength -= sizeof(BSPLEAF32);
	// }
	// else if (newLeaves.size() > map->models[newModelIdx].nVisLeafs)
	//{
	//	map->models[newModelIdx].nVisLeafs++;
	// }

	bspModel->setBspRender(NULL);
	delete bspModel;

	g_app->deselectObject();

	map->save_undo_lightmaps();
	map->resize_all_lightmaps();

	BspRenderer* rend = map->getBspRender();

	rend->reuploadTextures();

	rend->loadLightmaps();
	rend->refreshModel(newModelIdx);
	rend->preRenderEnts();

	map->getBspRender()->pushUndoState("IMPORT MODEL", EDIT_MODEL_LUMPS | FL_ENTITIES);

	return newModelIdx;
}

void Gui::ExportFaceModel(Bsp* src_map, const std::string& export_path, const std::vector<int>& faceIdxs, int ExportType, bool movemodel)
{
	if (faceIdxs.empty())
		return;

	LumpState backupLumps = src_map->duplicate_lumps();

	// Create a temporary model in the source map
	int newModelIdx = src_map->create_model();

	// Copy faces to a temporary buffer first to avoid pointer invalidation during append
	std::vector<BSPFACE32> tempFaces;
	for (int fid : faceIdxs)
	{
		tempFaces.push_back(src_map->faces[fid]);
	}

	// Copy faces to the end of the face lump to make them contiguous for the model
	int firstFace = src_map->faceCount;
	for (auto& face : tempFaces)
	{
		src_map->append_lump(LUMP_FACES, &face, sizeof(BSPFACE32));
	}
	src_map->update_lump_pointers();

	src_map->models[newModelIdx].iFirstFace = firstFace;
	src_map->models[newModelIdx].nFaces = (int)faceIdxs.size();

	src_map->get_model_vertex_bounds(newModelIdx, src_map->models[newModelIdx].nMins, src_map->models[newModelIdx].nMaxs);

	// Create a basic node tree for the model
	// We first create a bounding box to ensure the model has finite boundaries.
	int anyEmptyLeaf = src_map->create_leaf(CONTENTS_EMPTY);
	src_map->create_node_box(src_map->models[newModelIdx].nMins, src_map->models[newModelIdx].nMaxs, &src_map->models[newModelIdx], false, anyEmptyLeaf);

	// The node box creation sets iHeadnodes[0] to its first node.
	// We find the 'solid' leaf of this box and replace it with our face-based tree.
	int boxHeadNode = src_map->models[newModelIdx].iHeadnodes[0];

	// We build a "ladder" of nodes, one for each face, to ensure all planes are represented.
	int faceTreeHead = src_map->nodeCount;
	int sharedSolidLeaf = 0;

	for (int i = 0; i < (int)faceIdxs.size(); i++)
	{
		BSPNODE32 node;
		node.iFirstFace = firstFace + i;
		node.nFaces = 1;
		node.iPlane = src_map->faces[node.iFirstFace].iPlane;
		node.nMins = src_map->models[newModelIdx].nMins;
		node.nMaxs = src_map->models[newModelIdx].nMaxs;

		int side = src_map->faces[node.iFirstFace].nPlaneSide;

		if (i == (int)faceIdxs.size() - 1)
		{
			node.iChildren[1 - side] = ~sharedSolidLeaf;
		}
		else
		{
			node.iChildren[1 - side] = faceTreeHead + i + 1;
		}
		node.iChildren[side] = ~anyEmptyLeaf;

		src_map->append_lump(LUMP_NODES, &node, sizeof(BSPNODE32));
	}
	src_map->update_lump_pointers();

	// Connect the box tree to the face tree. The last node of the box (index 5 from boxHeadNode)
	// points to ~sharedSolidLeaf. We change it to point to faceTreeHead.
	src_map->nodes[boxHeadNode + 5].iChildren[1] = faceTreeHead;

	// Manually generate clipnodes for all hulls with proper expansion
	for (int hull = 1; hull < MAX_MAP_HULLS; hull++)
	{
		vec3 hullExtent = default_hull_extents[hull];

		// Create a bounding box for this hull
		src_map->create_clipnode_box(src_map->models[newModelIdx].nMins, src_map->models[newModelIdx].nMaxs, &src_map->models[newModelIdx], hull, false, false);
		int hullBoxHeadNode = src_map->models[newModelIdx].iHeadnodes[hull];

		// Build expanded face ladder for this hull
		int hullFaceTreeHead = src_map->clipnodeCount;
		for (int i = 0; i < (int)faceIdxs.size(); i++)
		{
			BSPFACE32& face = src_map->faces[firstFace + i];
			BSPCLIPNODE32 clipnode;

			BSPPLANE srcPlane = src_map->planes[face.iPlane];
			vec3 absNormal = vec3(std::abs(srcPlane.vNormal.x), std::abs(srcPlane.vNormal.y), std::abs(srcPlane.vNormal.z));
			float offset = dotProduct(absNormal, hullExtent);
			if (face.nPlaneSide == 0)
				srcPlane.fDist += offset;
			else
				srcPlane.fDist -= offset;

			clipnode.iPlane = src_map->create_plane();
			src_map->planes[clipnode.iPlane] = srcPlane;

			int side = face.nPlaneSide;
			if (i == (int)faceIdxs.size() - 1)
			{
				clipnode.iChildren[1 - side] = CONTENTS_SOLID;
			}
			else
			{
				clipnode.iChildren[1 - side] = hullFaceTreeHead + i + 1;
			}
			clipnode.iChildren[side] = CONTENTS_EMPTY;

			src_map->append_lump(LUMP_CLIPNODES, &clipnode, sizeof(BSPCLIPNODE32));
		}
		src_map->update_lump_pointers();

		// Link hull box to expanded face ladder
		// The last node of the box (index 5 from hullBoxHeadNode) points to CONTENTS_SOLID.
		// We change it to point to our expanded face ladder.
		src_map->clipnodes[hullBoxHeadNode + 5].iChildren[1] = hullFaceTreeHead;
	}

	// Now export this temporary model
	ExportModel(src_map, export_path, newModelIdx, ExportType, movemodel);

	src_map->replace_lumps(backupLumps);
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (backupLumps.lumps[i].size() == 0 && src_map->lumps[i].size() > 0)
		{
			src_map->lumps[i].clear();
			src_map->bsp_header.lump[i].nLength = 0;
		}
	}
	src_map->update_lump_pointers();
}

std::string get_auto_export_path(Bsp* map, int modelIdx)
{
	std::string targetname = "";
	std::string classname = "unknown";

	int entIdx = map->get_ent_from_model(modelIdx);
	if (entIdx >= 0)
	{
		Entity* ent = map->ents[entIdx];
		if (ent->hasKey("targetname") && !ent->keyvalues["targetname"].empty())
			targetname = "_" + ent->keyvalues["targetname"];
		if (ent->hasKey("classname"))
			classname = ent->keyvalues["classname"];
	}

	std::string folder = g_working_dir + "exported_models/" + map->bsp_name + "/";
	createDir(g_working_dir + "exported_models/");
	createDir(folder);

	return folder + std::to_string(modelIdx) + targetname + "_" + classname + ".bsp";
}

void ExportModel(Bsp* src_map, const std::string& export_path, int model_id, int ExportType, bool movemodel)
{
	std::string final_path = export_path;
	if (export_path.empty())
	{
		final_path = get_auto_export_path(src_map, model_id);
	}

	LumpState backupLumps = src_map->duplicate_lumps();

	Bsp* bspModel = new Bsp();
	bspModel->bsp_valid = true;

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		bspModel->bsp_header.lump[i].nOffset = 0;
		bspModel->bsp_header.lump[i].nLength = 0;
	}
	int textureCount = 0;

	bspModel->replace_lump(LUMP_TEXTURES, &textureCount, sizeof(int));

	bspModel->textureCount = 0;

	bspModel->ents.clear();
	bspModel->ents.push_back(new Entity("worldspawn"));

	int src_entId = src_map->get_ent_from_model(0);

	if (src_entId >= 0)
	{
		if (src_map->ents[src_entId]->hasKey("wad"))
		{
			bspModel->ents[bspModel->ents.size() - 1]->setOrAddKeyvalue("wad", src_map->ents[src_entId]->keyvalues["wad"]);
			bspModel->ents[bspModel->ents.size() - 1]->setOrAddKeyvalue("message", "bsp model");
		}
	}

	bspModel->update_ent_lump();

	/*bspModel->create_node(true);
	bspModel->create_clipnode(true);
	bspModel->create_leaf_back(CONTENTS_SOLID);*/

	bspModel->create_node();
	bspModel->create_clipnode();
	bspModel->create_leaf(CONTENTS_SOLID);

	std::vector<BSPPLANE> newPlanes;
	std::vector<vec3> newVerts;
	std::vector<BSPEDGE32> newEdges;
	std::vector<int> newSurfedges;
	std::vector<BSPTEXTUREINFO> newTexinfo;
	std::vector<BSPFACE32> newFaces;
	std::vector<COLOR3> newLightmaps;
	std::vector<BSPNODE32> newNodes;
	std::vector<BSPCLIPNODE32> newClipnodes;
	std::vector<WADTEX> newTextures;
	std::vector<BSPLEAF32> newLeaves;
	std::vector<int> newMarkSurfaces;

	STRUCTREMAP remap(src_map);
	STRUCTUSAGE usage(src_map);

	src_map->copy_bsp_model(model_id, bspModel, remap, usage, newPlanes, newVerts, newEdges, newSurfedges, newTexinfo, newFaces,
							newLightmaps, newNodes, newClipnodes, newTextures, newLeaves, newMarkSurfaces, true);

	if (newEdges.size())
	{
		bspModel->append_lump(LUMP_EDGES, &newEdges[0], sizeof(BSPEDGE32) * newEdges.size());
	}
	if (newFaces.size())
	{
		bspModel->append_lump(LUMP_FACES, &newFaces[0], sizeof(BSPFACE32) * newFaces.size());
	}
	if (newLeaves.size())
	{
		bspModel->append_lump(LUMP_LEAVES, &newLeaves[0], sizeof(BSPLEAF32) * newLeaves.size());
	}
	if (newMarkSurfaces.size())
	{
		bspModel->append_lump(LUMP_MARKSURFACES, &newMarkSurfaces[0], sizeof(int) * newMarkSurfaces.size());
	}

	if (newNodes.size())
	{
		bspModel->append_lump(LUMP_NODES, &newNodes[0], sizeof(BSPNODE32) * newNodes.size());
	}

	if (newClipnodes.size())
	{
		bspModel->append_lump(LUMP_CLIPNODES, &newClipnodes[0], sizeof(BSPCLIPNODE32) * newClipnodes.size());
	}

	if (newPlanes.size())
	{
		bspModel->append_lump(LUMP_PLANES, &newPlanes[0], sizeof(BSPPLANE) * newPlanes.size());
	}
	if (newSurfedges.size())
	{
		bspModel->append_lump(LUMP_SURFEDGES, &newSurfedges[0], sizeof(int) * newSurfedges.size());
	}

	if (newTextures.size())
	{
		while (newTextures.size())
		{
			auto& tex = newTextures[newTextures.size() - 1];
			if (tex.data.size() && ExportType != 0)
			{
				auto data = ConvertWadTexToRGB(tex);
				int mip = bspModel->add_texture(tex.szName, (unsigned char*)data, tex.nWidth, tex.nHeight);
				delete[] data;
				data = ConvertMipTexToRGB(bspModel->find_embedded_texture(tex.szName, mip));
				delete[] data;
			}
			else
			{
				bspModel->add_texture(tex.szName, NULL, tex.nWidth, tex.nHeight);
			}
			newTextures.pop_back();
		}
	}

	bspModel->update_lump_pointers();

	if (newTexinfo.size())
	{
		for (auto& texinfo : newTexinfo)
		{
			if (texinfo.iMiptex < 0 || texinfo.iMiptex >= src_map->textureCount)
			{
				texinfo.iMiptex = 0;
				texinfo.nFlags = TEX_SPECIAL;
				continue;
			}
			int newMiptex = -1;
			int texOffset = ((int*)src_map->textures)[texinfo.iMiptex + 1];
			if (texOffset < 0)
			{
				texinfo.iMiptex = 0;
				texinfo.nFlags = TEX_SPECIAL;
				continue;
			}
			BSPMIPTEX& tex = *((BSPMIPTEX*)(src_map->textures + texOffset));
			for (int i = bspModel->textureCount - 1; i >= 0; i--)
			{
				int tex2Offset = ((int*)bspModel->textures)[i + 1];
				if (tex2Offset >= 0)
				{
					BSPMIPTEX* tex2 = ((BSPMIPTEX*)(bspModel->textures + tex2Offset));
					if (strcasecmp(tex.szName, tex2->szName) == 0)
					{
						newMiptex = i;
						break;
					}
				}
			}
			if (newMiptex < 0 && src_map->getBspRender() && src_map->getBspRender()->wads.size())
			{
				for (auto& s : src_map->getBspRender()->wads)
				{
					if (s->hasTexture(tex.szName))
					{
						WADTEX wadTex = s->readTexture(tex.szName);
						if (ExportType != 0)
						{
							COLOR3* imageData = ConvertWadTexToRGB(wadTex);
							newMiptex = bspModel->add_texture(tex.szName, (unsigned char*)imageData, wadTex.nWidth, wadTex.nHeight);
							delete[] imageData;
						}
						else
						{
							newMiptex = bspModel->add_texture(tex.szName, NULL, wadTex.nWidth, wadTex.nHeight);
						}

						break;
					}
				}
			}
			texinfo.iMiptex = newMiptex;
			if (newMiptex < 0)
			{
				texinfo.iMiptex = 0;
				texinfo.nFlags = TEX_SPECIAL;
			}
		}
		bspModel->append_lump(LUMP_TEXINFO, &newTexinfo[0], sizeof(BSPTEXTUREINFO) * newTexinfo.size());
	}

	if (newVerts.size())
	{
		bspModel->append_lump(LUMP_VERTICES, &newVerts[0], sizeof(vec3) * newVerts.size());
	}
	if (newLightmaps.size())
	{
		bspModel->append_lump(LUMP_LIGHTING, &newLightmaps[0], sizeof(COLOR3) * newLightmaps.size());
	}

	int newModelIdx = bspModel->create_model();

	bspModel->models[newModelIdx] = src_map->models[model_id];
	bspModel->models[newModelIdx].iFirstFace = remap.faces[bspModel->models[newModelIdx].iFirstFace];
	bspModel->models[newModelIdx].iHeadnodes[0] = bspModel->models[newModelIdx].iHeadnodes[0] < 0 ? -1 : remap.nodes[bspModel->models[newModelIdx].iHeadnodes[0]];
	for (int i = 1; i < MAX_MAP_HULLS; i++)
	{
		bspModel->models[newModelIdx].iHeadnodes[i] = bspModel->models[newModelIdx].iHeadnodes[i] < 0 ? -1 : remap.clipnodes[bspModel->models[newModelIdx].iHeadnodes[i]];
	}

	/*STRUCTCOUNT removed = bspModel->remove_unused_model_structures();
	if (!removed.allZero())
		removed.print_delete_stats(1);*/

	if (movemodel)
	{
		vec3 modelOrigin = src_map->get_model_center(model_id);
		print_log(get_localized_string(LANG_0325));
		bspModel->move(-modelOrigin, 0, true, true);
	}

	if (ExportType != 0)
	{
		print_log(get_localized_string(LANG_0326));
		bspModel->update_lump_pointers();
		update_unused_wad_files(src_map, bspModel, ExportType);
	}

	if (ExportType == 1)
	{
		bspModel->is_bsp29 = true;
		bspModel->is_texture_has_pal = false;
		bspModel->target_save_texture_has_pal = false;
		bspModel->bsp_header.nVersion = 29;
	}
	else
	{
		bspModel->is_bsp29 = false;
		bspModel->is_texture_has_pal = true;
		bspModel->target_save_texture_has_pal = true;
		bspModel->bsp_header.nVersion = 30;
	}

	// if (src_entId >= 0)
	//{
	//	if (src_map->ents[src_entId]->classname == "func_water")
	//	{
	//		bspModel->models[0].vOrigin = getCenter(bspModel->models[0].nMins, bspModel->models[0].nMaxs);
	//	}
	// }

	bspModel->bsp_path = final_path;
	bspModel->write(bspModel->bsp_path);
	removeFile(bspModel->bsp_path);

	unsigned char* tmpCompressed = new unsigned char[g_limits.maxMapLeaves / 8];
	memset(tmpCompressed, 0xFF, g_limits.maxMapLeaves / 8);

	/* if something bad */
	bspModel->models[newModelIdx].nVisLeafs = bspModel->leafCount - 1;

	// ADD LEAFS TO ALL VISIBILITY BYTES
	for (int i = 0; i < bspModel->leafCount; i++)
	{
		if (bspModel->leaves[i].nVisOffset < 0)
		{
			bspModel->leaves[i].nVisOffset = bspModel->visDataLength;
			unsigned char* newVisLump = new unsigned char[bspModel->visDataLength + g_limits.maxMapLeaves / 8];
			memcpy(newVisLump, bspModel->visdata, bspModel->visDataLength);
			memcpy(newVisLump + bspModel->visDataLength, tmpCompressed, g_limits.maxMapLeaves / 8);
			bspModel->replace_lump(LUMP_VISIBILITY, newVisLump, bspModel->visDataLength + g_limits.maxMapLeaves / 8);
			delete[] newVisLump;
		}
	}
	// recompile vis lump, remove unused textures
	bspModel->remove_unused_model_structures(CLEAN_VISDATA | CLEAN_TEXTURES);

	bspModel->validate();
	bspModel->write(bspModel->bsp_path);

	delete bspModel;
	delete[] tmpCompressed;

	src_map->replace_lumps(backupLumps);

	print_log(get_localized_string(LANG_1068), final_path);
}

void Gui::OpenFile(const std::string& file)
{
	Bsp* map = app->getSelectedMap();

	std::string pathlowercase = toLowerCase(file);
	if (ends_with(pathlowercase, ".wad"))
	{
		if (!map)
		{
			app->addMap(new Bsp(""));
			app->selectMapId(0);
			map = app->getSelectedMap();
		}

		if (map)
		{
			BspRenderer* rend = map ? map->getBspRender() : NULL;
			if (!rend)
				return;
			bool foundInMap = false;
			for (auto& wad : rend->wads)
			{
				std::string tmppath = toLowerCase(wad->filename);
				if (tmppath.find(basename(pathlowercase)) != std::string::npos)
				{
					foundInMap = true;
					print_log(get_localized_string(LANG_0340));
					break;
				}
			}

			if (!foundInMap)
			{
				Wad* wad = new Wad(file);
				if (wad->readInfo())
				{
					rend->wads.push_back(wad);
					if (!ends_with(map->ents[0]->keyvalues["wad"], ";"))
						map->ents[0]->keyvalues["wad"] += ";";
					map->ents[0]->keyvalues["wad"] += basename(file) + ";";
					map->update_ent_lump();
					app->updateEnts();
					app->reloading = true;
					rend->reload();
					app->reloading = false;
				}
				else
					delete wad;
			}
		}
	}
	else if (ends_with(pathlowercase, ".mdl"))
	{
		Bsp* tmpMap = new Bsp(file);
		tmpMap->is_mdl_model = true;
		app->addMap(tmpMap);
		app->selectMap(tmpMap);
	}
	else if (ends_with(pathlowercase, ".spr"))
	{
		Bsp* tmpMap = new Bsp(file);
		tmpMap->is_mdl_model = true;
		app->addMap(tmpMap);
		app->selectMap(tmpMap);
	}
	else if (ends_with(pathlowercase, ".csm"))
	{
		Bsp* tmpMap = new Bsp(file);
		tmpMap->is_mdl_model = true;
		app->addMap(tmpMap);
		app->selectMap(tmpMap);
	}
	else
	{
		if (!ends_with(pathlowercase, ".bsp"))
		{
			print_log(get_localized_string(LANG_0898), file);
		}
		Bsp* tmpMap = new Bsp(file);
		app->addMap(tmpMap);
		app->selectMap(tmpMap);
	}
}

void Gui::drawToolbar()
{
	ImVec2 window_pos = ImVec2(10.0f, 35.0f);
	ImVec2 window_pos_pivot = ImVec2(0.0f, 0.0f);
	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
	if (ImGui::Begin(get_localized_string(LANG_0606).c_str(), 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
	{
		ImGuiStyle& style = ImGui::GetStyle();
		style.FrameBorderSize = 1.0f;
		ImGuiContext& g = *GImGui;
		ImVec4 dimColor = style.Colors[ImGuiCol_FrameBg];
		ImVec4 selectColor = style.Colors[ImGuiCol_FrameBgActive];
		float iconWidth = (fontSize / 22.0f) * 32;
		ImVec2 iconSize = ImVec2(iconWidth, iconWidth);
		ImVec2 iconSize_big = ImVec2(iconWidth * 2, iconWidth * 2);

		ImVec4 testColor = ImVec4(1, 0, 0, 1);
		selectColor.x *= selectColor.w;
		selectColor.y *= selectColor.w;
		selectColor.z *= selectColor.w;
		selectColor.w = 1;

		dimColor.x *= dimColor.w;
		dimColor.y *= dimColor.w;
		dimColor.z *= dimColor.w;
		dimColor.w = 1;

		ImGui::PushStyleColor(ImGuiCol_Button, app->pickMode == PICK_OBJECT ? selectColor : dimColor);
		ImGui::PushStyleColor(ImGuiCol_Border, app->pickMode == PICK_OBJECT ? dimColor : selectColor);
		if (ImGui::ImageButton("##pickobj", (ImTextureID)(size_t)objectIconTexture->id, iconSize, ImVec2(0, 0), ImVec2(1, 1)))
		{
			if (app->pickMode != PICK_OBJECT)
			{
				app->deselectFaces();
				app->deselectObject();
				app->pickMode = PICK_OBJECT;
				showFaceEditWidget = false;
			}
		}
		ImGui::PopStyleColor(2);
		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
		{
			ImGui::BeginTooltip();
			ImGui::ImageButton("##pickobj_big", (ImTextureID)(size_t)objectIconTexture->id, iconSize_big, ImVec2(0, 0), ImVec2(1, 1));
			ImGui::TextUnformatted(get_localized_string(LANG_0607).c_str());
			ImGui::EndTooltip();
		}

		ImGui::PushStyleColor(ImGuiCol_Button, app->pickMode == PICK_FACE ? selectColor : dimColor);
		ImGui::PushStyleColor(ImGuiCol_Border, app->pickMode == PICK_FACE ? dimColor : selectColor);
		ImGui::SameLine();
		if (ImGui::ImageButton("##pickface", (ImTextureID)(size_t)faceIconTexture->id, iconSize, ImVec2(0, 0), ImVec2(1, 1)))
		{
			if (app->pickMode == PICK_OBJECT && app->pickInfo.selectedEnts.size() > 1)
			{
				app->deselectObject(true);
				pickCount++;
			}
			showFaceEditWidget = true;
			app->pickMode = PICK_FACE;
		}
		ImGui::PopStyleColor(2);
		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
		{
			ImGui::BeginTooltip();
			ImGui::ImageButton("##pickface_big", (ImTextureID)(size_t)faceIconTexture->id, iconSize_big, ImVec2(0, 0), ImVec2(1, 1));
			ImGui::TextUnformatted(get_localized_string(LANG_0608).c_str());
			ImGui::EndTooltip();
		}

		ImGui::PushStyleColor(ImGuiCol_Button, app->pickMode == PICK_FACE_LEAF ? selectColor : dimColor);
		ImGui::PushStyleColor(ImGuiCol_Border, app->pickMode == PICK_FACE_LEAF ? dimColor : selectColor);
		ImGui::SameLine();
		if (ImGui::ImageButton("##pickleaf", (ImTextureID)(size_t)leafIconTexture->id, iconSize, ImVec2(0, 0), ImVec2(1, 1)))
		{
			if (app->pickMode == PICK_OBJECT && app->pickInfo.selectedEnts.size() > 1)
			{
				app->deselectObject(true);
				pickCount++;
			}
			showFaceEditWidget = true;
			app->pickMode = PICK_FACE_LEAF;
		}
		ImGui::PopStyleColor(2);
		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
		{
			ImGui::BeginTooltip();
			ImGui::ImageButton("##pickleaf_big", (ImTextureID)(size_t)leafIconTexture->id, iconSize_big, ImVec2(0, 0), ImVec2(1, 1));
			ImGui::TextUnformatted(get_localized_string("FACE_LEAF_MODE").c_str());
			ImGui::EndTooltip();
		}
	}
	ImGui::End();
}

void Gui::drawPanelsToolbar()
{
	ImGuiStyle& style = ImGui::GetStyle();
	float iconWidth = (fontSize / 22.0f) * 32.0f;
	float modeToolbarHeight = iconWidth + style.FramePadding.y * 2.0f + style.WindowPadding.y * 2.0f;
	ImVec2 window_pos = ImVec2(10.0f, 35.0f + modeToolbarHeight + 6.0f);

	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
	if (ImGui::Begin("##PanelsToolbar", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
	{
		style.FrameBorderSize = 1.0f;
		ImGuiContext& g = *GImGui;
		ImVec4 dimColor = style.Colors[ImGuiCol_FrameBg];
		ImVec4 selectColor = style.Colors[ImGuiCol_FrameBgActive];
		ImVec2 iconSize = ImVec2(iconWidth, iconWidth);
		ImVec2 iconSize_big = ImVec2(iconWidth * 2.0f, iconWidth * 2.0f);

		selectColor.x *= selectColor.w;
		selectColor.y *= selectColor.w;
		selectColor.z *= selectColor.w;
		selectColor.w = 1.0f;

		dimColor.x *= dimColor.w;
		dimColor.y *= dimColor.w;
		dimColor.z *= dimColor.w;
		dimColor.w = 1.0f;

		auto drawPanelButton = [&](const char* id, Texture* iconTex, bool& stateFlag, int langId, const char* fallbackLabel, const char* tooltipDesc)
		{
			Texture* tex = iconTex ? iconTex : objectIconTexture;
			ImGui::PushStyleColor(ImGuiCol_Button, stateFlag ? selectColor : dimColor);
			ImGui::PushStyleColor(ImGuiCol_Border, stateFlag ? dimColor : selectColor);
			if (ImGui::ImageButton(id, (ImTextureID)(size_t)tex->id, iconSize, ImVec2(0, 0), ImVec2(1, 1)))
			{
				stateFlag = !stateFlag;
			}
			ImGui::PopStyleColor(2);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::ImageButton(fmt::format("{}_big", id).c_str(), (ImTextureID)(size_t)tex->id, iconSize_big, ImVec2(0, 0), ImVec2(1, 1));
				std::string title = (langId > 0) ? get_localized_string(langId) : fallbackLabel;
				ImGui::TextUnformatted(title.c_str());
				if (tooltipDesc && strlen(tooltipDesc) > 0)
				{
					ImGui::TextDisabled("%s", tooltipDesc);
				}
				ImGui::EndTooltip();
			}
		};

		// 1. Entity Keyvalues & SmartEdit
		drawPanelButton("##btn_keyvalues", keyvaluesIconTexture, showKeyvalueWidget, LANG_0596, "Keyvalues", "Toggle Entity Keyvalue & SmartEdit inspector panel (Alt+Enter)");

		// 2. 3D Transform Tool
		drawPanelButton("##btn_transform", transformIconTexture, showTransformWidget, LANG_1160, "Transform Tool", "Toggle 3D coordinate transform manipulator panel (Ctrl+M)");

		// 3. Face Editor
		drawPanelButton("##btn_face_editor", faceEditorIconTexture, showFaceEditWidget, LANG_0597, "Face Editor", "Toggle Face texture alignment, shift offsets, and scale panel (F6)");

		// 4. Texture Browser
		drawPanelButton("##btn_texture_browser", textureBrowserIconTexture, showTextureBrowser, LANG_0598, "Texture Browser", "Toggle visual texture browser for embedded and WAD assets (F4)");

		// 5. Lightmap Editor
		drawPanelButton("##btn_lightmap", lightmapIconTexture, showLightmapEditorWidget, LANG_0599, "Lightmap Editor", "Toggle face lightmap luminance and RGB color editor panel");

		// 6. Log Console
		drawPanelButton("##btn_log", logIconTexture, showLogWidget, LANG_0594, "Log Console", "Toggle bspguy diagnostic output log window (F5)");

		// 7. Debug / PVS Inspection
		drawPanelButton("##btn_debug", debugIconTexture, showDebugWidget, LANG_0595, "Debug PVS", "Toggle low-level BSP engine debug and PVS inspection panel");

		// 8. Map Overview
		drawPanelButton("##btn_overview", overviewIconTexture, showOverviewWidget, 0, "Map Overview", "Toggle 2D radar/overview map rendering controls");

		// 9. Map Merger
		drawPanelButton("##btn_merge", mergeIconTexture, showMergeMapWidget, LANG_0825, "Map Merger", "Toggle multi-map merger tool window");
	}
	ImGui::End();
}

void Gui::drawFpsOverlay()
{
	ImVec2 window_pos = ImVec2(imgui_io->DisplaySize.x - 10.0f, 35.0f);
	ImVec2 window_pos_pivot = ImVec2(1.0f, 0.0f);
	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
	ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
	if (ImGui::Begin(get_localized_string(LANG_0609).c_str(), 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
	{
		// ImGui::TextUnformatted(fmt::format("{} FPS", current_fps).c_str());
		ImGui::TextUnformatted(fmt::format("{:.0f} FPS", imgui_io->Framerate).c_str());
		if (ImGui::BeginPopupContextWindow())
		{
			ImGui::Checkbox(get_localized_string(LANG_0611).c_str(), &g_settings.vsync);
			ImGui::EndPopup();
		}
	}
	ImGui::End();
}

void Gui::drawStatusMessage()
{
	static float windowWidth = 32;
	static float loadingWindowWidth = 32;
	static float loadingWindowHeight = 32;

	bool selectedEntity = false;
	Bsp* map = app->getSelectedMap();
	for (auto& i : app->pickInfo.selectedEnts)
	{
		if (map && i > 0 && (map->ents[i]->getBspModelIdx() < 0 || map->ents[i]->isWorldSpawn()))
		{
			selectedEntity = true;
			break;
		}
	}

	bool showStatus = (app->invalidSolid && selectedEntity) || (!app->isTransformableSolid && selectedEntity) || badSurfaceExtents || lightmapTooLarge || app->modelUsesSharedStructures;

	if (showStatus)
	{
		ImVec2 window_pos = ImVec2((app->windowWidth - windowWidth) / 2.f, app->windowHeight - 10.f);
		ImVec2 window_pos_pivot = ImVec2(0.0f, 1.0f);
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
		ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background

		if (ImGui::Begin(get_localized_string(LANG_0612).c_str(), 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
		{
			if (app->modelUsesSharedStructures)
			{
				if (app->transformMode == TRANSFORM_MODE_MOVE && !app->moveOrigin)
					ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), get_localized_string(LANG_0613).c_str());
				else
					ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), get_localized_string(LANG_0614).c_str());
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"Model shares planes/clipnodes with other models.\n\nNeed duplicate the model to enable model editing.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (!app->isTransformableSolid && app->pickInfo.selectedEnts.size() > 0 && app->pickInfo.selectedEnts[0] >= 0)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), get_localized_string(LANG_0615).c_str());
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"Scaling and vertex manipulation don't work with concave solids yet\n";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (app->invalidSolid && app->pickInfo.selectedEnts.size() > 0 && app->pickInfo.selectedEnts[0] >= 0)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), get_localized_string(LANG_0616).c_str());
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"The selected solid is not convex or has non-planar faces.\n\n"
						"Transformations will be reverted unless you fix this.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (badSurfaceExtents)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), get_localized_string(LANG_0617).c_str());
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"One or more of the selected faces contain too many texture pixels on some axis.\n\n"
						"This will crash the game. Increase texture scale to fix.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (lightmapTooLarge)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), get_localized_string(LANG_0618).c_str());
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"One or more of the selected faces contain too many texture pixels.\n\n"
						"This will crash the game. Increase texture scale to fix.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			windowWidth = ImGui::GetWindowWidth();
		}
		ImGui::End();
	}

	if (app->isLoading)
	{
		ImVec2 window_pos = ImVec2((app->windowWidth - loadingWindowWidth) / 2,
								   (app->windowHeight - loadingWindowHeight) / 2);
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);

		if (ImGui::Begin(get_localized_string(LANG_0619).c_str(), 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
		{
			static clock_t lastTick = clock();
			static int loadTick = 0;

			if (clock() - lastTick / (float)CLOCKS_PER_SEC > 0.05f)
			{
				loadTick = (loadTick + 1) % 8;
				lastTick = clock();
			}

			ImGui::PushFont(consoleFontLarge);
			switch (loadTick)
			{
				case 0:
					ImGui::Text(get_localized_string(LANG_0620).c_str());
					break;
				case 1:
					ImGui::Text(get_localized_string(LANG_0621).c_str());
					break;
				case 2:
					ImGui::Text(get_localized_string(LANG_0622).c_str());
					break;
				case 3:
					ImGui::Text(get_localized_string(LANG_0623).c_str());
					break;
				case 4:
					ImGui::Text(get_localized_string(LANG_1097).c_str());
					break;
				case 5:
					ImGui::Text(get_localized_string(LANG_1098).c_str());
					break;
				case 6:
					ImGui::Text(get_localized_string(LANG_1099).c_str());
					break;
				case 7:
					ImGui::Text(get_localized_string(LANG_1162).c_str());
					break;
				default:
					break;
			}
			ImGui::PopFont();
		}
		loadingWindowWidth = ImGui::GetWindowWidth();
		loadingWindowHeight = ImGui::GetWindowHeight();

		ImGui::End();
	}
}

void Gui::reloadLimits()
{
	if (!showLimitsWidget)
	{
		limitsInvalidated = true;
		return;
	}
	for (int i = 0; i < SORT_MODES; i++)
	{
		loadedLimit[i] = false;
	}
	loadedStats = false;
	limitsInvalidated = false;
}

void Gui::checkValidHulls()
{
	for (int i = 0; i < MAX_MAP_HULLS; i++)
	{
		anyHullValid[i] = false;
		for (size_t k = 0; k < mapRenderers.size() && !anyHullValid[i]; k++)
		{
			Bsp* map = mapRenderers[k]->map;

			for (int m = 0; m < map->modelCount; m++)
			{
				if (map->models[m].iHeadnodes[i] >= 0)
				{
					anyHullValid[i] = true;
					break;
				}
			}
		}
	}
}

void Gui::checkFaceErrors()
{
	lightmapTooLarge = badSurfaceExtents = false;

	Bsp* map = app->getSelectedMap();
	if (!map)
		return;

	for (size_t i = 0; i < app->pickInfo.selectedFaces.size(); i++)
	{
		int size[2];
		map->GetFaceLightmapSize((int)app->pickInfo.selectedFaces[i], size);
		if ((size[0] > g_limits.maxSurfaceExtent) || (size[1] > g_limits.maxSurfaceExtent) || size[0] < 0 || size[1] < 0)
		{
			print_log(get_localized_string(LANG_0426), size[0], size[1]);
			size[0] = std::min(size[0], g_limits.maxSurfaceExtent);
			size[1] = std::min(size[1], g_limits.maxSurfaceExtent);
			badSurfaceExtents = true;
		}

		if (size[0] * size[1] > MAX_LUXELS)
		{
			lightmapTooLarge = true;
		}
	}
}

void Gui::refresh()
{
	reloadLimits();
	checkValidHulls();
}
void Gui::setupTheme()
{
	constexpr ImVec4 COLOR_DEEP_OBSIDIAN = ImVec4(0.043f, 0.047f, 0.063f, 1.000f);
	constexpr ImVec4 COLOR_BLOOD_CRIMSON = ImVec4(0.545f, 0.078f, 0.165f, 1.000f);
	constexpr ImVec4 COLOR_NIGHTMARE_PURPLE = ImVec4(0.149f, 0.161f, 0.290f, 1.000f);
	constexpr ImVec4 COLOR_GARGOYLE_GREY = ImVec4(0.431f, 0.478f, 0.525f, 1.000f);
	constexpr ImVec4 COLOR_VELLUM_CREAM = ImVec4(0.890f, 0.835f, 0.722f, 1.000f);

	auto applyAlpha = [](const ImVec4& color, float alpha)
	{ return ImVec4(color.x, color.y, color.z, alpha); };

	ImGuiStyle& style = ImGui::GetStyle();
	style.Colors[ImGuiCol_Text] = applyAlpha(COLOR_VELLUM_CREAM, 1.00f);
	style.Colors[ImGuiCol_TextDisabled] = applyAlpha(COLOR_GARGOYLE_GREY, 0.80f);
	style.Colors[ImGuiCol_WindowBg] = applyAlpha(COLOR_DEEP_OBSIDIAN, 1.00f);
	style.Colors[ImGuiCol_ChildBg] = applyAlpha(COLOR_DEEP_OBSIDIAN, 0.94f);
	style.Colors[ImGuiCol_PopupBg] = applyAlpha(COLOR_DEEP_OBSIDIAN, 0.96f);
	style.Colors[ImGuiCol_Border] = applyAlpha(COLOR_GARGOYLE_GREY, 0.45f);
	style.Colors[ImGuiCol_BorderShadow] = applyAlpha(ImVec4(0, 0, 0, 0), 0.00f);
	style.Colors[ImGuiCol_FrameBg] = applyAlpha(COLOR_NIGHTMARE_PURPLE, 0.75f);
	style.Colors[ImGuiCol_FrameBgHovered] = applyAlpha(COLOR_NIGHTMARE_PURPLE, 1.00f);
	style.Colors[ImGuiCol_FrameBgActive] = applyAlpha(COLOR_BLOOD_CRIMSON, 0.70f);
	style.Colors[ImGuiCol_TitleBg] = applyAlpha(COLOR_DEEP_OBSIDIAN, 1.00f);
	style.Colors[ImGuiCol_TitleBgActive] = applyAlpha(COLOR_NIGHTMARE_PURPLE, 1.00f);
	style.Colors[ImGuiCol_TitleBgCollapsed] = applyAlpha(COLOR_DEEP_OBSIDIAN, 0.75f);
	style.Colors[ImGuiCol_MenuBarBg] = applyAlpha(COLOR_DEEP_OBSIDIAN, 1.00f);
	style.Colors[ImGuiCol_ScrollbarBg] = applyAlpha(COLOR_DEEP_OBSIDIAN, 0.60f);
	style.Colors[ImGuiCol_ScrollbarGrab] = applyAlpha(COLOR_GARGOYLE_GREY, 0.50f);
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = applyAlpha(COLOR_GARGOYLE_GREY, 0.80f);
	style.Colors[ImGuiCol_ScrollbarGrabActive] = applyAlpha(COLOR_BLOOD_CRIMSON, 0.90f);
	style.Colors[ImGuiCol_CheckMark] = applyAlpha(COLOR_VELLUM_CREAM, 1.00f);
	style.Colors[ImGuiCol_SliderGrab] = applyAlpha(COLOR_GARGOYLE_GREY, 0.80f);
	style.Colors[ImGuiCol_SliderGrabActive] = applyAlpha(COLOR_BLOOD_CRIMSON, 1.00f);
	style.Colors[ImGuiCol_Button] = applyAlpha(COLOR_NIGHTMARE_PURPLE, 0.85f);
	style.Colors[ImGuiCol_ButtonHovered] = applyAlpha(COLOR_BLOOD_CRIMSON, 0.70f);
	style.Colors[ImGuiCol_ButtonActive] = applyAlpha(COLOR_BLOOD_CRIMSON, 1.00f);
	style.Colors[ImGuiCol_Header] = applyAlpha(COLOR_NIGHTMARE_PURPLE, 0.60f);
	style.Colors[ImGuiCol_HeaderHovered] = applyAlpha(COLOR_BLOOD_CRIMSON, 0.65f);
	style.Colors[ImGuiCol_HeaderActive] = applyAlpha(COLOR_BLOOD_CRIMSON, 0.90f);
	style.Colors[ImGuiCol_Separator] = applyAlpha(COLOR_GARGOYLE_GREY, 0.40f);
	style.Colors[ImGuiCol_SeparatorHovered] = applyAlpha(COLOR_BLOOD_CRIMSON, 0.75f);
	style.Colors[ImGuiCol_SeparatorActive] = applyAlpha(COLOR_BLOOD_CRIMSON, 1.00f);
	style.Colors[ImGuiCol_ResizeGrip] = applyAlpha(COLOR_GARGOYLE_GREY, 0.30f);
	style.Colors[ImGuiCol_ResizeGripHovered] = applyAlpha(COLOR_BLOOD_CRIMSON, 0.70f);
	style.Colors[ImGuiCol_ResizeGripActive] = applyAlpha(COLOR_BLOOD_CRIMSON, 1.00f);
	style.Colors[ImGuiCol_Tab] = applyAlpha(COLOR_NIGHTMARE_PURPLE, 0.70f);
	style.Colors[ImGuiCol_TabHovered] = applyAlpha(COLOR_BLOOD_CRIMSON, 0.75f);
	style.Colors[ImGuiCol_TabActive] = applyAlpha(COLOR_BLOOD_CRIMSON, 0.95f);
	style.Colors[ImGuiCol_TabUnfocused] = applyAlpha(COLOR_NIGHTMARE_PURPLE, 0.40f);
	style.Colors[ImGuiCol_TabUnfocusedActive] = applyAlpha(COLOR_NIGHTMARE_PURPLE, 0.80f);
	style.Colors[ImGuiCol_PlotLines] = applyAlpha(COLOR_GARGOYLE_GREY, 1.00f);
	style.Colors[ImGuiCol_PlotLinesHovered] = applyAlpha(COLOR_BLOOD_CRIMSON, 1.00f);
	style.Colors[ImGuiCol_PlotHistogram] = applyAlpha(COLOR_BLOOD_CRIMSON, 0.85f);
	style.Colors[ImGuiCol_PlotHistogramHovered] = applyAlpha(COLOR_VELLUM_CREAM, 1.00f);
	style.Colors[ImGuiCol_TableHeaderBg] = applyAlpha(COLOR_NIGHTMARE_PURPLE, 0.85f);
	style.Colors[ImGuiCol_TableBorderStrong] = applyAlpha(COLOR_GARGOYLE_GREY, 0.60f);
	style.Colors[ImGuiCol_TableBorderLight] = applyAlpha(COLOR_GARGOYLE_GREY, 0.30f);
	style.Colors[ImGuiCol_TableRowBg] = applyAlpha(COLOR_DEEP_OBSIDIAN, 0.00f);
	style.Colors[ImGuiCol_TableRowBgAlt] = applyAlpha(COLOR_NIGHTMARE_PURPLE, 0.20f);
	style.Colors[ImGuiCol_TextSelectedBg] = applyAlpha(COLOR_BLOOD_CRIMSON, 0.45f);
	style.Colors[ImGuiCol_DragDropTarget] = applyAlpha(COLOR_VELLUM_CREAM, 0.90f);
	style.Colors[ImGuiCol_NavHighlight] = applyAlpha(COLOR_BLOOD_CRIMSON, 1.00f);
	style.Colors[ImGuiCol_NavWindowingHighlight] = applyAlpha(COLOR_VELLUM_CREAM, 0.70f);
	style.Colors[ImGuiCol_NavWindowingDimBg] = applyAlpha(COLOR_DEEP_OBSIDIAN, 0.60f);
	style.Colors[ImGuiCol_ModalWindowDimBg] = applyAlpha(COLOR_DEEP_OBSIDIAN, 0.70f);
}
