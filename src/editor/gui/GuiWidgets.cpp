#include <unordered_set>
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
#include <filesystem>
#include <algorithm>
#include <cmath>

extern float g_tooltip_delay;

void Gui::drawDebugWidget()
{
	static std::map<std::string, std::set<std::string>> mapTexsUsage{};
	static double lastupdate = 0.0;

	if (!app)
		return;

	ImGui::SetNextWindowBgAlpha(0.75f);

	ImGui::SetNextWindowSize(ImVec2(300.f, 400.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(200.f, 200.f), ImVec2(app->windowWidth - 40.f, app->windowHeight - 40.f));

	Bsp* map = app->getSelectedMap();
	BspRenderer* renderer = map ? map->getBspRender() : NULL;
	auto entIdx = app->pickInfo.selectedEnts;

	if (ImGui::Begin(fmt::format("{}###DEBUG_WIDGET", get_localized_string(LANG_0624)).c_str(), &showDebugWidget))
	{
		if (ImGui::CollapsingHeader(get_localized_string(LANG_0625).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0366)), floatRound(cameraOrigin.x), floatRound(cameraOrigin.y), floatRound(cameraOrigin.z)).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0367)), floatRound(cameraAngles.x), floatRound(cameraAngles.y), floatRound(cameraAngles.z)).c_str());

			vec3 hlAngles = cameraAngles;
			hlAngles = hlAngles.unflipUV();
			hlAngles = hlAngles.normalize_angles();
			hlAngles.y -= 90.0f;

			ImGui::Text(fmt::format(fmt::runtime(get_localized_string("DEBUG_HL_ANGLES")), floatRound(hlAngles.x), floatRound(hlAngles.y), floatRound(hlAngles.z)).c_str());

			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0368)), (unsigned int)app->pickInfo.selectedFaces.size()).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0369)), app->pickMode).c_str());
		}

		if (ImGui::CollapsingHeader("DEBUG INFO", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text(fmt::format("Mouse: {} {}", mousePos.x, mousePos.y).c_str());
			ImGui::Text(fmt::format("Workdir: {}", g_working_dir).c_str());
			if (imgui_io)
			{
				ImGui::Text(fmt::format("Opengl Errors: {} ", app->gl_errors).c_str());
				if (renderer)
					ImGui::Text(fmt::format("lmGen: {}.lmUpload: {}.lm: {}.", renderer->lightmapsGenerated, renderer->lightmapsUploaded, renderer->lightmaps != NULL).c_str());
				ImGui::Text(fmt::format("Mouse left {} right {}", app->curLeftMouse, app->curRightMouse).c_str());
				std::string keysStr;
				for (int key = 0; key < GLFW_KEY_LAST; key++)
				{
					if (app->pressed[key])
					{
						const char* keyName = glfwGetKeyName(key, 0);
						if (keyName != NULL)
						{
							keysStr += std::string(keyName) + " ";
						}
						else
						{
							keysStr += "C:" + std::to_string(key) + " ";
						}
					}
				}

				ImGui::Text("Pick count: %d. \nVert pick count: %d", pickCount, vertPickCount);
				ImGui::Text("Model verts: %zu. \nModel faces: %zu", app->modelVerts.size(), app->modelFaceVerts.size());
				ImGui::Text("KEYS: %s", keysStr.c_str());
				ImGui::Text(fmt::format("Time: {}", (float)app->curTime).c_str());
				ImGui::Text(fmt::format("canControl:{}\noldControl:{}\nNo WantTextInput:{}", app->canControl, app->oldControl, !imgui_io->WantTextInput).c_str());
				ImGui::Text(fmt::format("No WantCaptureMouseUnlessPopupClose:{}", !imgui_io->WantCaptureMouseUnlessPopupClose).c_str());
				ImGui::Text(fmt::format("No WantCaptureMouse:{}", !imgui_io->WantCaptureMouse).c_str());
				// ImGui::Text(fmt::format("BlockMoving:{}", app->blockMoving).c_str());
				ImGui::Text(fmt::format("MoveDir: [{}]", app->getMoveDir().toString()).c_str());

				static double movemulttime = app->curTime;
				static double movemult = (app->curTime - app->oldTime) * app->moveSpeed;
				static vec3 nextOrigin = app->getMoveDir() * (float)(app->curTime - app->oldTime) * app->moveSpeed;

				if (fabs(app->curTime - movemulttime) > 0.5)
				{
					movemult = (app->curTime - app->oldTime) * app->moveSpeed;
					movemulttime = app->curTime;
					nextOrigin = app->getMoveDir() * (float)(app->curTime - app->oldTime) * app->moveSpeed;
				}

				ImGui::Text(fmt::format("MoveDir mult: [{}]", movemult).c_str());
				ImGui::Text(fmt::format("MoveSpeed: [{}]", app->moveSpeed).c_str());
				ImGui::Text(fmt::format("nextOrigin: [{}]", nextOrigin.toString()).c_str());
			}
		}

		if (ImGui::CollapsingHeader(get_localized_string(LANG_1100).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (!map)
			{
				ImGui::Text(get_localized_string(LANG_0626).c_str());
			}
			else
			{
				ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0370)), map->bsp_name.c_str()).c_str());

				if (ImGui::CollapsingHeader(get_localized_string(LANG_0627).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (app->pickInfo.selectedEnts.size())
					{
						ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0371)), entIdx[0]).c_str());
					}

					int modelIdx = -1;

					if (entIdx.size())
					{
						modelIdx = map->ents[entIdx[0]]->getBspModelIdx();
					}

					if (modelIdx > 0)
					{
						ImGui::Checkbox(get_localized_string(LANG_0628).c_str(), &app->debugClipnodes);
						ImGui::SliderInt(get_localized_string(LANG_0629).c_str(), &app->debugInt, 0, app->debugIntMax);

						ImGui::Checkbox(get_localized_string(LANG_0630).c_str(), &app->debugNodes);
						ImGui::SliderInt(get_localized_string(LANG_0631).c_str(), &app->debugNode, 0, app->debugNodeMax);
					}

					if (modelIdx >= 0)
					{
						ImGui::TextUnformatted(fmt::format("Model{}.FirstFace:{}", modelIdx, map->models[modelIdx].iFirstFace).c_str());
						ImGui::TextUnformatted(fmt::format("Model{}.NumFace:{}", modelIdx, map->models[modelIdx].nFaces).c_str());
					}

					if (app->pickInfo.selectedFaces.size())
					{
						BSPFACE32& face = map->faces[app->pickInfo.selectedFaces[0]];

						if (modelIdx > 0)
						{
							BSPMODEL& model = map->models[modelIdx];
							ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0372)), modelIdx).c_str());

							ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0373)), model.nFaces).c_str());
						}

						ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0374)), app->pickInfo.selectedFaces[0]).c_str());
						ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0375)), face.iPlane).c_str());

						if (face.iTextureInfo < map->texinfoCount)
						{
							BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
							if (info.iMiptex >= 0 && info.iMiptex < map->textureCount)
							{
								int texOffset = ((int*)map->textures)[info.iMiptex + 1];
								if (texOffset >= 0)
								{
									BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
									ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0376)), face.iTextureInfo).c_str());
									ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0377)), info.iMiptex).c_str());
									ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0378)), tex.szName, tex.nWidth, tex.nHeight).c_str());
								}
							}
							BSPPLANE& plane = map->planes[face.iPlane];
							BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
							float anglex, angley;
							vec3 xv, yv;
							int val = TextureAxisFromPlane(plane, xv, yv);
							ImGui::Text(fmt::format("Plane type {} : axis ({}x{})", val, anglex = AngleFromTextureAxis(texinfo.vS, true, val),
													angley = AngleFromTextureAxis(texinfo.vT, false, val))
											.c_str());
							ImGui::Text(fmt::format("Texinfo: {}/{}/{} + {} / {}/{}/{} + {} ", texinfo.vS.x, texinfo.vS.y, texinfo.vS.z, texinfo.shiftS,
													texinfo.vT.x, texinfo.vT.y, texinfo.vT.z, texinfo.shiftT)
											.c_str());

							xv = AxisFromTextureAngle(anglex, true, val);
							yv = AxisFromTextureAngle(angley, false, val);

							ImGui::Text(fmt::format("AxisBack: {}/{}/{} + {} / {}/{}/{} + {} ", xv.x, xv.y, xv.z, texinfo.shiftS,
													yv.x, yv.y, yv.z, texinfo.shiftT)
											.c_str());
						}
						ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0379)), face.nLightmapOffset).c_str());
					}
				}
			}
		}
		int modelIdx = -1;

		if (map && entIdx.size())
		{
			modelIdx = map->ents[entIdx[0]]->getBspModelIdx();
		}

		std::string bspTreeTitle = "BSP Tree";
		if (modelIdx >= 0)
		{
			bspTreeTitle += " (Model " + std::to_string(modelIdx) + ")";
		}

		if (ImGui::CollapsingHeader((bspTreeTitle + "##bsptree").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (modelIdx < 0 && entIdx.size())
				modelIdx = 0;
			if (modelIdx >= 0)
			{
				if (!map || !renderer)
				{
					ImGui::Text(get_localized_string(LANG_0632).c_str());
				}
				else
				{
					static ImVec4 hullColors[] = {
						ImVec4(1, 1, 1, 1),
						ImVec4(0.3f, 1, 1, 1),
						ImVec4(1, 0.3f, 1, 1),
						ImVec4(1, 1, 0.3f, 1),
					};

					for (int i = 0; i < MAX_MAP_HULLS; i++)
					{
						std::vector<int> nodeBranch;
						int leafIdx;
						int childIdx = -1;
						int headNode = map->models[modelIdx].iHeadnodes[i];
						int contents = map->pointContents(headNode, renderer->localCameraOrigin, i, nodeBranch, leafIdx, childIdx);

						ImGui::PushStyleColor(ImGuiCol_Text, hullColors[i]);
						if (ImGui::TreeNode(("HULL " + std::to_string(i)).c_str()))
						{
							ImGui::Indent();
							ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0380)), map->getLeafContentsName(contents)).c_str());
							if (i == 0)
							{
								ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0381)), leafIdx).c_str());
							}
							else if (i == 3 && g_app->debugLeafNavMesh)
							{
								int tmpLeafIdx = map->get_leaf(renderer->localCameraOrigin, 3);
								int leafNavIdx = -1;

								if (tmpLeafIdx >= 0 && tmpLeafIdx < MAX_MAP_CLIPNODE_LEAVES)
								{
									leafNavIdx = g_app->debugLeafNavMesh->leafMap[tmpLeafIdx];
								}

								ImGui::Text("Nav ID: %d", leafNavIdx);
							}
							ImGui::Text(fmt::format("Parent Node: {} (child {})",
													nodeBranch.size() ? nodeBranch[nodeBranch.size() - 1] : headNode,
													childIdx)
											.c_str());
							ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0382)), headNode).c_str());
							ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0383)), nodeBranch.size()).c_str());

							ImGui::Unindent();
							ImGui::TreePop();
						}
						ImGui::PopStyleColor();
					}
				}
			}
			else
			{
				ImGui::Text(get_localized_string(LANG_0633).c_str());
			}
		}

		if (map && ImGui::CollapsingHeader(get_localized_string(LANG_0634).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			int InternalTextures = 0;
			int TotalInternalTextures = 0;
			int WadTextures = 0;

			for (int i = 0; i < map->textureCount; i++)
			{
				int oldOffset = ((int*)map->textures)[i + 1];
				if (oldOffset > 0)
				{
					BSPMIPTEX* bspTex = (BSPMIPTEX*)(map->textures + oldOffset);
					if (bspTex->nOffsets[0] > 0)
					{
						TotalInternalTextures++;
					}
				}
			}

			if (mapTexsUsage.size())
			{
				for (auto& tmpWad : mapTexsUsage)
				{
					if (tmpWad.first == "internal")
						InternalTextures += (int)tmpWad.second.size();
					else
						WadTextures += (int)tmpWad.second.size();
				}
			}

			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0384)), map->textureCount).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0385)), InternalTextures, TotalInternalTextures).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0386)), TotalInternalTextures > 0 ? (int)mapTexsUsage.size() - 1 : (int)mapTexsUsage.size()).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0387)), WadTextures).c_str());

			for (auto& tmpWad : mapTexsUsage)
			{
				if (ImGui::CollapsingHeader((tmpWad.first + "##debug").c_str(), ImGuiTreeNodeFlags_::ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_::ImGuiTreeNodeFlags_Framed))
				{
					for (auto& texName : tmpWad.second)
					{
						ImGui::Text(texName.c_str());
					}
				}
			}
		}

		if (map && renderer && ImGui::CollapsingHeader(get_localized_string(LANG_1101).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text(get_localized_string(LANG_0635).c_str(), renderer->intersectVec.x, renderer->intersectVec.y, renderer->intersectVec.z);
			ImGui::Text(get_localized_string(LANG_0636).c_str(), app->debugVec1.x, app->debugVec1.y, app->debugVec1.z);
			ImGui::Text(get_localized_string(LANG_0637).c_str(), app->debugVec2.x, app->debugVec2.y, app->debugVec2.z);
			ImGui::Text(get_localized_string(LANG_0638).c_str(), app->debugVec3.x, app->debugVec3.y, app->debugVec3.z);

			drawUndoMemUsage(renderer);

			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0388)), app->isTransformableSolid).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0389)), app->isScalingObject).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0390)), app->isMovingOrigin).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0391)), app->isTransformingValid).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0392)), app->isTransformingWorld).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0393)), app->transformMode).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0394)), app->transformTarget).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0395)), app->modelUsesSharedStructures).c_str());

			ImGui::Text(fmt::format("showDragAxes {}\nmovingEnt {}\nanyAltPressed {}",
									app->showDragAxes, app->movingEnt, app->anyAltPressed)
							.c_str());

			ImGui::Text(fmt::format("hoverAxis:{}", app->hoverAxis).c_str());

			ImGui::Text(fmt::format("anyVertSelected:{}", app->anyVertSelected).c_str());
			ImGui::Text(fmt::format("anyEdgeSelected:{}", app->anyEdgeSelected).c_str());
			ImGui::Text(fmt::format("hoverEdge:{}", app->hoverEdge).c_str());
			ImGui::Text(fmt::format("hoverVert:{}", app->hoverVert).c_str());
			ImGui::Text(fmt::format("pickClickHeld:{}", app->pickClickHeld).c_str());

			ImGui::Checkbox(get_localized_string(LANG_0640).c_str(), &app->showDragAxes);
		}

		if (map)
		{
			if (ImGui::Button(get_localized_string(LANG_0641).c_str()))
			{
				for (auto& ent : map->ents)
				{
					if (ent->hasKey("classname") && ent->keyvalues["classname"] == "infodecal" && ent->hasKey("texture"))
					{
						map->decalShoot(ent->origin, ent->keyvalues["texture"]);
					}
				}
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(get_localized_string(LANG_0646).c_str());
				ImGui::TextUnformatted(get_localized_string(LANG_1102).c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}

			static int model1 = 0;
			static int model2 = 0;

			ImGui::DragInt(get_localized_string(LANG_0647).c_str(), &model1, 1, 0, g_limits.maxMapModels);

			ImGui::DragInt(get_localized_string(LANG_0648).c_str(), &model2, 1, 0, g_limits.maxMapModels);

			if (ImGui::Button(get_localized_string(LANG_0649).c_str()))
			{
				if (model1 >= 0 && model2 >= 0)
				{
					map->swap_two_models(model1, model2);
				}
			}

			if (ImGui::Button("Select faces pos plane"))
			{

				int leafIdx = 0;
				int planeIdx = -1;

				map->pointLeaf(map->models[0].iHeadnodes[0], cameraOrigin, 0, leafIdx, planeIdx);
				if (planeIdx >= 0)
				{
					auto faces = map->getFacesFromPlane(planeIdx);
					for (auto& f : faces)
					{
						renderer->highlightFace(f, 2);
					}
				}
			}

			ImGui::TextUnformatted("BEST LEAFS:");
			auto leaf_list = map->getLeafsFromPos(cameraOrigin, 32);
			for (auto& f : leaf_list)
			{
				ImGui::TextUnformatted(std::to_string(f).c_str());
			}

			if (ImGui::Button("Select best face"))
			{
				float minDist = 128.0f;
				int minFace = -1;

				for (int f = 0; f < map->faceCount; f++)
				{
					BSPFACE32& face = map->faces[f];

					if (map->texinfos[face.iTextureInfo].nFlags & TEX_SPECIAL)
					{
						continue;
					}

					auto& faceMath = renderer->faceMaths[f];

					vec3 normal = faceMath.normal.normalize();

					float distanceToPlane = dotProduct(normal, cameraOrigin) - faceMath.fdist;
					float dot = std::fabs(distanceToPlane);

					if (dot > minDist)
					{
						continue;
					}

					bool isInsideFace = true;
					const std::vector<vec3>& vertices = map->get_face_verts(f);

					for (size_t i = 0; i < vertices.size(); i++)
					{
						const vec3& v0 = vertices[i];
						const vec3& v1 = vertices[(i + 1) % vertices.size()];

						vec3 edge = v1 - v0;

						vec3 edgeNormal = crossProduct(normal, edge).normalize();

						if (dotProduct(edgeNormal, cameraOrigin - v0) > 0)
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
						minFace = f;
					}
				}
				if (minFace >= 0)
				{
					renderer->highlightFace(minFace, 2);
				}
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(get_localized_string(LANG_0650).c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		}
	}

	if (renderer && map && renderer->needReloadDebugTextures)
	{
		renderer->needReloadDebugTextures = false;
		lastupdate = app->curTime;
		mapTexsUsage.clear();

		for (int i = 0; i < map->faceCount; i++)
		{
			BSPTEXTUREINFO& texinfo = map->texinfos[map->faces[i].iTextureInfo];
			if (texinfo.iMiptex >= 0 && texinfo.iMiptex < map->textureCount)
			{
				int texOffset = ((int*)map->textures)[texinfo.iMiptex + 1];
				if (texOffset >= 0)
				{
					BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));

					if (tex.szName[0] != '\0')
					{
						if (tex.nOffsets[0] <= 0)
						{
							bool fondTex = false;
							for (auto& s : renderer->wads)
							{
								if (s->hasTexture(tex.szName))
								{
									if (!mapTexsUsage[basename(s->filename)].count(tex.szName))
										mapTexsUsage[basename(s->filename)].insert(tex.szName);

									fondTex = true;
								}
							}
							if (!fondTex)
							{
								if (!mapTexsUsage["notfound"].count(tex.szName))
									mapTexsUsage["notfound"].insert(tex.szName);
							}
						}
						else
						{
							if (!mapTexsUsage["internal"].count(tex.szName))
								mapTexsUsage["internal"].insert(tex.szName);
						}
					}
				}
			}
		}

		for (size_t i = 0; i < map->ents.size(); i++)
		{
			if (map->ents[i]->hasKey("classname") && map->ents[i]->keyvalues["classname"] == "infodecal")
			{
				if (map->ents[i]->hasKey("texture"))
				{
					std::string texture = map->ents[i]->keyvalues["texture"];
					if (!mapTexsUsage["decals.wad"].count(texture))
						mapTexsUsage["decals.wad"].insert(texture);
				}
			}
		}

		if (mapTexsUsage.size())
			print_log(get_localized_string(LANG_0396), (int)mapTexsUsage.size());
	}
	ImGui::End();
}

void Gui::drawOverviewWidget()
{
	static Bsp* lastMap = NULL;
	static bool updateFarNear = false;
	static std::string imgFormat = ".tga";
	if (updateFarNear)
	{
		updateFarNear = false;
		ortho_near = (ortho_maxs.z - ortho_mins.z) + cameraOrigin.z;
		ortho_far = (ortho_maxs.z - ortho_mins.z) * 2 + cameraOrigin.z;
	}

	ortho_overview = showOverviewWidget && orthoMode;

	Bsp* map = app->getSelectedMap();

	if (ImGui::Begin("Overview Widget###OVERVIEW_MAKER", &showOverviewWidget, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (lastMap != map)
		{
			lastMap = map;
			if (map)
				map->get_model_vertex_bounds(0, ortho_mins, ortho_maxs);
		}

		if (!map)
		{
			ImGui::Text("No selected map");
			ImGui::End();
			return;
		}

		/*		ImGui::SeparatorText("Custom Window Size");
				ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.3f);
				ImGui::DragFloat("Width", &ortho_custom_w, 1.0f, 256.0f, 2048.0f, "%.0f");
				ImGui::SameLine();
				ImGui::DragFloat("Height", &ortho_custom_h, 1.0f, 256.0f, 2048.0f, "%.0f");
				ImGui::PopItemWidth();
				*/
		ImGui::SeparatorText("Overview Settings");
		ImGui::Checkbox("Show Overview", &orthoMode);
		ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.3f);
		ImGui::DragFloat("Aspect Ratio", &ortho_custom_aspect, 0.01f, 0.5f, 2.0f, "%.2f");
		ImGui::DragFloat("Ortho FOV", &ortho_fov, 0.1f, 0.01f, 200.0f, "%.2f");
		ImGui::DragFloat("Ortho Near", &ortho_near, 1.0f, -1.0f, 8192.0f, "%.2f");
		ImGui::DragFloat("Ortho Far", &ortho_far, 1.0f, -1.0f, FLT_MAX, "%.2f");
		ImGui::PopItemWidth();

		ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.7f);
		ImGui::DragFloat3("Mins", &ortho_mins.x, 1.0f, -g_limits.fltMaxCoord, g_limits.fltMaxCoord, "%.0f");
		ImGui::DragFloat3("Maxs", &ortho_maxs.x, 1.0f, -g_limits.fltMaxCoord, g_limits.fltMaxCoord, "%.0f");
		ImGui::DragFloat3("Offset", &ortho_offset.x, 1.0f, -g_limits.fltMaxCoord, g_limits.fltMaxCoord, "%.0f");
		ImGui::PopItemWidth();

		ImGui::SeparatorText("Fill Overview Mins/Maxs");
		if (ImGui::Button("Fill from Model[0]"))
		{
			map->get_bounding_box(ortho_mins, ortho_maxs);
		}
		if (ImGui::Button("Fill from Verts"))
		{
			map->get_model_vertex_bounds(0, ortho_mins, ortho_maxs, true);
		}
		if (ImGui::Button("Fill from Leaves"))
		{
			ortho_mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
			ortho_maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
			for (int i = 1; i < map->leafCount; i++)
			{
				if (map->leaves[i].nContents == CONTENTS_EMPTY || map->leaves[i].nContents == CONTENTS_WATER)
				{
					expandBoundingBox(map->leaves[i].nMins, ortho_mins, ortho_maxs);
					expandBoundingBox(map->leaves[i].nMaxs, ortho_mins, ortho_maxs);
				}
			}
		}

		if (ImGui::Button("Calculate Far/Near"))
		{
			updateFarNear = true;
		}

		ImGui::SeparatorText("Save to TGA");
		ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.3f);
		ImGui::DragInt("Width###2", &ortho_tga_w, 1.0f, 256, 4096);
		ImGui::SameLine();
		ImGui::DragInt("Height###3", &ortho_tga_h, 1.0f, 256, 4096);
		ImGui::PopItemWidth();

		if (ImGui::Button("Save .tga"))
		{
			ortho_save_tga = true;
			imgFormat = ".tga";
		}
		ImGui::SameLine();
		if (ImGui::Button("Save .bmp"))
		{
			ortho_save_bmp = true;
			imgFormat = ".bmp";
		}
		ImGui::SameLine();
		if (ImGui::Button("Save FULL"))
		{
			ortho_save_png_full = true;
			imgFormat = ".png";
		}
		ImGui::SameLine();

		float x_size = ortho_maxs.x - ortho_mins.x;
		float y_size = ortho_maxs.y - ortho_mins.y;
		float zoomFriction = ((float)ortho_tga_w / (float)ortho_tga_h);
		float xScale, yScale;
		bool rotated = false;

		if (x_size < y_size)
		{
			xScale = 8192.0f / (x_size * zoomFriction);
			yScale = 8192.0f / y_size;
		}
		else
		{
			rotated = true;
			xScale = 8192.0f / x_size;
			yScale = 8192.0f / (y_size * zoomFriction);
		}

		float zoom = (xScale < yScale) ? xScale : yScale;

		vec3 origin = vec3((ortho_mins.x + ortho_maxs.x) / 2.0f + ortho_offset.x,
						   (ortho_mins.y + ortho_maxs.y) / 2.0f + ortho_offset.y,
						   (ortho_mins.z + ortho_maxs.z) / 2.0f + ortho_offset.z);

		if (ImGui::Button("Save .txt"))
		{
			FILE* overfile = NULL;
			std::string overPath = g_working_dir + "overviews/";
			createDir(overPath);
			std::string overFile = overPath + map->bsp_name + ".txt";
			fopen_s(&overfile, overFile.c_str(), "wb");
			if (overfile)
			{
				fprintf(overfile, "// overview description file for %s\n\n", map->bsp_name.c_str());
				fprintf(overfile, "global \n{\n");
				fprintf(overfile, "\tZOOM\t%.2f\n", zoom);
				fprintf(overfile, "\tORIGIN\t%.2f\t%.2f\t%.2f\n", origin.x, origin.y, ortho_mins.z + ortho_offset.z);
				fprintf(overfile, "\tROTATED\t%i\n}\n\n", rotated ? 1 : 0);
				fprintf(overfile, "layer \n{\n");
				fprintf(overfile, "\tIMAGE\t\"overviews/%s%s\"\n", map->bsp_name.c_str(), imgFormat.c_str());
				fprintf(overfile, "\tHEIGHT\t%.2f\n}\n", ortho_mins.z + ortho_offset.z);
				fclose(overfile);
				print_log("Saved to {}\n", overFile);
			}
		}
		ImGui::SeparatorText("DEV INFO");

		ImGui::Text("Overview: Zoom %.2f", zoom);

		ImGui::Text("Height: %2.f", ortho_mins.z + ortho_offset.z);

		ImGui::Text("Map Origin: (%.2f, %.2f, %.2f)",
					(ortho_mins.x + ortho_maxs.x) / 2.0f + ortho_offset.x,
					(ortho_mins.y + ortho_maxs.y) / 2.0f + ortho_offset.y,
					(ortho_mins.z + ortho_maxs.z) / 2.0f + ortho_offset.z);

		ImGui::Text("Z Min: %.2f, Z Max: %.2f", ortho_near, ortho_far);

		ImGui::Text("Rotated: %s", rotated ? "true" : "false");

		ImGui::Text("X Scale: %.2f, Y Scale: %.2f", xScale, yScale);

		/*if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			if (std::fabs(ortho_custom_w) > EPSILON && ortho_custom_w < 256.0f)
				ortho_custom_w = 256.0f;

			if (std::fabs(ortho_custom_h) > EPSILON && ortho_custom_h < 256.0f)
				ortho_custom_h = 256.0f;
		}
		*/
	}
	ImGui::End();
}

void Gui::drawTextureBrowser()
{
	Bsp* map = app->getSelectedMap();
	BspRenderer* mapRender = map ? map->getBspRender() : nullptr;

	ImGui::SetNextWindowSize(ImVec2(720.f, 640.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(320.f, 120.f), ImVec2(FLT_MAX, app->windowHeight - 40.f));

	if (!ImGui::Begin(fmt::format("{}###TEXTURE_BROWSER", get_localized_string(LANG_0651)).c_str(), &showTextureBrowser, 0))
	{
		ImGui::End();
		return;
	}

	// Controls
	static char textureFilterBuf[256] = "";
	static float thumbSizeF = 96.0f;
	static float fontSizeScale = 1.0f;

	ImGui::PushItemWidth(300.0f);
	ImGui::InputText("Filter", textureFilterBuf, sizeof(textureFilterBuf));
	ImGui::PopItemWidth();
	ImGui::SameLine();
	if (ImGui::Button("Clear"))
		textureFilterBuf[0] = '\0';
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120);
	ImGui::SliderFloat("Size", &thumbSizeF, 64.0f, 160.0f, "%.0f");
	ImGui::SameLine();
	ImGui::Checkbox("Allow external textures", &allowExternalTextures);

	fontSizeScale = thumbSizeF / 1.2f / 96.0f;
	fontSizeScale = std::clamp(fontSizeScale, 0.4f, 1.2f);

	std::string filter = toLowerCase(std::string(textureFilterBuf));
	static std::string lastCopiedTextureName;
	static int lastCopiedMiptex = -1;

	// persistent preview cache
	static std::unordered_map<std::string, Texture*> previewCache;
	static size_t lastAllTexturesCount = 0;
	{
		std::lock_guard<std::mutex> lock(Sync::TexturesList);
		if (previewCache.empty() || g_all_Textures.size() != lastAllTexturesCount || textureBrowserCacheInvalidated)
		{
			previewCache.clear();
			for (auto t : g_all_Textures)
			{
				if (!t)
					continue;
				previewCache[toLowerCase(t->texName)] = t;
			}
			lastAllTexturesCount = g_all_Textures.size();
			textureBrowserCacheInvalidated = false;
		}
	}

	// pending WAD loads (limited per frame)
	static std::vector<std::tuple<Wad*, int, std::string>> pendingWadLoads;
	const int MAX_LOADS_PER_FRAME = 2;
	auto enqueueWadLoad = [&](Wad* wad, int dirIndex, const std::string& texName)
	{
		std::string key = toLowerCase(texName);
		if (previewCache.find(key) != previewCache.end())
			return;
		for (auto& p : pendingWadLoads)
		{
			if (std::get<0>(p) == wad && std::get<1>(p) == dirIndex)
				return;
		}
		pendingWadLoads.emplace_back(wad, dirIndex, texName);
	};

	for (int l = 0; l < MAX_LOADS_PER_FRAME && !pendingWadLoads.empty(); ++l)
	{
		auto task = pendingWadLoads.front();
		pendingWadLoads.erase(pendingWadLoads.begin());
		Wad* wad = std::get<0>(task);
		int dirIndex = std::get<1>(task);
		std::string texName = std::get<2>(task);
		if (!wad)
			continue;
		if (!wad->hasTexture(dirIndex))
			continue;
		WADTEX wtex = wad->readTexture(dirIndex);
		if (wtex.nWidth <= 0 || wtex.nHeight <= 0)
			continue;
		COLOR4* rgba = ConvertWadTexToRGBA(wtex, NULL, 256);
		if (!rgba)
			continue;

		int tw = std::max(8, (int)thumbSizeF);
		int th = tw;
		std::vector<COLOR4> scaled;
		scaleImage(rgba, scaled, wtex.nWidth, wtex.nHeight, tw, th);
		delete[] rgba;

		size_t bytes = (size_t)tw * (size_t)th * 4;
		unsigned char* buf = new unsigned char[bytes];
		memcpy(buf, scaled.data(), bytes);

		Texture* t = new Texture((GLsizei)tw, (GLsizei)th, buf, texName, true, true);
		t->upload(Texture::TYPE_TEXTURE);
		t->setWadName(basename(wad->filename));
		previewCache[toLowerCase(texName)] = t;
		{
			std::lock_guard<std::mutex> lock(Sync::TexturesList);
			lastAllTexturesCount = g_all_Textures.size();
		}
	}

	// Top selection info
	auto isTextureInMap = [&](const std::string& name) -> int
	{
		if (!map)
			return -1;
		std::string low = toLowerCase(name);
		for (int i = 0; i < map->textureCount; ++i)
		{
			int texOffset = ((int*)map->textures)[i + 1];
			if (texOffset < 0)
				continue;
			BSPMIPTEX* tex = (BSPMIPTEX*)(map->textures + texOffset);
			if (!tex)
				continue;
			if (toLowerCase(tex->szName) == low)
				return i;
		}
		return -1;
	};

	ImGui::Separator();
	if (copiedMiptex >= 0)
	{
		std::string name = lastCopiedTextureName.empty() ? fmt::format("index {}", copiedMiptex) : lastCopiedTextureName;
		int idx = isTextureInMap(name);
		ImGui::Text("Selected: %s", name.c_str());
		if (idx >= 0)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "(in map index %d)", idx);
			if (map)
			{
				bool isEmb = map->is_texture_embedded(idx);
				ImGui::SameLine();
				if (isEmb)
				{
					int sz = map->getBspTextureSize(idx);
					ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "[Embedded: %.1f KB]", sz / 1024.0f);
				}
				else
				{
					ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.5f, 1.0f), "[External WAD]");
				}
			}
		}
		else
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "(Not in map)");
		}
	}
	else if (!lastCopiedTextureName.empty())
	{
		int idx = isTextureInMap(lastCopiedTextureName);
		ImGui::Text("Selected: %s", lastCopiedTextureName.c_str());
		if (idx >= 0)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "(in map index %d)", idx);
			if (map)
			{
				bool isEmb = map->is_texture_embedded(idx);
				ImGui::SameLine();
				if (isEmb)
				{
					int sz = map->getBspTextureSize(idx);
					ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "[Embedded: %.1f KB]", sz / 1024.0f);
				}
				else
				{
					ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.5f, 1.0f), "[External WAD]");
				}
			}
		}
		else
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "(Not in map)");
		}
	}
	else
	{
		ImGui::Text("Selected: None");
	}
	ImGui::Separator();

	if (ImGui::BeginTabBar("##texture_browser_tabs", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoCloseWithMiddleMouseButton | ImGuiTabBarFlags_Reorderable))
	{
		// Fixed height child window for grid (scrollable area)
		float footerHeight = 60.0f; // Space for Apply button
		float childHeight = ImGui::GetContentRegionAvail().y - footerHeight;
		if (childHeight < 100.0f)
			childHeight = 100.0f;

		// Internal map textures tab
		if (ImGui::BeginTabItem(get_localized_string(LANG_0652).c_str()))
		{
			ImGui::BeginChild("##texture_grid_internal", ImVec2(0, childHeight), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

			if (map)
			{
				std::vector<std::pair<std::string, int>> internalTextures;
				internalTextures.reserve(std::max(0, map->textureCount));
				for (int i = 0; i < map->textureCount; ++i)
				{
					int texOffset = ((int*)map->textures)[i + 1];
					if (texOffset < 0)
						continue;
					BSPMIPTEX* tex = (BSPMIPTEX*)(map->textures + texOffset);
					if (!tex)
						continue;
					std::string texName = tex->szName;
					if (!filter.empty() && toLowerCase(texName).find(filter) == std::string::npos)
						continue;
					internalTextures.emplace_back(texName, i);
				}

				float availW = ImGui::GetContentRegionAvail().x;
				const float padding = 8.0f;
				const float cellW = thumbSizeF + padding;
				int columns = std::max(1, (int)floor((availW + padding) / cellW));
				int total = (int)internalTextures.size();
				int rows = (total + columns - 1) / columns;
				float textH = ImGui::CalcTextSize("Ay").y * fontSizeScale;
				float rowHeight = thumbSizeF + textH + 8.0f;

				ImVec2 originScreen = ImGui::GetCursorScreenPos();
				ImGuiListClipper clipper;
				clipper.Begin(rows, rowHeight);
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
				while (clipper.Step())
				{
					for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
					{
						float y = originScreen.y + row * rowHeight;
						for (int col = 0; col < columns; ++col)
						{
							int idx = row * columns + col;
							if (idx >= total)
								break;

							const auto& entry = internalTextures[idx];
							const std::string texName = entry.first;
							int texIdx = entry.second;

							float x = originScreen.x + col * cellW;
							ImGui::SetCursorScreenPos(ImVec2(x, y));
							ImGui::PushID(1000000 + idx);
							ImGui::BeginGroup();

							Texture* previewTex = nullptr;
							auto it = previewCache.find(toLowerCase(texName));
							if (it != previewCache.end())
								previewTex = it->second;

							GLuint texId = missingTex ? missingTex->id : 0;
							if (previewTex && previewTex->id != 0xFFFFFFFF && previewTex->id != 0)
								texId = previewTex->id;

							ImTextureRef texRef = ImTextureRef((ImTextureID)(intptr_t)texId);
							std::string btnId = std::string("internal_texbtn_") + std::to_string(idx);
							if (ImGui::ImageButton(btnId.c_str(), texRef, ImVec2(thumbSizeF, thumbSizeF), ImVec2(0, 0), ImVec2(1, 1)))
							{
								copiedMiptex = texIdx;
								lastCopiedTextureName = texName;
							}

							bool isEmbedded = map && map->is_texture_embedded(texIdx);

							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Index: %d", texIdx);
								ImGui::Text("Name: %s", texName.c_str());
								ImGui::Text("Storage: %s", isEmbedded ? "Embedded in BSP" : "External WAD reference");
								if (isEmbedded)
								{
									int sz = map->getBspTextureSize(texIdx);
									ImGui::Text("Size: %.1f KB", sz / 1024.0f);
								}
								ImGui::EndTooltip();
							}

							bool isSelected = (!lastCopiedTextureName.empty() && toLowerCase(lastCopiedTextureName) == toLowerCase(texName)) || (copiedMiptex == texIdx);
							if (isSelected)
							{
								ImVec2 a = ImGui::GetItemRectMin();
								ImVec2 b = ImGui::GetItemRectMax();
								ImGui::GetWindowDrawList()->AddRect(a, b, IM_COL32(255, 200, 0, 255), 4.0f, 0, 3.0f);
							}

							// Storage badge on top-left of thumbnail
							ImVec2 btnMin = ImGui::GetItemRectMin();
							const char* badgeText = isEmbedded ? "BSP" : "WAD";
							ImU32 badgeBg = isEmbedded ? IM_COL32(180, 110, 10, 220) : IM_COL32(30, 120, 50, 220);
							ImVec2 badgePos = ImVec2(btnMin.x + 3.0f, btnMin.y + 3.0f);
							ImVec2 badgeSize = ImGui::CalcTextSize(badgeText);
							ImGui::GetWindowDrawList()->AddRectFilled(badgePos, ImVec2(badgePos.x + badgeSize.x + 4.0f, badgePos.y + badgeSize.y + 2.0f), badgeBg, 2.0f);
							ImGui::GetWindowDrawList()->AddText(ImVec2(badgePos.x + 2.0f, badgePos.y + 1.0f), IM_COL32(255, 255, 255, 255), badgeText);

							// Texture name with scaled font
							std::string displayName = texName;
							ImVec2 textPos = ImVec2(x, y + thumbSizeF + 4.0f);
							ImGui::SetCursorScreenPos(textPos);
							ImGui::PushClipRect(ImVec2(x, y + thumbSizeF), ImVec2(x + cellW, y + rowHeight), true);
							ImGui::PushFont(smallFont);
							ImGui::SetWindowFontScale(fontSizeScale);
							ImGui::Text("%s", displayName.c_str());
							ImGui::SetWindowFontScale(1.0f);
							ImGui::PopFont();
							ImGui::PopClipRect();

							ImGui::EndGroup();
							ImGui::PopID();
						}
						// Add Dummy to extend window boundaries after each row
						ImGui::SetCursorScreenPos(ImVec2(originScreen.x, y + rowHeight));
						ImGui::Dummy(ImVec2(availW, 0));
					}
				}
				ImGui::PopStyleVar();
				clipper.End();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		// Used textures in map tab
		if (ImGui::BeginTabItem(get_localized_string(LANG_0653).c_str()))
		{
			ImGui::BeginChild("##texture_grid_used", ImVec2(0, childHeight), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

			if (map && map->texinfos && map->texinfoCount > 0)
			{
				std::unordered_set<std::string> uniqueTexNames;
				std::vector<std::pair<std::string, int>> usedTextures;
				for (int i = 0; i < map->texinfoCount; ++i)
				{
					BSPTEXTUREINFO& texInfo = map->texinfos[i];
					if (texInfo.iMiptex >= 0 && texInfo.iMiptex < map->textureCount)
					{
						int texOffset = ((int*)map->textures)[texInfo.iMiptex + 1];
						if (texOffset >= 0)
						{
							BSPMIPTEX* tex = (BSPMIPTEX*)(map->textures + texOffset);
							if (tex)
							{
								std::string texName = tex->szName;
								if (uniqueTexNames.find(texName) == uniqueTexNames.end())
								{
									uniqueTexNames.insert(texName);
									if (!filter.empty() && toLowerCase(texName).find(filter) == std::string::npos)
										continue;
									usedTextures.emplace_back(texName, texInfo.iMiptex);
								}
							}
						}
					}
				}

				float availW = ImGui::GetContentRegionAvail().x;
				const float padding = 8.0f;
				const float cellW = thumbSizeF + padding;
				int columns = std::max(1, (int)floor((availW + padding) / cellW));
				int total = (int)usedTextures.size();
				int rows = (total + columns - 1) / columns;
				float textH = ImGui::CalcTextSize("Ay").y * fontSizeScale;
				float rowHeight = thumbSizeF + textH + 8.0f;

				ImVec2 originScreen = ImGui::GetCursorScreenPos();
				ImGuiListClipper clipper;
				clipper.Begin(rows, rowHeight);
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
				while (clipper.Step())
				{
					for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
					{
						float y = originScreen.y + row * rowHeight;
						for (int col = 0; col < columns; ++col)
						{
							int idx = row * columns + col;
							if (idx >= total)
								break;

							const auto& entry = usedTextures[idx];
							const std::string texName = entry.first;
							int texIdx = entry.second;

							float x = originScreen.x + col * cellW;
							ImGui::SetCursorScreenPos(ImVec2(x, y));
							ImGui::PushID(2000000 + idx);
							ImGui::BeginGroup();

							Texture* previewTex = nullptr;
							auto it = previewCache.find(toLowerCase(texName));
							if (it != previewCache.end())
								previewTex = it->second;

							GLuint texId = missingTex ? missingTex->id : 0;
							if (previewTex && previewTex->id != 0xFFFFFFFF && previewTex->id != 0)
								texId = previewTex->id;

							ImTextureRef texRef = ImTextureRef((ImTextureID)(intptr_t)texId);
							std::string btnId = std::string("used_texbtn_") + std::to_string(idx);
							if (ImGui::ImageButton(btnId.c_str(), texRef, ImVec2(thumbSizeF, thumbSizeF), ImVec2(0, 0), ImVec2(1, 1)))
							{
								copiedMiptex = texIdx;
								lastCopiedTextureName = texName;
							}

							bool isEmbedded = map && map->is_texture_embedded(texIdx);

							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("Index: %d", texIdx);
								ImGui::Text("Name: %s", texName.c_str());
								ImGui::Text("Storage: %s", isEmbedded ? "Embedded in BSP" : "External WAD reference");
								if (isEmbedded)
								{
									int sz = map->getBspTextureSize(texIdx);
									ImGui::Text("Size: %.1f KB", sz / 1024.0f);
								}
								ImGui::EndTooltip();
							}

							bool isSelected = (!lastCopiedTextureName.empty() && toLowerCase(lastCopiedTextureName) == toLowerCase(texName)) || (copiedMiptex == texIdx);
							if (isSelected)
							{
								ImVec2 a = ImGui::GetItemRectMin();
								ImVec2 b = ImGui::GetItemRectMax();
								ImGui::GetWindowDrawList()->AddRect(a, b, IM_COL32(255, 200, 0, 255), 4.0f, 0, 3.0f);
							}

							// Storage badge on top-left of thumbnail
							ImVec2 btnMin = ImGui::GetItemRectMin();
							const char* badgeText = isEmbedded ? "BSP" : "WAD";
							ImU32 badgeBg = isEmbedded ? IM_COL32(180, 110, 10, 220) : IM_COL32(30, 120, 50, 220);
							ImVec2 badgePos = ImVec2(btnMin.x + 3.0f, btnMin.y + 3.0f);
							ImVec2 badgeSize = ImGui::CalcTextSize(badgeText);
							ImGui::GetWindowDrawList()->AddRectFilled(badgePos, ImVec2(badgePos.x + badgeSize.x + 4.0f, badgePos.y + badgeSize.y + 2.0f), badgeBg, 2.0f);
							ImGui::GetWindowDrawList()->AddText(ImVec2(badgePos.x + 2.0f, badgePos.y + 1.0f), IM_COL32(255, 255, 255, 255), badgeText);

							// Texture name with scaled font
							std::string displayName = texName;
							ImVec2 textPos = ImVec2(x, y + thumbSizeF + 4.0f);
							ImGui::SetCursorScreenPos(textPos);
							ImGui::PushClipRect(ImVec2(x, y + thumbSizeF), ImVec2(x + cellW, y + rowHeight), true);
							ImGui::PushFont(smallFont);
							ImGui::SetWindowFontScale(fontSizeScale);
							ImGui::Text("%s", displayName.c_str());
							ImGui::SetWindowFontScale(1.0f);
							ImGui::PopFont();
							ImGui::PopClipRect();

							ImGui::EndGroup();
							ImGui::PopID();
						}
						// Add Dummy to extend window boundaries after each row
						ImGui::SetCursorScreenPos(ImVec2(originScreen.x, y + rowHeight));
						ImGui::Dummy(ImVec2(availW, 0));
					}
				}
				ImGui::PopStyleVar();
				clipper.End();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		// Per-WAD tabs
		if (mapRender)
		{
			for (size_t wadIdx = 0; wadIdx < mapRender->wads.size(); ++wadIdx)
			{
				Wad* wad = mapRender->wads[wadIdx];
				if (!wad)
					continue;
				if (wad->dirEntries.empty())
					wad->readInfo();

				std::string tabName = basename(wad->filename);
				if (!ImGui::BeginTabItem(tabName.c_str()))
					continue;

				ImGui::BeginChild("##texture_grid_wad", ImVec2(0, childHeight), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

				float availW = ImGui::GetContentRegionAvail().x;
				const float padding = 8.0f;
				const float cellW = thumbSizeF + padding;
				int columns = std::max(1, (int)floor((availW + padding) / cellW));
				std::vector<std::string> names;
				std::vector<int> indices;
				for (int texIdx = 0; texIdx < (int)wad->dirEntries.size(); ++texIdx)
				{
					if (wad->dirEntries[texIdx].nType == 0x43)
					{
						std::string texName = wad->dirEntries[texIdx].szName;
						if (!filter.empty() && toLowerCase(texName).find(filter) == std::string::npos)
							continue;
						names.push_back(texName);
						indices.push_back(texIdx);
					}
				}

				int total = (int)names.size();
				int rows = (total + columns - 1) / columns;
				float textH = ImGui::CalcTextSize("Ay").y * fontSizeScale;
				float rowHeight = thumbSizeF + textH + 8.0f;

				ImVec2 originScreen = ImGui::GetCursorScreenPos();
				ImGuiListClipper clipper;
				clipper.Begin(rows, rowHeight);
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
				while (clipper.Step())
				{
					for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
					{
						float y = originScreen.y + row * rowHeight;
						for (int col = 0; col < columns; ++col)
						{
							int ii = row * columns + col;
							if (ii >= total)
								break;

							const std::string texName = names[ii];
							int texIdx = indices[ii];

							float x = originScreen.x + col * cellW;
							ImGui::SetCursorScreenPos(ImVec2(x, y));
							ImGui::PushID((int)(3000000 + wadIdx * 100000 + ii));
							ImGui::BeginGroup();

							Texture* previewTex = nullptr;
							auto it = previewCache.find(toLowerCase(texName));
							if (it != previewCache.end())
								previewTex = it->second;

							if (!previewTex && wad->hasTexture(texIdx))
							{
								enqueueWadLoad(wad, texIdx, texName);
							}

							GLuint texId = missingTex ? missingTex->id : 0;
							if (previewTex && previewTex->id != 0xFFFFFFFF && previewTex->id != 0)
								texId = previewTex->id;

							ImTextureRef texRef = ImTextureRef((ImTextureID)(intptr_t)texId);
							std::string btnId = std::string("wad_single_texbtn_") + std::to_string(wadIdx) + "_" + std::to_string(ii);
							if (ImGui::ImageButton(btnId.c_str(), texRef, ImVec2(thumbSizeF, thumbSizeF), ImVec2(0, 0), ImVec2(1, 1)))
							{
								if (previewTex)
									lastCopiedTextureName = previewTex->texName;
								else
									lastCopiedTextureName = texName;
								copiedMiptex = -1;
							}

							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::Text("File: %s", basename(wad->filename).c_str());
								ImGui::Text("Name: %s", texName.c_str());
								ImGui::Text("Entry: %d", texIdx);
								ImGui::EndTooltip();
							}

							bool isSelected = (!lastCopiedTextureName.empty() && toLowerCase(lastCopiedTextureName) == toLowerCase(texName));
							if (isSelected)
							{
								ImVec2 a = ImGui::GetItemRectMin();
								ImVec2 b = ImGui::GetItemRectMax();
								ImGui::GetWindowDrawList()->AddRect(a, b, IM_COL32(255, 200, 0, 255), 4.0f, 0, 3.0f);
							}

							// Texture name with entry number and scaled font
							std::string displayName = fmt::format("{} ({})", texName, texIdx);
							ImVec2 textPos = ImVec2(x, y + thumbSizeF + 4.0f);
							ImGui::SetCursorScreenPos(textPos);
							ImGui::PushClipRect(ImVec2(x, y + thumbSizeF), ImVec2(x + cellW, y + rowHeight), true);
							ImGui::PushFont(smallFont);
							ImGui::SetWindowFontScale(fontSizeScale);
							ImGui::Text("%s", displayName.c_str());
							ImGui::SetWindowFontScale(1.0f);
							ImGui::PopFont();
							ImGui::PopClipRect();

							ImGui::EndGroup();
							ImGui::PopID();
						}
						// Add Dummy to extend window boundaries after each row
						ImGui::SetCursorScreenPos(ImVec2(originScreen.x, y + rowHeight));
						ImGui::Dummy(ImVec2(availW, 0));
					}
				}
				ImGui::PopStyleVar();
				clipper.End();

				ImGui::EndChild();
				ImGui::EndTabItem();
			}
		}

		ImGui::EndTabBar();
	}

	// Footer with Apply button and selection summary (fixed at bottom)
	ImGui::Separator();
	ImGui::BeginGroup();
	ImGui::Text("Selected: %s", lastCopiedTextureName.empty() ? "None" : lastCopiedTextureName.c_str());
	ImGui::SameLine();
	if (copiedMiptex >= 0)
		ImGui::Text("(BSP index: %d)", copiedMiptex);
	ImGui::EndGroup();

	bool showUnembedButton = (copiedMiptex >= 0 && map);
	float footerBtnWidth = showUnembedButton ? (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f : -1.0f;

	if (showUnembedButton)
	{
		bool isEmbedded = map->is_texture_embedded(copiedMiptex);
		if (isEmbedded)
		{
			if (ImGui::Button("Unembed Texture", ImVec2(footerBtnWidth, 30)))
			{
				if (mapRender)
				{
					mapRender->pushUndoState("Unembed Texture", FL_TEXTURES);
					int removed = map->unembed_textures({copiedMiptex});
					if (removed > 0)
					{
						print_log("Unembedded texture '%s' (BSP index %d)\n", lastCopiedTextureName.c_str(), copiedMiptex);
						invalidateTextureBrowserCache();
						mapRender->reloadTextures();
						mapRender->reload();
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, "Failed to unembed texture '%s'\n", lastCopiedTextureName.c_str());
					}
				}
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Strip embedded miptex pixel data from BSP, converting this texture into an external WAD reference.");
			}
		}
		else
		{
			ImGui::BeginDisabled(true);
			ImGui::Button("Already External (WAD)", ImVec2(footerBtnWidth, 30));
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("This texture is already referencing an external WAD and has no embedded pixel data in the BSP.");
			}
		}
		ImGui::SameLine();
	}

	if (ImGui::Button("Apply Selected Texture", ImVec2(footerBtnWidth, 30)))
	{
		if (copiedMiptex >= 0 && map)
		{
			pasteTexture();
			print_log("Applied texture: %s (index: %d)\n", lastCopiedTextureName.c_str(), copiedMiptex);
		}
		else if (!lastCopiedTextureName.empty() && map)
		{
			if (allowExternalTextures)
			{
				int idx = isTextureInMap(lastCopiedTextureName);
				if (idx < 0)
				{
					for (auto& s : mapRender->wads)
					{
						if (s->hasTexture(lastCopiedTextureName))
						{
							WADTEX wadTex = s->readTexture(lastCopiedTextureName);
							idx = map->add_texture(lastCopiedTextureName.c_str(), NULL, wadTex.nWidth, wadTex.nHeight);
							mapRender->reuploadTextures();
							mapRender->preRenderFaces();
							break;
						}
					}
				}
				if (idx >= 0)
				{
					copiedMiptex = idx;
					pasteTexture();
					print_log("Applied external texture: %s (index: %d)\n", lastCopiedTextureName.c_str(), copiedMiptex);
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, "Failed to find/add external texture reference: %s\n", lastCopiedTextureName.c_str());
				}
			}
			else
			{
				print_log("Texture selected (WAD): %s. Enable 'Allow external textures' to apply.\n", lastCopiedTextureName.c_str());
			}
		}
		else
		{
			print_log(PRINT_RED | PRINT_INTENSITY, "No texture selected");
		}
	}

	ImGui::End();
}

void Gui::drawKeyvalueEditor()
{
	ImGui::SetNextWindowSize(ImVec2(610.f, 610.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(300.f, 100.f), ImVec2(FLT_MAX, app->windowHeight - 40.f));
	// ImGui::SetNextWindowContentSize(ImVec2(550, 0.0f));
	if (ImGui::Begin(fmt::format("{}###KEYVALUE_WIDGET", get_localized_string(LANG_1103)).c_str(), &showKeyvalueWidget, 0))
	{
		auto entIdx = app->pickInfo.selectedEnts;

		Bsp* map = app->getSelectedMap();
		if (entIdx.size() && app->fgd && !app->isLoading && !app->isModelsReloading && !app->reloading && map)
		{

			// ImGui::TextUnformatted(fmt::format("ENTS {}. FIRST ENT {}.", g_app->pickInfo.selectedEnts.size(), g_app->pickInfo.selectedEnts.size() ? g_app->pickInfo.selectedEnts[0] : -1).c_str());

			Entity* ent = map->ents[entIdx[0]];
			std::string cname = ent->keyvalues["classname"];
			FgdClass* fgdClass = app->fgd->getFgdClass(cname, FGD_CLASS_POINT);
			std::vector<FgdGroup> targetGroup = app->fgd->pointEntGroups;

			if (!fgdClass || (ent->hasKey("model") &&
							  (starts_with(ent->keyvalues["model"], '*') || ends_with(toLowerCase(ent->keyvalues["model"]), ".bsp"))))
			{
				FgdClass* tmpfgdClass = app->fgd->getFgdClass(cname, FGD_CLASS_SOLID);
				if (tmpfgdClass)
				{
					targetGroup = app->fgd->solidEntGroups;
					fgdClass = tmpfgdClass;
				}
			}

			ImGui::PushFont(largeFont);
			ImGui::AlignTextToFramePadding();
			ImGui::Text(get_localized_string(LANG_0654).c_str());
			ImGui::SameLine();
			if (cname != "worldspawn")
			{
				if (!targetGroup.size())
				{
					ImGui::BeginDisabled();
				}

				if (ImGui::Button((" " + cname + " ").c_str()))
					ImGui::OpenPopup("classname_popup");

				if (!targetGroup.size())
				{
					ImGui::EndDisabled();
				}
			}
			else
			{
				ImGui::Text(cname.c_str());
			}

			ImGui::PopFont();

			if (fgdClass)
			{
				ImGui::SameLine();
				ImGui::Text("(?)");
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
					ImGui::TextUnformatted((fgdClass->description).c_str());
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
			}

			if (ImGui::BeginPopup("classname_popup"))
			{
				ImGui::Text(get_localized_string(LANG_0656).c_str());
				ImGui::Separator();

				for (FgdGroup& group : targetGroup)
				{
					if (!group.classes.size())
					{
						ImGui::BeginDisabled();
					}

					if (ImGui::BeginMenu(group.groupName.c_str()))
					{
						for (size_t k = 0; k < group.classes.size(); k++)
						{
							if (ImGui::MenuItem(group.classes[k]->name.c_str()))
							{
								for (auto selected_entId : entIdx)
								{
									map->ents[selected_entId]->setOrAddKeyvalue("classname", group.classes[k]->name);
									map->getBspRender()->refreshEnt((int)selected_entId);
								}
							}
						}

						map->getBspRender()->pushEntityUndoStateDelay("Change Class");
						ImGui::EndMenu();
					}

					if (!group.classes.size())
					{
						ImGui::EndDisabled();
					}
				}

				ImGui::EndPopup();
			}

			ImGui::Dummy(ImVec2(0, 10));

			if (ImGui::BeginTabBar(get_localized_string(LANG_0657).c_str()))
			{
				if (ImGui::BeginTabItem(get_localized_string(LANG_0658).c_str()))
				{
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_SmartEditTab((int)entIdx[0]);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem(get_localized_string(LANG_0659).c_str()))
				{
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_FlagsTab((int)entIdx[0]);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem(get_localized_string(LANG_0660).c_str()))
				{
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_RawEditTab((int)entIdx[0]);
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}
		else
		{
			if (entIdx.empty())
				ImGui::Text(get_localized_string(LANG_0661).c_str());
			else
				ImGui::Text(get_localized_string(LANG_0662).c_str());
		}
	}
	ImGui::End();
}

void Gui::drawKeyvalueEditor_SmartEditTab(int entIdx)
{
	Bsp* map = app->getSelectedMap();
	if (!map || entIdx < 0)
	{
		ImGui::Text(get_localized_string(LANG_1105).c_str());
		return;
	}
	Entity* sel_ent = map->ents[entIdx];
	std::string cname = sel_ent->keyvalues["classname"];
	FgdClass* fgdClass = app->fgd->getFgdClass(cname);
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::BeginChild(get_localized_string(LANG_0663).c_str());

	ImGui::Columns(2, get_localized_string(LANG_0664).c_str(), false); // 4-ways, with border

	float paddingx = style.WindowPadding.x + style.FramePadding.x;
	float inputWidth = (ImGui::GetWindowWidth() - (paddingx * 2)) * 0.5f;

	// needed if autoresize is true
	if (ImGui::GetScrollMaxY() > 0)
		inputWidth -= style.ScrollbarSize * 0.5f;

	struct InputDataKey
	{
		std::string key;
		std::string defaultValue;

		InputDataKey()
		{
			key.clear();
			defaultValue.clear();
		}
	};

	if (fgdClass)
	{
		static InputDataKey inputData[128];
		static int lastPickCount = 0;

		if (sel_ent->hasKey("model"))
		{
			bool foundmodel = false;
			for (size_t i = 0; i < fgdClass->keyvalues.size(); i++)
			{
				KeyvalueDef& keyvalue = fgdClass->keyvalues[i];
				std::string key = keyvalue.name;
				if (key == "model")
				{
					foundmodel = true;
				}
			}
			if (!foundmodel)
			{
				KeyvalueDef keyvalue = KeyvalueDef();
				keyvalue.name = "model";
				keyvalue.defaultValue =
					keyvalue.shortDescription = "Model";
				keyvalue.iType = FGD_KEY_STRING;
				fgdClass->keyvalues.push_back(keyvalue);
			}
		}

		for (size_t i = 0; i < fgdClass->keyvalues.size() && i < 128; i++)
		{
			KeyvalueDef& keyvalue = fgdClass->keyvalues[i];
			std::string key = keyvalue.name;
			if (key == "spawnflags")
			{
				continue;
			}
			std::string value = sel_ent->keyvalues[key];
			std::string niceName = keyvalue.shortDescription;

			if (!nullstrlen(value) && nullstrlen(keyvalue.defaultValue))
			{
				value = keyvalue.defaultValue;
			}

			if (niceName.size() >= g_limits.maxKeyLen)
				niceName = niceName.substr(0, g_limits.maxKeyLen - 1);
			if (value.size() >= g_limits.maxValLen)
				value = value.substr(0, g_limits.maxValLen - 1);

			inputData[i].key = key;
			inputData[i].defaultValue = keyvalue.defaultValue;

			ImGui::SetNextItemWidth(inputWidth);
			ImGui::AlignTextToFramePadding();
			ImGui::Text(niceName.c_str());
			ImGui::NextColumn();
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::PushStyleColor(ImGuiCol_Text, {0.9f, 0.4f, 0.2f, 1.0f});
				ImGui::TextUnformatted(keyvalue.shortDescription.c_str());
				ImGui::PopStyleColor();
				if (keyvalue.fullDescription.size())
					ImGui::TextUnformatted(keyvalue.fullDescription.c_str());
				ImGui::PushStyleColor(ImGuiCol_Text, {0.9f, 0.4f, 0.2f, 1.0f});
				ImGui::TextUnformatted(keyvalue.name.c_str());
				ImGui::SameLine();
				ImGui::TextUnformatted(" = ");
				ImGui::SameLine();
				ImGui::TextUnformatted(keyvalue.defaultValue.c_str());
				ImGui::PopStyleColor();
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			ImGui::SetNextItemWidth(inputWidth);

			if (keyvalue.iType == FGD_KEY_CHOICES && !keyvalue.choices.empty())
			{
				std::string selectedValue = keyvalue.choices[0].name;
				int ikey = str_to_int(value);

				for (size_t k = 0; k < keyvalue.choices.size(); k++)
				{
					KeyvalueChoice& choice = keyvalue.choices[k];

					if ((choice.isInteger && ikey == choice.ivalue) ||
						(!choice.isInteger && value == choice.svalue))
					{
						selectedValue = choice.name;
					}
				}

				if (ImGui::BeginCombo(("##comboval" + std::to_string(i)).c_str(), selectedValue.c_str()))
				{
					for (size_t k = 0; k < keyvalue.choices.size(); k++)
					{
						KeyvalueChoice& choice = keyvalue.choices[k];
						bool selected = choice.svalue == value || (value.empty() && choice.svalue == keyvalue.defaultValue);
						bool needrefreshmodel = false;
						if (ImGui::Selectable(choice.name.c_str(), selected))
						{
							if (key == "renderamt")
							{
								if (sel_ent->hasKey("renderamt") && sel_ent->keyvalues["renderamt"] != choice.svalue)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "rendermode")
							{
								if (sel_ent->hasKey("rendermode") && sel_ent->keyvalues["rendermode"] != choice.svalue)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "renderfx")
							{
								if (sel_ent->hasKey("renderfx") && sel_ent->keyvalues["renderfx"] != choice.svalue)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "rendercolor")
							{
								if (sel_ent->hasKey("rendercolor") && sel_ent->keyvalues["rendercolor"] != choice.svalue)
								{
									needrefreshmodel = true;
								}
							}
							BspRenderer* render = map->getBspRender();
							if (render)
							{
								if (g_app->pickInfo.selectedEnts.size() && g_app->pickInfo.selectedEnts[0] >= 0)
								{
									for (auto selected_entId : g_app->pickInfo.selectedEnts)
									{
										Entity* selected_ent = map->ents[selected_entId];
										selected_ent->setOrAddKeyvalue(key, choice.svalue);
										map->getBspRender()->refreshEnt((int)selected_entId);

										if (needrefreshmodel)
										{
											if (selected_ent->getBspModelIdx() > 0)
											{
												map->getBspRender()->refreshModel(selected_ent->getBspModelIdx());
												map->getBspRender()->refreshEnt(selected_entId);
											}
										}
									}

									map->getBspRender()->pushEntityUndoStateDelay("Edit Keyvalue");
								}
							}
							pickCount++;
							vertPickCount++;

							g_app->updateEntConnections();
						}

						if (choice.fullDescription.size())
						{
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
								ImGui::PushStyleColor(ImGuiCol_Text, {0.9f, 0.4f, 0.2f, 1.0f});
								ImGui::TextUnformatted(choice.fullDescription.c_str());
								ImGui::PopStyleColor();
								if (choice.sdefvalue.size())
								{
									ImGui::TextUnformatted(choice.name.c_str());
									ImGui::SameLine();
									ImGui::TextUnformatted(" = ");
									ImGui::SameLine();
									ImGui::TextUnformatted(choice.sdefvalue.c_str());
								}
								ImGui::PopTextWrapPos();
								ImGui::EndTooltip();
							}
						}
					}

					ImGui::EndCombo();
				}
			}
			else
			{
				struct InputChangeCallback
				{
					static int keyValueChanged(ImGuiInputTextCallbackData* data)
					{
						if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
						{
							if (data->EventChar < 256)
							{
								if (strchr("-0123456789", (char)data->EventChar))
									return 0;
							}
							return 1;
						}
						InputDataKey* linputData = (InputDataKey*)data->UserData;

						if (!data->Buf || !nullstrlen(linputData->key))
							return 0;

						bool needReloadModel = false;
						Bsp* map2 = g_app->getSelectedMap();
						if (map2)
						{
							BspRenderer* render = map2->getBspRender();
							if (render)
							{
								if (g_app->pickInfo.selectedEnts.size() && g_app->pickInfo.selectedEnts[0] >= 0)
								{
									for (auto selected_entId : g_app->pickInfo.selectedEnts)
									{
										bool needRefreshModel = false;
										Entity* ent = map2->ents[selected_entId];
										std::string newVal = data->Buf;

										if (!g_app->reloading && !g_app->isModelsReloading && linputData->key == "model")
										{
											if (ent->hasKey("model") && ent->keyvalues["model"] != newVal)
											{
												needReloadModel = true;
											}
										}

										if (linputData->key == "renderamt")
										{
											if (ent->hasKey("renderamt") && ent->keyvalues["renderamt"] != newVal)
											{
												needRefreshModel = true;
											}
										}
										if (linputData->key == "rendermode")
										{
											if (ent->hasKey("rendermode") && ent->keyvalues["rendermode"] != newVal)
											{
												needRefreshModel = true;
											}
										}
										if (linputData->key == "renderfx")
										{
											if (ent->hasKey("renderfx") && ent->keyvalues["renderfx"] != newVal)
											{
												needRefreshModel = true;
											}
										}
										if (linputData->key == "rendercolor")
										{
											if (ent->hasKey("rendercolor") && ent->keyvalues["rendercolor"] != newVal)
											{
												needRefreshModel = true;
											}
										}

										if (!nullstrlen(newVal))
										{
											ent->setOrAddKeyvalue(linputData->key, linputData->defaultValue);
										}
										else
										{
											ent->setOrAddKeyvalue(linputData->key, newVal);
										}

										render->refreshEnt((int)selected_entId);

										pickCount++;
										vertPickCount++;

										if (needRefreshModel)
										{
											if (ent->getBspModelIdx() > 0)
											{
												map2->getBspRender()->refreshModel(ent->getBspModelIdx());
											}
										}
									}
									map2->getBspRender()->pushEntityUndoStateDelay("Edit Keyvalue");
								}
							}
						}
						pickCount++;
						vertPickCount++;
						if (needReloadModel)
							g_app->reloadBspModels();

						g_app->updateEntConnections();
						return 1;
					}
				};

				if (sel_ent->keyvalues.count(key))
				{
					std::string* keyval = &sel_ent->keyvalues[key];

					if (keyvalue.iType == FGD_KEY_INTEGER)
					{
						ImGui::InputText(("##inval" + std::to_string(i)).c_str(), keyval,
										 ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackEdit,
										 InputChangeCallback::keyValueChanged, &inputData[i]);
					}
					else
					{
						ImGui::InputText(("##inval" + std::to_string(i)).c_str(), keyval,
										 ImGuiInputTextFlags_CallbackEdit, InputChangeCallback::keyValueChanged, &inputData[i]);
					}
				}
			}

			ImGui::NextColumn();
		}

		lastPickCount = pickCount;
	}

	ImGui::Columns(1);

	ImGui::EndChild();
}

void Gui::drawKeyvalueEditor_FlagsTab(int entIdx)
{
	Bsp* map = app->getSelectedMap();
	if (!map || entIdx < 0)
	{
		ImGui::Text(get_localized_string(LANG_1163).c_str());
		return;
	}

	Entity* ent = map->ents[entIdx];

	ImGui::BeginChild(get_localized_string(LANG_0665).c_str());

	unsigned int spawnflags = strtoul(ent->keyvalues["spawnflags"].c_str(), NULL, 10);
	FgdClass* fgdClass = app->fgd->getFgdClass(ent->keyvalues["classname"]);

	ImGui::Columns(2, get_localized_string(LANG_0666).c_str(), true);

	static bool checkboxEnabled[32];

	for (int i = 0; i < 32; i++)
	{
		if (i == 16)
		{
			ImGui::NextColumn();
		}
		std::string name;
		std::string description;
		if (fgdClass)
		{
			name = fgdClass->spawnFlagNames[i];
			description = fgdClass->spawnFlagDescriptions[i];
		}

		checkboxEnabled[i] = spawnflags & (1 << i);

		if (ImGui::Checkbox((name + "##flag" + std::to_string(i)).c_str(), &checkboxEnabled[i]))
		{
			for (auto selected_entId : g_app->pickInfo.selectedEnts)
			{
				Entity* selected_ent = map->ents[selected_entId];
				unsigned int entSpawnflags = strtoul(selected_ent->keyvalues["spawnflags"].c_str(), NULL, 10);
				if (!checkboxEnabled[i])
				{
					entSpawnflags &= ~(1U << i);
				}
				else
				{
					entSpawnflags |= (1U << i);
				}

				if (entSpawnflags != 0)
					selected_ent->setOrAddKeyvalue("spawnflags", std::to_string(entSpawnflags));
				else
					selected_ent->removeKeyvalue("spawnflags");

				map->getBspRender()->refreshEnt((int)selected_entId);
			}

			map->getBspRender()->pushEntityUndoStateDelay(checkboxEnabled[i] ? "Enable Flag" : "Disable Flag");
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, {0.9f, 0.4f, 0.2f, 1.0f});
			std::string title = (name.empty() ? get_localized_string(LANG_0659) : name) + " (" + std::to_string(1U << i) + ")";
			ImGui::TextUnformatted(title.c_str());
			ImGui::PopStyleColor();
			if (description.size())
				ImGui::TextUnformatted(description.c_str());
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	ImGui::Columns(1);

	ImGui::EndChild();
}

struct InputData
{
	int idx;
};

struct TextChangeCallback
{
	static int keyNameChanged(ImGuiInputTextCallbackData* data)
	{
		InputData* inputData = (InputData*)data->UserData;

		Bsp* map = g_app->getSelectedMap();
		if (map)
		{
			BspRenderer* render = map->getBspRender();
			if (render)
			{
				if (g_app->pickInfo.selectedEnts.size() && g_app->pickInfo.selectedEnts[0] >= 0)
				{
					std::string key = map->ents[g_app->pickInfo.selectedEnts[0]]->keyOrder[inputData->idx];
					if (key != data->Buf)
					{
						bool reloadModels = false;
						for (auto entId : g_app->pickInfo.selectedEnts)
						{
							Entity* selent = map->ents[entId];
							if (selent->renameKey(key, data->Buf))
							{
								render->refreshEnt((int)entId);
								if (key == "model" || std::string(data->Buf) == "model")
								{
									reloadModels = true;
								}
							}
						}
						if (reloadModels)
						{
							g_app->reloadBspModels();
						}
						g_app->updateEntConnections();
						map->getBspRender()->pushEntityUndoStateDelay("Rename Keyvalue");
					}
				}
			}
		}
		return 1;
	}

	static int keyValueChanged(ImGuiInputTextCallbackData* data)
	{
		InputData* inputData = (InputData*)data->UserData;

		Bsp* map2 = g_app->getSelectedMap();
		if (map2)
		{
			BspRenderer* render = map2->getBspRender();
			if (render)
			{
				if (g_app->pickInfo.selectedEnts.size() && g_app->pickInfo.selectedEnts[0] >= 0)
				{
					bool needreloadmodels = false;
					std::string key = map2->ents[g_app->pickInfo.selectedEnts[0]]->keyOrder[inputData->idx];
					int part_vec = -1;

					for (auto entId : g_app->pickInfo.selectedEnts)
					{
						Entity* selent = map2->ents[entId];
						if (selent->keyvalues[key] != data->Buf)
						{
							if (part_vec == -1 && g_app->pickInfo.selectedEnts.size() > 1)
							{
								if (key == "origin")
								{
									vec3 newOrigin = parseVector(data->Buf);
									vec3 oldOrigin = parseVector(selent->keyvalues[key]);
									vec3 testOrigin = newOrigin - oldOrigin;
									if (std::fabs(testOrigin.x) > EPSILON2)
									{
										part_vec = 0;
									}
									else if (std::fabs(testOrigin.y) > EPSILON2)
									{
										part_vec = 1;
									}
									else
									{
										part_vec = 2;
									}
								}
							}

							bool needrefreshmodel = false;
							if (key == "model")
							{
								if (selent->hasKey("model") && selent->keyvalues["model"] != data->Buf)
								{
									selent->setOrAddKeyvalue(key, data->Buf);
									render->refreshEnt((int)entId);
									needreloadmodels = true;
								}
							}
							if (key == "renderamt")
							{
								if (selent->hasKey("renderamt") && selent->keyvalues["renderamt"] != data->Buf)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "rendermode")
							{
								if (selent->hasKey("rendermode") && selent->keyvalues["rendermode"] != data->Buf)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "renderfx")
							{
								if (selent->hasKey("renderfx") && selent->keyvalues["renderfx"] != data->Buf)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "rendercolor")
							{
								if (selent->hasKey("rendercolor") && selent->keyvalues["rendercolor"] != data->Buf)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "origin" && part_vec != -1)
							{
								vec3 newOrigin = parseVector(data->Buf);
								vec3 oldOrigin = parseVector(selent->keyvalues[key]);
								oldOrigin[part_vec] = newOrigin[part_vec];
								selent->setOrAddKeyvalue("origin", oldOrigin.toKeyvalueString());
							}
							else
							{
								selent->setOrAddKeyvalue(key, data->Buf);
							}
							render->refreshEnt((int)entId);
							if (needrefreshmodel)
							{
								if (selent->getBspModelIdx() > 0)
								{
									map2->getBspRender()->refreshModel(selent->getBspModelIdx());
								}
							}
						}
					}

					if (needreloadmodels)
					{
						g_app->reloadBspModels();
					}

					pickCount++;
					vertPickCount++;
					g_app->updateEntConnections();
					map2->getBspRender()->pushEntityUndoStateDelay("Edit Keyvalue RAW");
				}
			}
		}

		return 1;
	}
};
void Gui::drawKeyvalueEditor_RawEditTab(int entIdx)
{
	Bsp* map = app->getSelectedMap();
	if (!map || entIdx < 0)
	{
		ImGui::Text(get_localized_string(LANG_1176).c_str());
		return;
	}

	Entity* ent = map->ents[entIdx];
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::Columns(4, get_localized_string(LANG_1106).c_str(), false);

	float butColWidth = smallFont->CalcTextSizeA(GImGui->FontSize, 100, 100, " X ").x + style.FramePadding.x * 4;
	float textColWidth = (ImGui::GetWindowWidth() - (butColWidth + style.FramePadding.x * 2) * 2) * 0.5f;

	ImGui::SetColumnWidth(0, butColWidth);
	ImGui::SetColumnWidth(1, textColWidth);
	ImGui::SetColumnWidth(2, textColWidth);
	ImGui::SetColumnWidth(3, butColWidth);

	ImGui::NextColumn();
	ImGui::Text(get_localized_string(LANG_0667).c_str());
	ImGui::NextColumn();
	ImGui::Text(get_localized_string(LANG_0668).c_str());
	ImGui::NextColumn();
	ImGui::NextColumn();

	ImGui::Columns(1);
	ImGui::BeginChild(get_localized_string(LANG_0669).c_str());

	ImGui::Columns(4, get_localized_string(LANG_0670).c_str(), false);

	textColWidth -= style.ScrollbarSize; // double space to prevent accidental deletes

	ImGui::SetColumnWidth(0, butColWidth);
	ImGui::SetColumnWidth(1, textColWidth);
	ImGui::SetColumnWidth(2, textColWidth);
	ImGui::SetColumnWidth(3, butColWidth);

	float paddingx = style.WindowPadding.x + style.FramePadding.x;
	float inputWidth = (ImGui::GetWindowWidth() - paddingx * 2) * 0.5f;

	static InputData keyIds[MAX_KEYS_PER_ENT];
	static InputData valueIds[MAX_KEYS_PER_ENT];
	static int lastPickCount = -1;
	static std::string dragNames[MAX_KEYS_PER_ENT];
	static const char* dragIds[MAX_KEYS_PER_ENT];

	if (dragNames[0].empty())
	{
		for (int i = 0; i < MAX_KEYS_PER_ENT; i++)
		{
			std::string name = "::##drag" + std::to_string(i);
			dragNames[i] = std::move(name);
		}
	}

	if (lastPickCount != pickCount)
	{
		for (int i = 0; i < MAX_KEYS_PER_ENT; i++)
		{
			dragIds[i] = dragNames[i].c_str();
		}
	}

	ImVec4 dragColor = style.Colors[ImGuiCol_FrameBg];
	dragColor.x *= 2;
	dragColor.y *= 2;
	dragColor.z *= 2;

	ImVec4 dragButColor = style.Colors[ImGuiCol_Header];

	static bool hoveredDrag[MAX_KEYS_PER_ENT];
	static bool wasKeyDragging = false;
	bool keyDragging = false;

	float startY = 0;
	for (size_t i = 0; i < ent->keyOrder.size() && i < MAX_KEYS_PER_ENT; i++)
	{

		const char* item = dragIds[i];

		{
			style.SelectableTextAlign.x = 0.5f;
			ImGui::AlignTextToFramePadding();
			ImGui::PushStyleColor(ImGuiCol_Header, hoveredDrag[i] ? dragColor : dragButColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, dragColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, dragColor);
			ImGui::Selectable(item, true);
			ImGui::PopStyleColor(3);
			style.SelectableTextAlign.x = 0.0f;

			hoveredDrag[i] = ImGui::IsItemActive();
			if (hoveredDrag[i])
			{
				keyDragging = true;
			}

			if (i == 0)
			{
				startY = ImGui::GetItemRectMin().y;
			}

			if (ImGui::IsItemActive() && !ImGui::IsItemHovered())
			{
				int n_next = (int)((ImGui::GetMousePos().y - startY) / (ImGui::GetItemRectSize().y + style.FramePadding.y * 2));
				if (n_next >= 0 && (size_t)n_next < ent->keyOrder.size() && n_next < MAX_KEYS_PER_ENT)
				{
					dragIds[i] = dragIds[n_next];
					dragIds[n_next] = item;

					std::string temp = ent->keyOrder[i];
					ent->keyOrder[i] = ent->keyOrder[n_next];
					ent->keyOrder[n_next] = std::move(temp);

					ImGui::ResetMouseDragDelta();
				}
			}

			ImGui::NextColumn();
		}

		{
			bool invalidKey = lastPickCount == pickCount;

			keyIds[i].idx = (int)i;

			if (invalidKey)
			{
				ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
			}
			else if (hoveredDrag[i])
			{
				ImGui::PushStyleColor(ImGuiCol_FrameBg, dragColor);
			}

			ImGui::SetNextItemWidth(inputWidth);
			ImGui::InputText(("##key" + std::to_string(i)).c_str(), &ent->keyOrder[i], ImGuiInputTextFlags_CallbackEdit,
							 TextChangeCallback::keyNameChanged, &keyIds[i]);

			if (invalidKey || hoveredDrag[i])
			{
				ImGui::PopStyleColor();
			}

			ImGui::NextColumn();
		}
		{
			valueIds[i].idx = (int)i;

			if (hoveredDrag[i])
			{
				ImGui::PushStyleColor(ImGuiCol_FrameBg, dragColor);
			}
			ImGui::SetNextItemWidth(inputWidth);
			ImGui::InputText(("##val" + std::to_string(i)).c_str(), &ent->keyvalues[ent->keyOrder[i]], ImGuiInputTextFlags_CallbackEdit,
							 TextChangeCallback::keyValueChanged, &valueIds[i]);
			if (ImGui::IsItemHovered() && ent->keyvalues[ent->keyOrder[i]].size())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(ent->keyvalues[ent->keyOrder[i]].c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}

			if (ent->keyOrder[i] == "angles" ||
				ent->keyOrder[i] == "angle")
			{
				if (IsEntNotSupportAngles(ent->keyvalues["classname"]))
				{
					ImGui::SetNextItemWidth(inputWidth);
					ImGui::TextUnformatted(get_localized_string(LANG_0671).c_str());
				}
				else if (ent->keyvalues["classname"] == "env_sprite")
				{
					ImGui::SetNextItemWidth(inputWidth);
					ImGui::TextUnformatted(get_localized_string(LANG_0672).c_str());
				}
				else if (ent->keyvalues["classname"] == "func_breakable")
				{
					ImGui::SetNextItemWidth(inputWidth);
					ImGui::TextUnformatted(get_localized_string(LANG_0673).c_str());
				}
			}

			if (hoveredDrag[i])
			{
				ImGui::PopStyleColor();
			}

			ImGui::NextColumn();
		}
		{
			std::string keyOrdname = ent->keyOrder[i];
			ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
			if (ImGui::Button((" X ##delorder" + keyOrdname).c_str()))
			{
				for (auto selected_entId : app->pickInfo.selectedEnts)
				{
					map->ents[selected_entId]->removeKeyvalue(keyOrdname);
					map->getBspRender()->refreshEnt((int)selected_entId);
				}
				app->updateEntConnections();
				map->getBspRender()->pushEntityUndoStateDelay("Delete Keyvalue RAW");
			}
			ImGui::PopStyleColor(3);
			ImGui::NextColumn();
		}
	}

	if (!keyDragging && wasKeyDragging)
	{
		map->getBspRender()->refreshEnt(entIdx);
		map->getBspRender()->pushEntityUndoStateDelay("Move Keyvalue");
	}

	wasKeyDragging = keyDragging;

	lastPickCount = pickCount;

	ImGui::Columns(1);

	ImGui::Dummy(ImVec2(0, style.FramePadding.y));
	ImGui::Dummy(ImVec2(butColWidth, 0));
	ImGui::SameLine();

	static std::string keyName = "NewKey";

	if (ImGui::Button(get_localized_string(LANG_0674).c_str()))
	{
		for (auto selected_entId : app->pickInfo.selectedEnts)
		{
			if (!map->ents[selected_entId]->hasKey(keyName))
			{
				map->ents[selected_entId]->addKeyvalue(keyName, "");
				map->getBspRender()->refreshEnt((int)selected_entId);
			}
		}
		keyName.clear();
		map->getBspRender()->pushEntityUndoStateDelay("Add Keyvalue");
	}
	ImGui::SameLine();

	ImGui::InputText(get_localized_string(LANG_0675).c_str(), &keyName);

	ImGui::Separator();

	bool multipleEnts = app->pickInfo.selectedEnts.size() > 1;

	if (multipleEnts)
		ImGui::BeginDisabled();
	if (ImGui::Button(get_localized_string(LANG_1207).c_str()))
	{
		ImGui::SetClipboardText(ent->serialize().c_str());
	}
	if (multipleEnts)
		ImGui::EndDisabled();

	float pasteBtnWidth = ImGui::CalcTextSize(get_localized_string(LANG_1208).c_str()).x + style.FramePadding.x * 2.0f;
	ImGui::SameLine(ImGui::GetWindowWidth() - pasteBtnWidth - style.WindowPadding.x);

	if (multipleEnts)
		ImGui::BeginDisabled();
	if (ImGui::Button(get_localized_string(LANG_1208).c_str()))
	{
		ImGui::OpenPopup(get_localized_string(LANG_1209).c_str());
	}
	if (multipleEnts)
		ImGui::EndDisabled();

	if (ImGui::BeginPopupModal(get_localized_string(LANG_1209).c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text(get_localized_string(LANG_1210).c_str());
		ImGui::Separator();

		if (ImGui::Button(get_localized_string(LANG_0943).c_str(), ImVec2(120, 0)))
		{
			const char* clipText = ImGui::GetClipboardText();
			if (clipText)
			{
				std::string clipboard = clipText;
				if (clipboard.find("classname") == std::string::npos)
				{
					clipboard = "{\n\"classname\" \"_temp\"\n" + clipboard + "\n}";
				}

				std::vector<Entity*> newEnts = load_ents(clipboard, "clipboard");
				if (newEnts.size() > 0)
				{
					Entity* source = newEnts[0];
					std::string oldClassname = ent->keyvalues["classname"];
					std::string oldModel = ent->hasKey("model") ? ent->keyvalues["model"] : "";

					ent->keyvalues.clear();
					ent->keyOrder.clear();

					ent->setOrAddKeyvalue("classname", oldClassname);
					if (!oldModel.empty())
					{
						ent->setOrAddKeyvalue("model", oldModel);
					}

					for (auto const& key : source->keyOrder)
					{
						if (key == "classname" || key == "model")
							continue;
						ent->setOrAddKeyvalue(key, source->keyvalues[key]);
					}

					map->getBspRender()->refreshEnt(entIdx);
					app->updateEntConnections();
					map->getBspRender()->pushEntityUndoStateDelay("Paste Keyvalues");
				}

				for (auto e : newEnts)
					delete e;
			}

			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::SameLine();
		if (ImGui::Button(get_localized_string(LANG_0945).c_str(), ImVec2(120, 0)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::EndChild();
}

void Gui::drawMDLWidget()
{
	Bsp* map = app->getSelectedMap();
	ImGui::SetNextWindowSize(ImVec2(410.f, 200.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(410.f, 330.f), ImVec2(410.f, 330.f));

	int sequenceCount = map->map_mdl->GetSequenceCount();
	static int MDL_Sequence = map->map_mdl->GetSequence();
	static int prev_MDL_Sequence = MDL_Sequence;

	int bodyCount = map->map_mdl->GetBodyCount();
	static int MDL_Body = map->map_mdl->GetBody();
	static int prev_MDL_Body = MDL_Body;

	int skinCount = map->map_mdl->GetSkinCount();
	static int MDL_Skin = map->map_mdl->GetSkin();
	static int prev_MDL_Skin = MDL_Skin;

	if (ImGui::Begin(fmt::format("{}###MDL_WIDGET", get_localized_string("LANG_MDL_WIDGET")).c_str()))
	{
		ImGui::InputInt("Sequence", &MDL_Sequence);
		ImGui::InputInt("Body", &MDL_Body);
		ImGui::InputInt("Skin", &MDL_Skin);

		if (MDL_Sequence < 0)
			MDL_Sequence = 0;
		if (MDL_Sequence > sequenceCount)
			MDL_Sequence = sequenceCount;

		if (MDL_Body < 0)
			MDL_Body = 0;
		if (MDL_Body > bodyCount)
			MDL_Body = bodyCount;

		if (MDL_Skin < 0)
			MDL_Skin = 0;
		if (MDL_Skin > skinCount)
			MDL_Skin = skinCount;

		if (MDL_Sequence != prev_MDL_Sequence)
		{
			map->map_mdl->SetSequence(MDL_Sequence);
			prev_MDL_Sequence = MDL_Sequence;
		}

		if (MDL_Body != prev_MDL_Body)
		{
			map->map_mdl->SetBody(MDL_Body);
			prev_MDL_Body = MDL_Body;
		}

		if (MDL_Skin != prev_MDL_Skin)
		{
			map->map_mdl->SetSkin(MDL_Skin);
			prev_MDL_Skin = MDL_Skin;
		}
	}
	ImGui::End();
}

void Gui::drawGOTOWidget()
{
	ImGui::SetNextWindowSize(ImVec2(410.f, 210.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(410.f, 340.f), ImVec2(410.f, 340.f));
	static vec3 coordinates = vec3();
	static vec3 angles = vec3();
	float angles_y = 0.0f;
	static int modelid = -1, faceid = -1, entid = -1, leafid = -1;
	static bool use_model_offset = false;

	if (ImGui::Begin(fmt::format("{}###GOTO_WIDGET", get_localized_string(LANG_0676)).c_str(), &showGOTOWidget, 0))
	{
		ImGuiStyle& style = ImGui::GetStyle();
		float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
		float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.33f;
		if (showGOTOWidget_update)
		{
			entid = g_app->pickInfo.selectedEnts.size() && g_app->pickInfo.selectedEnts[0] > 0 ? (int)g_app->pickInfo.selectedEnts[0] : -1;
			coordinates = cameraOrigin;
			angles = cameraAngles;
			angles = angles.normalize_angles();
			angles.z -= 90.0f;
			angles.y = angles.z;
			angles.z = 0.0f;
			angles.unflip();
			showGOTOWidget_update = false;
		}
		ImGui::Text(get_localized_string(LANG_0677).c_str());
		ImGui::PushItemWidth(inputWidth);
		ImGui::DragFloat(get_localized_string(LANG_0678).c_str(), &coordinates.x, 0.1f, 0, 0, "Y: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat(get_localized_string(LANG_0679).c_str(), &coordinates.y, 0.1f, 0, 0, "X: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat(get_localized_string(LANG_0680).c_str(), &coordinates.z, 0.1f, 0, 0, "Z: %.0f");
		ImGui::PopItemWidth();
		ImGui::Text(get_localized_string(LANG_0681).c_str());
		ImGui::PushItemWidth(inputWidth);
		ImGui::DragFloat(get_localized_string(LANG_0683).c_str(), &angles.x, 0.1f, 0, 0, "Pitch: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat(get_localized_string(LANG_0682).c_str(), &angles.z, 0.1f, 0, 0, "Yaw: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat(get_localized_string(LANG_0684).c_str(), &angles_y, 0.1f, 0, 0, "Roll: %.0f");
		ImGui::PopItemWidth();
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Not supported camera rolling");
			ImGui::EndTooltip();
		}

		Bsp* map = app->getSelectedMap();
		ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
		if (map && ImGui::Button("Go to"))
		{
			cameraOrigin = coordinates;
			map->getBspRender()->renderCameraOrigin = cameraOrigin;

			cameraAngles = angles.flipUV();
			cameraAngles.z = cameraAngles.y + 90.0f;
			cameraAngles = cameraAngles.normalize_angles();
			cameraAngles.y = 0.0f;
			map->getBspRender()->renderCameraAngles = cameraAngles;

			makeVectors(cameraAngles, app->cameraForward, app->cameraRight, app->cameraUp);
		}
		ImGui::PopStyleColor(3);
		if (map && !map->is_mdl_model)
		{
			ImGui::Separator();
			ImGui::PushItemWidth(inputWidth);
			ImGui::DragInt(get_localized_string(LANG_0685).c_str(), &modelid);
			ImGui::DragInt(get_localized_string(LANG_0686).c_str(), &faceid);

			ImGui::SameLine();
			ImGui::Checkbox("Face of model##face_app_mdl", &use_model_offset);
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted("Use model first face as offset.");
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}

			ImGui::DragInt(get_localized_string(LANG_0687).c_str(), &entid);
			ImGui::DragInt("Leaf", &leafid);
			ImGui::PopItemWidth();
			if (ImGui::Button("Go to##2"))
			{
				if (modelid >= 0 && modelid < map->modelCount && faceid < 0)
				{
					app->pickMode = PICK_OBJECT;
					for (int i = 0; i < (int)map->ents.size(); i++)
					{
						if (map->ents[i]->getBspModelIdx() == modelid)
						{
							app->selectEnt(map, i);
							app->goToEnt(map, i);
							break;
						}
					}
				}
				else if (faceid >= 0 && faceid < map->faceCount)
				{
					app->pickMode = PICK_FACE;
					app->goToFace(map, faceid);
					int modelIdx = use_model_offset && modelid >= 0 ? modelid : map->get_model_from_face(faceid);
					if (modelIdx >= 0)
					{
						for (size_t i = 0; i < map->ents.size(); i++)
						{
							if (map->ents[i]->getBspModelIdx() == modelid)
							{
								app->pickInfo.SetSelectedEnt((int)i);
								break;
							}
						}
					}
					if (use_model_offset && modelid >= 0 && modelid < map->modelCount)
					{
						app->selectFace(map, faceid + map->models[modelid].iFirstFace);
					}
					else
					{
						app->selectFace(map, faceid);
					}
				}
				else if (leafid > 0 && leafid < (int)map->leafCount)
				{
					app->pickMode = PICK_FACE;
					BSPLEAF32& leaf = map->leaves[leafid];
					app->goToCoords(getCenter(leaf.nMins, leaf.nMaxs));
				}
				else if (entid > 0 && entid < (int)map->ents.size())
				{
					app->pickMode = PICK_OBJECT;
					app->selectEnt(map, entid);
					app->goToEnt(map, entid);
				}

				if (modelid != -1 && entid != -1 ||
					modelid != -1 && faceid != -1 ||
					entid != -1 && faceid != -1)
				{
					modelid = entid = faceid = -1;
				}
			}
		}
	}

	ImGui::End();
}
void Gui::drawTransformWidget()
{
	Entity* ent = NULL;
	int modelIdx = -1;
	auto entIdx = app->pickInfo.selectedEnts;
	Bsp* map = app->getSelectedMap();
	BspRenderer* rend = map ? map->getBspRender() : NULL;

	if (map && entIdx.size())
	{
		ent = map->ents[entIdx[0]];
		modelIdx = ent->getBspModelIdx();
	}

	ImGui::SetNextWindowSize(ImVec2(440.f, 450.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(430, 100.f), ImVec2(FLT_MAX, app->windowHeight - 40.f));

	static float new_x, new_y, new_z;
	static float new_scale_x, new_scale_y, new_scale_z;

	static float default_x, default_y, default_z;
	static float default_scale_x, default_scale_y, default_scale_z;

	static int lastPickCount = -1;
	static int lastVertPickCount = -1;
	static bool oldSnappingEnabled = app->gridSnappingEnabled;

	if (ImGui::Begin(fmt::format("{}###TRANSFORM_WIDGET", get_localized_string(LANG_0688)).c_str(), &showTransformWidget, 0))
	{
		if (!ent)
		{
			ImGui::Text(get_localized_string(LANG_1180).c_str());
		}
		else
		{
			ImGuiStyle& style = ImGui::GetStyle();

			TransformAxes& activeAxes = *(app->transformMode == TRANSFORM_MODE_SCALE ? &app->scaleAxes : &app->moveAxes);

			int currentTransformMode = app->transformMode;
			int currentTransformTarget = app->transformTarget;

			if (updateTransformWidget)
			{
				if (app->transformTarget == TRANSFORM_VERTEX)
				{
					new_x = activeAxes.origin.x;
					new_y = activeAxes.origin.y;
					new_z = activeAxes.origin.z;
				}
				else if (app->transformTarget == TRANSFORM_ORIGIN)
				{
					if (modelIdx > 0 && modelIdx < map->modelCount)
					{
						new_x = map->models[modelIdx].vOrigin.x;
						new_y = map->models[modelIdx].vOrigin.y;
						new_z = map->models[modelIdx].vOrigin.z;
					}
				}
				else
				{
					new_x = ent->origin.x;
					new_y = ent->origin.y;
					new_z = ent->origin.z;
				}

				if (app->transformTarget == TRANSFORM_VERTEX)
				{
					new_scale_x = new_scale_y = new_scale_z = 1.0f;
				}
				else
				{
					if (modelIdx <= 0)
					{
						new_scale_x = new_scale_y = new_scale_z = 0.0f;
					}
					else
					{
						new_scale_x = app->selectionSize.x;
						new_scale_y = app->selectionSize.y;
						new_scale_z = app->selectionSize.z;
					}
				}

				default_scale_x = new_scale_x;
				default_scale_y = new_scale_y;
				default_scale_z = new_scale_z;

				default_x = new_x;
				default_y = new_y;
				default_z = new_z;

				updateTransformWidget = false;
			}

			oldSnappingEnabled = app->gridSnappingEnabled;
			lastVertPickCount = vertPickCount;
			lastPickCount = pickCount;

			guiHoverAxis = -1;

			float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
			float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.33f;
			float inputWidth4 = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.25f;

			float dragPow = app->gridSnappingEnabled ? app->snapSize : 0.02f;

			static double LastTransformUpdateTime = 0.0;

			ImGui::Text(get_localized_string(LANG_0689).c_str());
			ImGui::PushItemWidth(inputWidth);

			ImGui::DragFloat(get_localized_string(LANG_1107).c_str(), &new_x, dragPow, -g_limits.fltMaxCoord, g_limits.fltMaxCoord, "Y: %.2f");

			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 0;
			ImGui::SameLine();

			ImGui::DragFloat(get_localized_string(LANG_1108).c_str(), &new_y, dragPow, -g_limits.fltMaxCoord, g_limits.fltMaxCoord, "X: %.2f");

			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 1;
			ImGui::SameLine();

			ImGui::DragFloat(get_localized_string(LANG_1109).c_str(), &new_z, dragPow, -g_limits.fltMaxCoord, g_limits.fltMaxCoord, "Z: %.2f");

			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 2;

			ImGui::PopItemWidth();

			ImGui::Dummy(ImVec2(0, style.FramePadding.y));

			ImGui::Text(get_localized_string(LANG_0690).c_str());
			ImGui::PushItemWidth(inputWidth);

			if (modelIdx == 0 || !app->isTransformableSolid || app->modelUsesSharedStructures || app->transformMode != TRANSFORM_MODE_SCALE)
			{
				ImGui::BeginDisabled();
			}

			ImGui::DragFloat(get_localized_string(LANG_0691).c_str(), &new_scale_x, dragPow, 0, 0, "Y: %.2f");

			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 0;

			ImGui::SameLine();

			ImGui::DragFloat(get_localized_string(LANG_0692).c_str(), &new_scale_y, dragPow, 0, 0, "X: %.2f");

			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 1;

			ImGui::SameLine();

			ImGui::DragFloat(get_localized_string(LANG_0693).c_str(), &new_scale_z, dragPow, 0, 0, "Z: %.2f");

			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 2;

			if (modelIdx == 0 || !app->isTransformableSolid || app->modelUsesSharedStructures || app->transformMode != TRANSFORM_MODE_SCALE)
			{
				ImGui::EndDisabled();
			}

			ImGui::Dummy(ImVec2(0, style.FramePadding.y * 3));
			ImGui::PopItemWidth();

			ImGui::Dummy(ImVec2(0, style.FramePadding.y));
			ImGui::Separator();
			ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));

			ImGui::Columns(4, 0, false);
			ImGui::SetColumnWidth(0, inputWidth4);
			ImGui::SetColumnWidth(1, inputWidth4);
			ImGui::SetColumnWidth(2, inputWidth4);
			ImGui::SetColumnWidth(3, inputWidth4);
			ImGui::AlignTextToFramePadding();
			ImGui::Text(get_localized_string(LANG_0694).c_str());
			ImGui::NextColumn();

			if (modelIdx == 0 || app->transformMode == TRANSFORM_MODE_NONE)
			{
				ImGui::BeginDisabled();
			}

			if (ImGui::RadioButton(get_localized_string(LANG_0695).c_str(), &app->transformTarget, TRANSFORM_OBJECT))
			{
				pickCount++;
				vertPickCount++;
			}

			ImGui::NextColumn();
			if (modelIdx <= 0)
			{
				ImGui::BeginDisabled();
				if (app->transformTarget == TRANSFORM_ORIGIN || app->transformTarget == TRANSFORM_VERTEX)
				{
					app->transformTarget = TRANSFORM_OBJECT;
				}
			}
			if (modelIdx == 0 || !app->isTransformableSolid || app->modelUsesSharedStructures)
			{
				if (app->transformTarget == TRANSFORM_VERTEX)
				{
					app->transformTarget = TRANSFORM_OBJECT;
				}
				ImGui::BeginDisabled();
			}
			if (ImGui::RadioButton(get_localized_string(LANG_0696).c_str(), &app->transformTarget, TRANSFORM_VERTEX))
			{
				pickCount++;
				vertPickCount++;
			}
			if (modelIdx == 0 || !app->isTransformableSolid || app->modelUsesSharedStructures)
			{
				ImGui::EndDisabled();
			}

			if (modelIdx == 0 || app->transformMode == TRANSFORM_MODE_SCALE)
			{
				ImGui::BeginDisabled();
				if (app->transformTarget == TRANSFORM_ORIGIN)
				{
					app->transformTarget = TRANSFORM_OBJECT;
				}
			}

			ImGui::NextColumn();

			if (ImGui::RadioButton(get_localized_string(LANG_0697).c_str(), &app->transformTarget, TRANSFORM_ORIGIN))
			{
				pickCount++;
				vertPickCount++;
			}
			if (modelIdx <= 0)
			{
				ImGui::EndDisabled();
			}
			ImGui::NextColumn();
			if (modelIdx == 0 || app->transformMode == TRANSFORM_MODE_SCALE)
			{
				ImGui::EndDisabled();
			}
			if (modelIdx == 0 || app->transformMode == TRANSFORM_MODE_NONE)
			{
				ImGui::EndDisabled();
			}
			ImGui::Text(get_localized_string(LANG_0698).c_str());
			ImGui::NextColumn();
			ImGui::RadioButton(get_localized_string(LANG_1110).c_str(), &app->transformMode, TRANSFORM_MODE_NONE);
			ImGui::NextColumn();
			ImGui::RadioButton(get_localized_string(LANG_1111).c_str(), &app->transformMode, TRANSFORM_MODE_MOVE);
			ImGui::NextColumn();
			if (modelIdx <= 0 || !app->isTransformableSolid || app->modelUsesSharedStructures)
			{
				if (app->transformMode == TRANSFORM_MODE_SCALE)
					app->transformMode = TRANSFORM_MODE_MOVE;
				ImGui::BeginDisabled();
			}
			ImGui::RadioButton(get_localized_string(LANG_1112).c_str(), &app->transformMode, TRANSFORM_MODE_SCALE);
			ImGui::NextColumn();
			if (modelIdx <= 0 || !app->isTransformableSolid || app->modelUsesSharedStructures)
			{
				ImGui::EndDisabled();
			}
			ImGui::Columns(1);

			const char* element_names[] = {"0", "0.01", "0.1", "0.5", "1", "2", "4", "8", "16", "32", "64"};
			const int grid_snap_modes = sizeof(element_names) / sizeof(element_names[0]);
			const float element_values[grid_snap_modes] = {0.00001f, 0.01f, 0.1f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f, 32.f, 64.f};

			int current_element = app->gridSnapLevel;

			ImGui::Columns(2, 0, false);
			ImGui::SetColumnWidth(0, inputWidth4);
			ImGui::SetColumnWidth(1, inputWidth4 * 3);
			ImGui::Text(get_localized_string(LANG_0699).c_str());
			ImGui::NextColumn();
			ImGui::SetNextItemWidth(inputWidth4 * 3);

			if (ImGui::SliderInt(get_localized_string(LANG_0700).c_str(), &current_element, 0, grid_snap_modes - 1, element_names[current_element]))
			{
				app->gridSnapLevel = current_element;
				app->updateGridSnap();
				g_settings_changed = true;
			}

			ImGui::Columns(1);

			ImGui::Separator();
			ImGui::Dummy(ImVec2(0, style.FramePadding.y));

			bool anyBrushSelected = false;
			for (auto& eIdx : app->pickInfo.selectedEnts)
			{
				if (map->ents[eIdx]->isBspModel() && !map->ents[eIdx]->isWorldSpawn())
				{
					anyBrushSelected = true;
					break;
				}
			}

			auto applyTransform = [&](mat4x4 matrix, std::string desc)
			{
				vec3 mins(FLT_MAX, FLT_MAX, FLT_MAX);
				vec3 maxs(-FLT_MAX, -FLT_MAX, -FLT_MAX);
				for (auto& eIdx : app->pickInfo.selectedEnts)
				{
					Entity* tmpEnt = map->ents[eIdx];
					int mIdx = tmpEnt->getBspModelIdx();
					if (mIdx >= 0)
					{
						vec3 emins, emaxs;
						map->get_bounding_box(mIdx, emins, emaxs);
						expandBoundingBox(emins + tmpEnt->origin, mins, maxs);
						expandBoundingBox(emaxs + tmpEnt->origin, mins, maxs);
					}
					else
					{
						expandBoundingBox(tmpEnt->origin, mins, maxs);
					}
				}
				vec3 collectiveCenter = getCenter(maxs, mins);

				for (auto& entIdx : app->pickInfo.selectedEnts)
				{
					Entity* tmpEnt = map->ents[entIdx];
					int mIdx = tmpEnt->getBspModelIdx();

					bool hasOrigin = tmpEnt->hasKey("origin");
					vec3 oldOrigin = tmpEnt->origin;

					vec4 v4(tmpEnt->origin - collectiveCenter, 1.0f);
					tmpEnt->origin = (matrix * v4).xyz() + collectiveCenter;
					if (hasOrigin)
						tmpEnt->setOrAddKeyvalue("origin", tmpEnt->origin.toKeyvalueString());

					if (mIdx > 0)
					{
						int newModelIdx = map->duplicate_model(mIdx);
						tmpEnt->setOrAddKeyvalue("model", "*" + std::to_string(newModelIdx));

						if (hasOrigin)
						{
							map->transform(newModelIdx, matrix, vec3(0, 0, 0));
						}
						else
						{
							map->transform(newModelIdx, matrix, collectiveCenter);
						}
						map->getBspRender()->refreshModel(newModelIdx);
					}
				}
				map->remove_unused_model_structures();
				map->getBspRender()->pushUndoState(desc, EDIT_MODEL_LUMPS | FL_ENTITIES);
				map->getBspRender()->preRenderEnts();
				pickCount++;
				updateTransformWidget = true;
			};

			if (!anyBrushSelected)
				ImGui::BeginDisabled();

			if (ImGui::Button(get_localized_string("FLIP_X").c_str()))
			{
				mat4x4 mat;
				mat.loadIdentity();
				mat.scale(-1, 1, 1);
				applyTransform(mat, get_localized_string("FLIP_X"));
			}
			ImGui::SameLine();
			if (ImGui::Button(get_localized_string("FLIP_Y").c_str()))
			{
				mat4x4 mat;
				mat.loadIdentity();
				mat.scale(1, -1, 1);
				applyTransform(mat, get_localized_string("FLIP_Y"));
			}

			ImGui::Dummy(ImVec2(0, style.FramePadding.y));

			if (ImGui::Button(get_localized_string("ROTATE_90_CW").c_str()))
			{
				mat4x4 mat;
				mat.loadIdentity();
				mat.rotateZ(-90.0f * (HL_PI / 180.0f));
				applyTransform(mat, get_localized_string("ROTATE_90_CW"));
			}
			ImGui::SameLine();
			if (ImGui::Button(get_localized_string("ROTATE_90_CCW").c_str()))
			{
				mat4x4 mat;
				mat.loadIdentity();
				mat.rotateZ(90.0f * (HL_PI / 180.0f));
				applyTransform(mat, get_localized_string("ROTATE_90_CCW"));
			}

			if (!anyBrushSelected)
				ImGui::EndDisabled();

			ImGui::PushItemWidth(inputWidth);
			ImGui::Checkbox(get_localized_string(LANG_0701).c_str(), &app->textureLock);
			ImGui::SameLine();
			ImGui::Text(get_localized_string(LANG_1113).c_str());
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(get_localized_string(LANG_0702).c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}

			ImGui::SameLine();
			if (modelIdx == 0 || app->transformMode != TRANSFORM_MODE_MOVE || app->transformTarget != TRANSFORM_OBJECT || app->modelUsesSharedStructures)
				ImGui::BeginDisabled();
			ImGui::Checkbox(get_localized_string(LANG_0703).c_str(), &app->moveOrigin);
			if (modelIdx == 0 || app->transformMode != TRANSFORM_MODE_MOVE || app->transformTarget != TRANSFORM_OBJECT || app->modelUsesSharedStructures)
				ImGui::EndDisabled();

			if (ImGui::IsItemHovered(ImGuiHoveredFlags_::ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(get_localized_string(LANG_0705).c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}

			ImGui::PopItemWidth();

			ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));
			ImGui::Separator(); /*
			 ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));
			 ImGui::Text(("Size: " + app->selectionSize.toKeyvalueString(false, "w ", "l ", "h")).c_str());
			 ImGui::Separator();*/

			ImGui::Text(fmt::format("Entity origin: {:.2f} {:.2f} {:.2f}", ent->origin.x, ent->origin.y, ent->origin.z).c_str());

			if (modelIdx >= 0 && map)
			{
				ImGui::Text(fmt::format("Model origin: {:.2f} {:.2f} {:.2f}", map->models[modelIdx].vOrigin.x, map->models[modelIdx].vOrigin.y, map->models[modelIdx].vOrigin.z).c_str());
				vec3 modelCenter = getCenter(map->models[modelIdx].nMins, map->models[modelIdx].nMaxs);
				ImGui::Text(fmt::format("Model center: {:.2f} {:.2f} {:.2f}", modelCenter.x, modelCenter.y, modelCenter.z).c_str());
				ImGui::Text(fmt::format("Model size/bounds: {:.2f} {:.2f} {:.2f} \n{:.2f} {:.2f} {:.2f} / {:.2f} {:.2f} {:.2f}", map->models[modelIdx].nMaxs.x - map->models[modelIdx].nMins.x, map->models[modelIdx].nMaxs.y - map->models[modelIdx].nMins.y, map->models[modelIdx].nMaxs.z - map->models[modelIdx].nMins.z, map->models[modelIdx].nMins.x, map->models[modelIdx].nMins.y, map->models[modelIdx].nMins.z, map->models[modelIdx].nMaxs.x, map->models[modelIdx].nMaxs.y, map->models[modelIdx].nMaxs.z).c_str());
			}

			if (currentTransformMode != app->transformMode || currentTransformTarget != app->transformTarget)
			{
				pickCount++;
				vertPickCount++;
			}

			bool needUpdate = new_scale_x != default_scale_x ||
							  new_scale_y != default_scale_y ||
							  new_scale_z != default_scale_z ||
							  new_x != default_x || new_y != default_y || new_z != default_z;

			if (needUpdate && app->curTime - LastTransformUpdateTime < 1.0)
			{
				needUpdate = false;
			}

			if (needUpdate)
			{
				updateTransformWidget = true;
				LastTransformUpdateTime = app->curTime;
				if (app->transformTarget == TRANSFORM_VERTEX)
				{
					vec3 org1 = vec3(default_x, default_y, default_z);
					vec3 org2 = vec3(new_x, new_y, new_z);
					vec3 delta = app->gridSnappingEnabled ? app->snapToGrid(org2 - org1) : org2 - org1;
					vec3 delta2 = org2 - org1;
					if (!delta.IsZero() && !delta2.IsZero())
					{
						app->moveSelectedVerts(delta);

						updateTransformWidget = true;
						vertPickCount++;
					}
				}
				else if (app->transformTarget == TRANSFORM_OBJECT)
				{
					vec3 org1 = vec3(default_x, default_y, default_z);
					vec3 org2 = vec3(new_x, new_y, new_z);
					vec3 delta = app->gridSnappingEnabled ? app->snapToGrid(org2 - org1) : org2 - org1;
					vec3 delta2 = org2 - org1;
					if (!delta.IsZero() && !delta2.IsZero())
					{
						ent->setOrAddKeyvalue("origin", org2.toKeyvalueString());
						map->getBspRender()->refreshEnt((int)entIdx[0]);
						app->updateEntConnectionPositions();

						updateTransformWidget = true;
						pickCount++;
					}
				}
				else if (app->transformTarget == TRANSFORM_ORIGIN)
				{
					vec3 org1 = vec3(default_x, default_y, default_z);
					vec3 org2 = vec3(new_x, new_y, new_z);
					vec3 delta = app->gridSnappingEnabled ? app->snapToGrid(org2 - org1) : org2 - org1;
					vec3 delta2 = org2 - org1;
					if (!delta.IsZero() && !delta2.IsZero())
					{
						if (modelIdx > 0 && modelIdx < map->modelCount)
						{
							map->models[modelIdx].vOrigin = org2;
						}

						updateTransformWidget = true;
						pickCount++;
					}
				}
				if (app->isTransformableSolid && !app->modelUsesSharedStructures && modelIdx > 0)
				{
					if (app->transformTarget == TRANSFORM_VERTEX)
					{
						vec3 org1 = vec3(1.0f, 1.0f, 1.0f);
						vec3 org2 = vec3(new_scale_x, new_scale_y, new_scale_z);
						vec3 delta = app->gridSnappingEnabled ? app->snapToGrid(org2 - org1) : org2 - org1;
						vec3 delta2 = org2 - org1;
						if (!delta.IsZero() && !delta2.IsZero())
						{
							app->scaleSelectedVerts(map, modelIdx, new_scale_x, new_scale_y, new_scale_z);

							updateTransformWidget = true;
							vertPickCount++;
						}
					}
					else
					{
						vec3 org1 = vec3(default_scale_x, default_scale_y, default_scale_z);
						vec3 org2 = vec3(new_scale_x, new_scale_y, new_scale_z);
						vec3 delta = app->gridSnappingEnabled ? app->snapToGrid(org2 - org1) : org2 - org1;
						vec3 delta2 = org2 - org1;
						if (!delta.IsZero() && !delta2.IsZero())
						{
							app->scaleSelectedObject(map, modelIdx, delta.x, delta.y, delta.z);
							map->getBspRender()->refreshModel(modelIdx);
							map->getBspRender()->refreshModelClipnodes(modelIdx);
							app->applyTransform(map, true);

							updateTransformWidget = true;
							vertPickCount++;
							pickCount++;
						}
					}

					app->updateSelectionSize(map, modelIdx);
				}
			}
		}
	}
	ImGui::End();
}

void Gui::loadFonts()
{
	const std::string fontPath = "./fonts/";
	const std::string mainFont = "calibri.ttf";
	std::vector<std::string> fontFiles;
	ImFontConfig config;
	config.SizePixels = fontSize * 2.0f;
	config.OversampleH = 3;
	config.OversampleV = 1;
	config.RasterizerMultiply = 1.5f;
	config.PixelSnapH = true;

	if (!fs::exists(fontPath) || !fs::is_directory(fontPath))
	{
		print_log(PRINT_RED, "Font directory does not exist or is not accessible.\n");
		FlushConsoleLog(true);
		return;
	}
	std::error_code err{};

	for (const auto& entry : fs::directory_iterator(fontPath, err))
	{
		if (entry.is_regular_file())
		{
			auto extension = entry.path().extension().string();
			extension = toLowerCase(extension);
			if (extension == ".ttf" || extension == ".ttc")
			{
				fontFiles.emplace_back(entry.path().string());
			}
		}
	}

	std::sort(fontFiles.begin(), fontFiles.end(), [&](const std::string& a, const std::string& b)
			  {
		bool isA = a.find(mainFont) != std::string::npos;
		bool isB = b.find(mainFont) != std::string::npos;
		if (isA && !isB) return true;
		if (!isA && isB) return false;
		return a < b; });

	ImVector<ImWchar> glyphRanges;
	ImFontGlyphRangesBuilder builder;
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesDefault());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesCyrillic());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesChineseFull());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesChineseSimplifiedCommon());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesGreek());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesKorean());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesJapanese());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesThai());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesVietnamese());
	builder.BuildRanges(&glyphRanges);

	config.GlyphRanges = glyphRanges.Data;

	ImFont* tmpFont = NULL;

	for (const auto& fontFile : fontFiles)
	{
		try
		{
			auto font = imgui_io->Fonts->AddFontFromFileTTF(fontFile.c_str(), fontSize, &config, glyphRanges.Data);
			if (!font)
			{
				print_log(PRINT_RED, "Invalid {} font.\n", fontFile);
			}
			else
			{
				tmpFont = font;
			}
			if (tmpFont)
				config.MergeMode = true;
		}
		catch (...)
		{
			print_log(PRINT_RED, "Invalid {} font.\n", fontFile);
		}
	}

	imgui_io->Fonts->Build();

	defaultFont = tmpFont;
	smallFont = tmpFont;
	consoleFont = tmpFont;
	largeFont = tmpFont;
	consoleFontLarge = tmpFont;
}

static bool ColorPicker(ImGuiIO* imgui_io, float* col, bool alphabar)
{
	const int EDGE_SIZE = 200; // = int( ImGui::GetWindowWidth() * 0.75f );
	const ImVec2 SV_PICKER_SIZE = ImVec2(EDGE_SIZE, EDGE_SIZE);
	const float SPACING = ImGui::GetStyle().ItemInnerSpacing.x;
	const float HUE_PICKER_WIDTH = 20.f;
	const float CROSSHAIR_SIZE = 7.0f;

	ImColor color(col[0], col[1], col[2]);
	bool value_changed = false;

	ImDrawList* draw_list = ImGui::GetWindowDrawList();

	// setup

	ImVec2 picker_pos = ImGui::GetCursorScreenPos();

	float hue, saturation, value;
	ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, hue, saturation, value);

	// draw hue bar

	ImColor colors[] = {ImColor(255, 0, 0),
						ImColor(255, 255, 0),
						ImColor(0, 255, 0),
						ImColor(0, 255, 255),
						ImColor(0, 0, 255),
						ImColor(255, 0, 255),
						ImColor(255, 0, 0)};

	for (int i = 0; i < 6; ++i)
	{
		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING, picker_pos.y + i * (SV_PICKER_SIZE.y / 6)),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING + HUE_PICKER_WIDTH,
				   picker_pos.y + (i + 1) * (SV_PICKER_SIZE.y / 6)),
			colors[i],
			colors[i],
			colors[i + 1],
			colors[i + 1]);
	}

	draw_list->AddLine(
		ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING - 2, picker_pos.y + hue * SV_PICKER_SIZE.y),
		ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING + 2 + HUE_PICKER_WIDTH, picker_pos.y + hue * SV_PICKER_SIZE.y),
		ImColor(255, 255, 255));

	// draw alpha bar

	if (alphabar)
	{
		float alpha = col[3];

		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * SPACING + HUE_PICKER_WIDTH, picker_pos.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * SPACING + 2 * HUE_PICKER_WIDTH, picker_pos.y + SV_PICKER_SIZE.y),
			ImColor(0, 0, 0), ImColor(0, 0, 0), ImColor(255, 255, 255), ImColor(255, 255, 255));

		draw_list->AddLine(
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * (SPACING - 2) + HUE_PICKER_WIDTH, picker_pos.y + alpha * SV_PICKER_SIZE.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * (SPACING + 2) + 2 * HUE_PICKER_WIDTH, picker_pos.y + alpha * SV_PICKER_SIZE.y),
			ImColor(255.f - alpha, 255.f, 255.f));
	}

	// draw color matrix

	{
		const ImU32 c_oColorBlack = ImGui::ColorConvertFloat4ToU32(ImVec4(0.f, 0.f, 0.f, 1.f));
		const ImU32 c_oColorBlackTransparent = ImGui::ColorConvertFloat4ToU32(ImVec4(0.f, 0.f, 0.f, 0.f));
		const ImU32 c_oColorWhite = ImGui::ColorConvertFloat4ToU32(ImVec4(1.f, 1.f, 1.f, 1.f));

		ImVec4 cHueValue(1, 1, 1, 1);
		ImGui::ColorConvertHSVtoRGB(hue, 1, 1, cHueValue.x, cHueValue.y, cHueValue.z);
		ImU32 oHueColor = ImGui::ColorConvertFloat4ToU32(cHueValue);

		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x, picker_pos.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x, picker_pos.y + SV_PICKER_SIZE.y),
			c_oColorWhite,
			oHueColor,
			oHueColor,
			c_oColorWhite);

		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x, picker_pos.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x, picker_pos.y + SV_PICKER_SIZE.y),
			c_oColorBlackTransparent,
			c_oColorBlackTransparent,
			c_oColorBlack,
			c_oColorBlack);
	}

	// draw cross-hair

	float x = saturation * SV_PICKER_SIZE.x;
	float y = (1 - value) * SV_PICKER_SIZE.y;
	ImVec2 p(picker_pos.x + x, picker_pos.y + y);
	draw_list->AddLine(ImVec2(p.x - CROSSHAIR_SIZE, p.y), ImVec2(p.x - 2, p.y), ImColor(255, 255, 255));
	draw_list->AddLine(ImVec2(p.x + CROSSHAIR_SIZE, p.y), ImVec2(p.x + 2, p.y), ImColor(255, 255, 255));
	draw_list->AddLine(ImVec2(p.x, p.y + CROSSHAIR_SIZE), ImVec2(p.x, p.y + 2), ImColor(255, 255, 255));
	draw_list->AddLine(ImVec2(p.x, p.y - CROSSHAIR_SIZE), ImVec2(p.x, p.y - 2), ImColor(255, 255, 255));

	// color matrix logic

	ImGui::InvisibleButton(get_localized_string(LANG_0860).c_str(), SV_PICKER_SIZE);

	if (ImGui::IsItemActive() && imgui_io->MouseDown[0])
	{
		ImVec2 mouse_pos_in_canvas = ImVec2(
			imgui_io->MousePos.x - picker_pos.x, imgui_io->MousePos.y - picker_pos.y);

		/**/ if (mouse_pos_in_canvas.x < 0)
			mouse_pos_in_canvas.x = 0;
		else if (mouse_pos_in_canvas.x >= SV_PICKER_SIZE.x - 1)
			mouse_pos_in_canvas.x = SV_PICKER_SIZE.x - 1;

		/**/ if (mouse_pos_in_canvas.y < 0)
			mouse_pos_in_canvas.y = 0;
		else if (mouse_pos_in_canvas.y >= SV_PICKER_SIZE.y - 1)
			mouse_pos_in_canvas.y = SV_PICKER_SIZE.y - 1;

		value = 1 - (mouse_pos_in_canvas.y / (SV_PICKER_SIZE.y - 1));
		saturation = mouse_pos_in_canvas.x / (SV_PICKER_SIZE.x - 1);
		value_changed = true;
	}

	// hue bar logic

	ImGui::SetCursorScreenPos(ImVec2(picker_pos.x + SPACING + SV_PICKER_SIZE.x, picker_pos.y));
	ImGui::InvisibleButton(get_localized_string(LANG_0861).c_str(), ImVec2(HUE_PICKER_WIDTH, SV_PICKER_SIZE.y));

	if (imgui_io->MouseDown[0] && (ImGui::IsItemHovered() || ImGui::IsItemActive()))
	{
		ImVec2 mouse_pos_in_canvas = ImVec2(
			imgui_io->MousePos.x - picker_pos.x, imgui_io->MousePos.y - picker_pos.y);

		/**/ if (mouse_pos_in_canvas.y < 0)
			mouse_pos_in_canvas.y = 0;
		else if (mouse_pos_in_canvas.y >= SV_PICKER_SIZE.y - 1)
			mouse_pos_in_canvas.y = SV_PICKER_SIZE.y - 1;

		hue = mouse_pos_in_canvas.y / (SV_PICKER_SIZE.y - 1);
		value_changed = true;
	}

	// alpha bar logic

	if (alphabar)
	{

		ImGui::SetCursorScreenPos(ImVec2(picker_pos.x + SPACING * 2 + HUE_PICKER_WIDTH + SV_PICKER_SIZE.x, picker_pos.y));
		ImGui::InvisibleButton(get_localized_string(LANG_0862).c_str(), ImVec2(HUE_PICKER_WIDTH, SV_PICKER_SIZE.y));

		if (imgui_io->MouseDown[0] && (ImGui::IsItemHovered() || ImGui::IsItemActive()))
		{
			ImVec2 mouse_pos_in_canvas = ImVec2(
				imgui_io->MousePos.x - picker_pos.x, imgui_io->MousePos.y - picker_pos.y);

			/**/ if (mouse_pos_in_canvas.y < 0)
				mouse_pos_in_canvas.y = 0;
			else if (mouse_pos_in_canvas.y >= SV_PICKER_SIZE.y - 1)
				mouse_pos_in_canvas.y = SV_PICKER_SIZE.y - 1;

			float alpha = mouse_pos_in_canvas.y / (SV_PICKER_SIZE.y - 1);
			col[3] = alpha;
			value_changed = true;
		}
	}

	// R,G,B or H,S,V color editor

	color = ImColor::HSV(hue >= 1.f ? hue - 10.f * (float)1e-6 : hue, saturation > 0.f ? saturation : 10.f * (float)1e-6, value > 0.f ? value : (float)1e-6);
	col[0] = color.Value.x;
	col[1] = color.Value.y;
	col[2] = color.Value.z;

	bool widget_used;
	ImGui::PushItemWidth((alphabar ? SPACING + HUE_PICKER_WIDTH : 0) +
						 SV_PICKER_SIZE.x + SPACING + HUE_PICKER_WIDTH - 2 * ImGui::GetStyle().FramePadding.x);
	widget_used = alphabar ? ImGui::ColorEdit4("", col) : ImGui::ColorEdit3("", col);
	ImGui::PopItemWidth();

	// try to cancel hue wrap (after ColorEdit), if any
	{
		float new_hue, new_sat, new_val;
		ImGui::ColorConvertRGBtoHSV(col[0], col[1], col[2], new_hue, new_sat, new_val);
		if (new_hue <= 0 && hue > 0)
		{
			if (new_val <= 0 && value != new_val)
			{
				color = ImColor::HSV(hue, saturation, new_val <= 0 ? value * 0.5f : new_val);
				col[0] = color.Value.x;
				col[1] = color.Value.y;
				col[2] = color.Value.z;
			}
			else if (new_sat <= 0)
			{
				color = ImColor::HSV(hue, new_sat <= 0 ? saturation * 0.5f : new_sat, new_val);
				col[0] = color.Value.x;
				col[1] = color.Value.y;
				col[2] = color.Value.z;
			}
		}
	}
	return value_changed || widget_used;
}

bool ColorPicker3(ImGuiIO* imgui_io, float col[3])
{
	return ColorPicker(imgui_io, col, false);
}

bool ColorPicker4(ImGuiIO* imgui_io, float col[4])
{
	return ColorPicker(imgui_io, col, true);
}

std::vector<COLOR3> colordata;

static const int ATLAS_SPACING = 1;

int LMapMaxWidth = 512;

void DrawImageAtOneBigLightMap(COLOR3* img, int w, int h, int x, int y)
{
	if (!img || w <= 0 || h <= 0)
		return;
	int bottom = y + h;
	size_t required = (size_t)LMapMaxWidth * (size_t)bottom;
	if (colordata.size() < required)
		colordata.resize(required, COLOR3(0, 0, 255));

	for (int yy = 0; yy < h; ++yy)
	{
		int dstRow = (y + yy) * LMapMaxWidth;
		int srcRow = yy * w;
		for (int xx = 0; xx < w; ++xx)
		{
			int dstIdx = dstRow + (x + xx);
			int srcIdx = srcRow + xx;
			if (dstIdx >= 0 && dstIdx < (int)colordata.size())
			{
				colordata[dstIdx] = img[srcIdx];
			}
		}
	}
}

void DrawOneBigLightMapAtImage(COLOR3* img, int w, int h, int x, int y)
{
	if (!img || w <= 0 || h <= 0)
		return;

	for (int yy = 0; yy < h; ++yy)
	{
		int dstRow = yy * w;
		int srcRow = (y + yy) * LMapMaxWidth;
		for (int xx = 0; xx < w; ++xx)
		{
			int srcIdx = srcRow + (x + xx);
			int dstIdx = dstRow + xx;
			if (srcIdx >= 0 && srcIdx < (int)colordata.size())
				img[dstIdx] = colordata[srcIdx];
			else
				img[dstIdx] = COLOR3(0, 0, 255);
		}
	}
}

struct PackEntry
{
	int faceIdx;
	int x, y;
	int w, h;
	int lightId;
};

static std::vector<PackEntry> PackFacesDeterministic(Bsp* map, const std::vector<int>& faces, int lightId, int atlasWidth, int& outAtlasHeight)
{
	std::vector<PackEntry> entries;
	int current_x = 0;
	int current_y = 0;
	int max_row_height = 0;
	int global_max_y = 0;

	for (int faceIdx : faces)
	{
		if (map->faces[faceIdx].nLightmapOffset < 0 || map->faces[faceIdx].nStyles[lightId] == 255)
			continue;

		int size[2];
		if (!map->GetFaceLightmapSize(faceIdx, size))
			continue;
		int sizeX = size[0], sizeY = size[1];
		if (sizeX <= 0 || sizeY <= 0)
			continue;

		if (current_x + sizeX > atlasWidth)
		{
			current_y += max_row_height + ATLAS_SPACING;
			current_x = 0;
			max_row_height = 0;
		}

		PackEntry e;
		e.faceIdx = faceIdx;
		e.lightId = lightId;
		e.x = current_x;
		e.y = current_y;
		e.w = sizeX;
		e.h = sizeY;
		entries.push_back(e);

		current_x += sizeX + ATLAS_SPACING;
		if (sizeY > max_row_height)
			max_row_height = sizeY;
		int bottom = current_y + sizeY;
		if (bottom > global_max_y)
			global_max_y = bottom;
	}

	outAtlasHeight = std::max(1, global_max_y);
	return entries;
}

std::vector<int> faces_to_export;

void ImportOneBigLightmapFile(Bsp* map)
{
	if (!faces_to_export.size())
	{
		for (int faceIdx = 0; faceIdx < map->faceCount; ++faceIdx)
			faces_to_export.push_back(faceIdx);
	}

	std::string importPath = g_working_dir + "exported_lighting/";
	for (int lightId = 0; lightId < MAX_LIGHTMAPS; ++lightId)
	{
		std::string filename = fmt::format(fmt::runtime(get_localized_string(LANG_0407)),
										   importPath.c_str(),
										   get_localized_string(LANG_0408),
										   lightId);

		unsigned char* image_bytes = nullptr;
		unsigned int w2 = 0, h2 = 0;
		auto error = lodepng_decode24_file(&image_bytes, &w2, &h2, filename.c_str());
		if (error != 0 || !image_bytes)
		{
			if (image_bytes)
				free(image_bytes);
			continue;
		}

		colordata.clear();
		try
		{
			colordata.resize((size_t)w2 * (size_t)h2);
			for (size_t i = 0, j = 0; i < colordata.size(); ++i, j += 3)
			{
				colordata[i].r = image_bytes[j + 0];
				colordata[i].g = image_bytes[j + 1];
				colordata[i].b = image_bytes[j + 2];
			}
		}
		catch (...)
		{
			free(image_bytes);
			print_log(PRINT_RED | PRINT_INTENSITY, "Memory error while loading atlas");
			continue;
		}
		free(image_bytes);

		int atlasHeight = 0;
		auto entries = PackFacesDeterministic(map, faces_to_export, lightId, LMapMaxWidth, atlasHeight);

		int atlasW = (int)w2;
		int atlasH = (int)h2;

		for (const auto& e : entries)
		{
			int faceIdx = e.faceIdx;
			int sizeX = e.w, sizeY = e.h;
			int lightmapSz = (int)((size_t)sizeX * sizeY * sizeof(COLOR3));
			int offset = map->faces[faceIdx].nLightmapOffset + lightId * lightmapSz;

			int sizeCheck[2];
			if (!map->GetFaceLightmapSize(faceIdx, sizeCheck))
				continue;
			if (sizeCheck[0] != sizeX || sizeCheck[1] != sizeY)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, "Face %d size changed since export: skip import for this face", faceIdx);
				continue;
			}

			if (!map->lightdata || offset < 0 || (size_t)offset + lightmapSz > map->lightDataLength)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, "Skipping write to map->lightdata: out of bounds or null (face %d)", faceIdx);
				continue;
			}

			std::vector<COLOR3> tmp;
			tmp.resize((size_t)sizeX * sizeY);
			for (int yy = 0; yy < sizeY; ++yy)
			{
				for (int xx = 0; xx < sizeX; ++xx)
				{
					int srcX = e.x + xx;
					int srcY = e.y + yy;
					if (srcX < 0 || srcY < 0 || srcX >= atlasW || srcY >= atlasH)
					{
						tmp[yy * sizeX + xx] = COLOR3(0, 0, 255);
					}
					else
					{
						tmp[yy * sizeX + xx] = colordata[srcY * atlasW + srcX];
					}
				}
			}

			memcpy((unsigned char*)(map->lightdata + offset), (unsigned char*)tmp.data(), lightmapSz);
		}
	}
}

float RandomFloat(float a, float b)
{
	float random = ((float)rand()) / (float)RAND_MAX;
	float diff = b - a;
	float r = random * diff;
	return a + r;
}

std::map<float, float> mapx;
std::map<float, float> mapy;
std::map<float, float> mapz;

void Gui::ExportOneBigLightmap(Bsp* map)
{
	std::string filename;
	faces_to_export.clear();

	if (app->pickInfo.selectedFaces.size() > 1)
	{
		faces_to_export = app->pickInfo.selectedFaces;
	}
	else
	{
		for (int faceIdx = 0; faceIdx < map->faceCount; ++faceIdx)
			faces_to_export.push_back(faceIdx);
	}

	for (int lightId = 0; lightId < MAX_LIGHTMAPS; ++lightId)
	{
		colordata.clear();
		int atlasHeight = 0;

		auto entries = PackFacesDeterministic(map, faces_to_export, lightId, LMapMaxWidth, atlasHeight);

		if (entries.empty())
			continue;

		size_t required = (size_t)LMapMaxWidth * (size_t)atlasHeight;
		colordata.resize(required, COLOR3(0, 0, 255));

		for (const auto& e : entries)
		{
			int sizeX = e.w, sizeY = e.h;
			int lightmapSz = (int)((size_t)sizeX * sizeY * sizeof(COLOR3));
			int offset = map->faces[e.faceIdx].nLightmapOffset + lightId * lightmapSz;

			COLOR3* src = nullptr;
			if (map->lightdata && offset >= 0 && (size_t)offset + lightmapSz <= map->lightDataLength)
				src = (COLOR3*)(map->lightdata + offset);

			if (src)
			{
				DrawImageAtOneBigLightMap(src, sizeX, sizeY, e.x, e.y);
			}
			else
			{
				std::vector<COLOR3> tmp((size_t)sizeX * sizeY, COLOR3(0, 0, 255));
				DrawImageAtOneBigLightMap(tmp.data(), sizeX, sizeY, e.x, e.y);
			}
		}

		std::string exportPath = g_working_dir + "exported_lighting/";
		createDir(exportPath);
		filename = fmt::format(fmt::runtime(get_localized_string(LANG_1061)),
							   exportPath.c_str(),
							   get_localized_string(LANG_1062),
							   lightId);
		print_log(get_localized_string(LANG_0412), filename);

		unsigned err = lodepng_encode24_file(filename.c_str(),
											 (const unsigned char*)colordata.data(),
											 LMapMaxWidth,
											 atlasHeight);
		if (err)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, "lodepng encode error: %u", err);
		}
	}
}

void ExportLightmap(const BSPFACE32& face, int faceIdx, Bsp* map)
{
	int size[2];
	map->GetFaceLightmapSize(faceIdx, size);
	std::string filename;
	std::string exportPath = g_working_dir + "exported_lighting/";
	createDir(exportPath);

	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (face.nStyles[i] == 255)
			continue;
		int lightmapSz = (int)((size_t)size[0] * size[1] * sizeof(COLOR3));
		int offset = face.nLightmapOffset + i * lightmapSz;
		filename = fmt::format(fmt::runtime(get_localized_string(LANG_0413)), exportPath.c_str(), get_localized_string(LANG_0408), faceIdx, i);
		print_log(get_localized_string(LANG_0414), filename);
		lodepng_encode24_file(filename.c_str(), (unsigned char*)(map->lightdata + offset), size[0], size[1]);
	}
}

void ImportLightmap(const BSPFACE32& face, int faceIdx, Bsp* map)
{
	std::string filename;
	int size[2];
	map->GetFaceLightmapSize(faceIdx, size);
	std::string importPath = g_working_dir + "exported_lighting/";
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (face.nStyles[i] == 255)
			continue;
		int lightmapSz = (int)((size_t)size[0] * size[1] * sizeof(COLOR3));
		int offset = face.nLightmapOffset + i * lightmapSz;
		filename = fmt::format(fmt::runtime(get_localized_string(LANG_1063)), importPath.c_str(), get_localized_string(LANG_1062), faceIdx, i);
		unsigned int w = size[0], h = size[1];
		unsigned int w2 = 0, h2 = 0;
		print_log(get_localized_string(LANG_0415), filename);
		unsigned char* image_bytes = NULL;
		auto error = lodepng_decode24_file(&image_bytes, &w2, &h2, filename.c_str());
		if (error == 0 && image_bytes)
		{
			if (w == w2 && h == h2)
			{
				memcpy((unsigned char*)(map->lightdata + offset), image_bytes, lightmapSz);
			}
			else
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0416), w, h);
			}
			free(image_bytes);
		}
		else
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0417));
		}
	}
}

void Gui::drawLightMapTool()
{
	static float colourPatch[3];
	static Texture* currentlightMap[MAX_LIGHTMAPS] = {NULL};
	static float windowWidth = 500.0f;
	static float windowHeight = 600.0f;
	static int lightmap_count = 0;
	static bool needPickColor = false;
	const char* light_names[] =
		{
			"ALL",
			"Main light",
			"Light 1",
			"Light 2",
			"Light 3"};

	static int light_offsets[] =
		{
			0,
			0,
			0,
			0,
			0};

	ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(windowWidth, windowHeight), ImVec2(windowWidth, -1.0f));

	if (ImGui::Begin(fmt::format("{}###LIGHTMAP_WIDGET", get_localized_string(LANG_0599)).c_str(), &showLightmapEditorWidget))
	{
		if (needPickColor)
		{
			ImGui::TextDisabled(get_localized_string(LANG_0863).c_str());
		}
		Bsp* map = app->getSelectedMap();
		if (map)
		{
			BspRenderer* renderer = map->getBspRender();
			int faceIdx = app->pickInfo.selectedFaces.size() ? (int)app->pickInfo.selectedFaces[0] : -1;
			BSPFACE32* face = NULL;
			int size[2]{};
			if (faceIdx >= 0)
			{
				face = &map->faces[faceIdx];
				map->GetFaceLightmapSize(faceIdx, size);
			}
			else
			{
				lightmap_count = 0;
			}
			if (showLightmapEditorUpdate && face)
			{
				lightmap_count = 0;

				for (int i = 0; i < MAX_LIGHTMAPS; i++)
				{
					delete currentlightMap[i];
					currentlightMap[i] = NULL;
				}

				for (int i = 0; i < MAX_LIGHTMAPS; i++)
				{
					if (face->nStyles[i] == 255)
						continue;
					int lightmapSz = (int)((size_t)size[0] * size[1] * sizeof(COLOR3));
					currentlightMap[i] = new Texture(size[0], size[1], new unsigned char[lightmapSz], "LIGHTMAP");
					int offset = face->nLightmapOffset + i * lightmapSz;
					light_offsets[i] = offset;
					if (!map->lightdata || offset + lightmapSz > map->lightDataLength)
						memset(currentlightMap[i]->getData(), 255, lightmapSz);
					else
						memcpy(currentlightMap[i]->getData(), map->lightdata + offset, lightmapSz);
					currentlightMap[i]->upload(Texture::TEXTURE_TYPE::TYPE_LIGHTMAP_NOFILTER);
					lightmap_count++;
					// print_log(get_localized_string(LANG_0418),i,offset);
				}

				windowWidth = lightmap_count > 1 ? 500.f : 290.f;
				showLightmapEditorUpdate = false;
			}
			ImVec2 imgSize = ImVec2(200, 200);
			for (int i = 0; i < lightmap_count; i++)
			{
				if (i == 0)
				{
					ImGui::Separator();
					ImGui::Dummy(ImVec2(50, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[1]);
					ImGui::SameLine();
					ImGui::Dummy(ImVec2(120, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[2]);
					ImGui::Separator();
					ImGui::TextDisabled(fmt::format("Offest:{}", light_offsets[i]).c_str());
				}

				if (i == 2)
				{
					ImGui::Separator();
					ImGui::Dummy(ImVec2(50, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[3]);
					ImGui::SameLine();
					ImGui::Dummy(ImVec2(150, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[4]);
					ImGui::Separator();
					ImGui::TextDisabled(fmt::format("Offest:{}", light_offsets[i]).c_str());
				}

				if (i == 1 || i > 2)
				{
					ImGui::SameLine();
				}
				else if (i == 2)
				{
					ImGui::Separator();
				}

				if (!currentlightMap[i])
				{
					ImGui::Dummy(ImVec2(200, 200));
					continue;
				}

				if (ImGui::ImageButton((std::to_string(i) + "_lightmap").c_str(), (ImTextureID)(long long)currentlightMap[i]->id, imgSize, ImVec2(0, 0), ImVec2(1, 1)))
				{
					float itemwidth = ImGui::GetItemRectMax().x - ImGui::GetItemRectMin().x;
					float itemheight = ImGui::GetItemRectMax().y - ImGui::GetItemRectMin().y;

					float mousex = ImGui::GetItemRectMax().x - ImGui::GetMousePos().x;
					float mousey = ImGui::GetItemRectMax().y - ImGui::GetMousePos().y;

					int imagex = (int)round((currentlightMap[i]->width - ((currentlightMap[i]->width / itemwidth) * mousex)) - 0.5f);
					int imagey = (int)round((currentlightMap[i]->height - ((currentlightMap[i]->height / itemheight) * mousey)) - 0.5f);

					if (imagex < 0)
					{
						imagex = 0;
					}
					if (imagey < 0)
					{
						imagey = 0;
					}
					if (imagex > currentlightMap[i]->width)
					{
						imagex = currentlightMap[i]->width;
					}
					if (imagey > currentlightMap[i]->height)
					{
						imagey = currentlightMap[i]->height;
					}

					int offset = ArrayXYtoId(currentlightMap[i]->width, imagex, imagey);
					int len = currentlightMap[i]->width * currentlightMap[i]->height * (int)sizeof(COLOR3);
					if (offset >= len)
						offset = len - 1;
					if (offset < 0)
						offset = 0;

					COLOR3* lighdata = (COLOR3*)currentlightMap[i]->getData();

					if (needPickColor)
					{
						colourPatch[0] = lighdata[offset].r / 255.f;
						colourPatch[1] = lighdata[offset].g / 255.f;
						colourPatch[2] = lighdata[offset].b / 255.f;
						needPickColor = false;
					}
					else
					{
						lighdata[offset] = COLOR3(FixBounds(colourPatch[0] * 255.f),
												  FixBounds(colourPatch[1] * 255.f), FixBounds(colourPatch[2] * 255.f));
						currentlightMap[i]->upload(Texture::TEXTURE_TYPE::TYPE_LIGHTMAP_NOFILTER);
					}
				}
			}
			if (face)
			{
				ImGui::Separator();
				ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0419)), size[0], size[1]).c_str());
				ImGui::Separator();
				ColorPicker3(imgui_io, colourPatch);
				ImGui::SetNextItemWidth(100.f);
				if (ImGui::Button(get_localized_string(LANG_0864).c_str(), ImVec2(120, 0)))
				{
					needPickColor = true;
				}
				ImGui::SameLine();
				if (ImGui::Button(get_localized_string("FILL").c_str(), ImVec2(120, 0)))
				{
					COLOR3 fillCol(FixBounds(colourPatch[0] * 255.f), FixBounds(colourPatch[1] * 255.f), FixBounds(colourPatch[2] * 255.f));
					map->save_undo_lightmaps();
					for (int fIdx : app->pickInfo.selectedFaces)
					{
						BSPFACE32& fillFace = map->faces[fIdx];
						int fillSize[2];
						map->GetFaceLightmapSize(fIdx, fillSize);
						int layerSize = fillSize[0] * fillSize[1];
						for (int i = 0; i < MAX_LIGHTMAPS; i++)
						{
							if (fillFace.nStyles[i] == 255)
								continue;
							// Only fill the layer currently being edited if we could identify it,
							// but here we just fill all active layers of the face to match the user's probable intent
							// for a "Fill" operation on selected faces.
							int offset = (int)(fillFace.nLightmapOffset + (size_t)i * layerSize * sizeof(COLOR3));
							if (map->lightdata && offset + layerSize * (int)sizeof(COLOR3) <= map->lightDataLength)
							{
								COLOR3* dst = (COLOR3*)(map->lightdata + offset);
								for (int k = 0; k < layerSize; k++)
									dst[k] = fillCol;
							}
						}
					}
					renderer->pushUndoState("Fill Lightmap", FL_LIGHTING);
					showLightmapEditorUpdate = true;
					renderer->reloadLightmaps();
				}
				ImGui::Separator();
			}
			ImGui::SetNextItemWidth(100.f);
			ImGui::Checkbox(light_names[1], &renderer->lightEnableFlags[0]);
			ImGui::SameLine();
			ImGui::Checkbox(light_names[2], &renderer->lightEnableFlags[1]);
			ImGui::Checkbox(light_names[3], &renderer->lightEnableFlags[2]);
			ImGui::SameLine();
			ImGui::Dummy({22, 0});
			ImGui::SameLine();
			ImGui::Checkbox(light_names[4], &renderer->lightEnableFlags[3]);
			ImGui::Separator();

			if (face)
			{
				if (ImGui::Button(get_localized_string(LANG_1126).c_str(), ImVec2(120, 0)))
				{
					for (int i = 0; i < MAX_LIGHTMAPS; i++)
					{
						if (face->nStyles[i] == 255 || !currentlightMap[i])
							continue;
						int lightmapSz = (int)((size_t)size[0] * size[1] * sizeof(COLOR3));
						int offset = face->nLightmapOffset + i * lightmapSz;
						memcpy(map->lightdata + offset, currentlightMap[i]->getData(), lightmapSz);
					}
					map->resize_all_lightmaps(true);
					renderer->pushUndoState(get_localized_string(LANG_0599), FL_LIGHTING);
				}
				ImGui::SameLine();

				if (ImGui::Button(get_localized_string(LANG_1127).c_str(), ImVec2(120, 0)))
				{
					showLightmapEditorUpdate = true;
				}

				ImGui::Separator();
				if (ImGui::Button(get_localized_string(LANG_1128).c_str(), ImVec2(120, 0)))
				{
					print_log(get_localized_string(LANG_0420));
					createDir(g_working_dir + "exported_lighting/");
					ExportLightmap(*face, faceIdx, map);
				}
				ImGui::SameLine();
				if (ImGui::Button(get_localized_string(LANG_1129).c_str(), ImVec2(120, 0)))
				{
					print_log(get_localized_string(LANG_0421));
					ImportLightmap(*face, faceIdx, map);
					showLightmapEditorUpdate = true;
					renderer->reloadLightmaps();
				}
				ImGui::Separator();
			}

			// ImGui::Text(get_localized_string(LANG_0866).c_str());
			// ImGui::Separator();
			if (ImGui::Button(get_localized_string(LANG_0867).c_str(), ImVec2(125, 0)))
			{
				print_log(get_localized_string(LANG_1064));
				createDir(g_working_dir + "exported_lighting/");

				// for (int z = 0; z < map->faceCount; z++)
				//{
				//	lightmaps = 0;
				//	ExportLightmaps(map->faces[z], z, map);
				// }

				ExportOneBigLightmap(map);
			}
			ImGui::SameLine();
			if (ImGui::Button(get_localized_string(LANG_0868).c_str(), ImVec2(125, 0)))
			{
				print_log(get_localized_string(LANG_1065));

				// for (int z = 0; z < map->faceCount; z++)
				//{
				//	lightmaps = 0;
				//	ImportLightmaps(map->faces[z], z, map);
				// }

				ImportOneBigLightmapFile(map);
				renderer->reloadLightmaps();
			}
		}
		else
		{
			ImGui::Text(get_localized_string(LANG_0869).c_str());
		}
	}
	ImGui::End();
}
void Gui::drawFaceEditorWidget()
{
	static float scroll_x = 0.0f;
	static float scroll_y = 0.0f;

	ImGui::SetNextWindowScroll(ImVec2(scroll_x, scroll_y));

	ImGui::SetNextWindowSize(ImVec2(300.f, 570.f), ImGuiCond_FirstUseEver);
	// ImGui::SetNextWindowSize(ImVec2(400, 600));
	bool beginFaceEditor = ImGui::Begin(fmt::format("{} {}###FACE_EDITOR_WIDGET", get_localized_string(LANG_0870),
													app->pickInfo.selectedFaces.size() != 1 ? std::string() : std::to_string(app->pickInfo.selectedFaces[0]))
											.c_str(),
										&showFaceEditWidget);

	if (beginFaceEditor && app->pickMode != PICK_FACE_LEAF)
	{
		static float scaleX, scaleY, shiftX, shiftY;
		static std::vector<std::array<int, 2>> lightmapSizes{};
		static float rotateX, rotateY;
		static bool lockRotate = true;
		static int bestplane;
		static bool isSpecial;
		static int width = 256, height = 256;
		static std::vector<vec3> edgeVerts;
		static ImTextureID textureId = NULL; // OpenGL ID
		static char textureName[MAXTEXTURENAME];
		static char textureName2[MAXTEXTURENAME];
		static int lastPickCount = -1;
		static int miptex = 0;
		static bool validTexture = true;
		static bool scaledX = false;
		static bool scaledY = false;
		static bool shiftedX = false;
		static bool shiftedY = false;
		static bool updatedTexVec = false;
		static bool updatedFaceVec = false;

		static float verts_merge_epsilon = 1.0f;

		static int tmpStyles[MAX_LIGHTMAPS] = {255, 255, 255, 255};
		static bool stylesChanged = false;

		Bsp* map = app->getSelectedMap();
		if (!map || app->pickMode == PICK_OBJECT || app->pickInfo.selectedFaces.empty())
		{
			ImGui::Text(get_localized_string(LANG_1130).c_str());
			ImGui::End();
			return;
		}
		BspRenderer* mapRenderer = map->getBspRender();
		if (!mapRenderer || !mapRenderer->texturesLoaded)
		{
			ImGui::Text(get_localized_string(LANG_0871).c_str());
			ImGui::End();
			return;
		}

		if (lastPickCount != pickCount && app->pickMode != PICK_OBJECT)
		{
			edgeVerts.clear();
			scaledX = scaledY = shiftedX = shiftedY = updatedTexVec = stylesChanged = updatedFaceVec = false;
			if (app->pickInfo.selectedFaces.size())
			{
				int faceIdx = (int)app->pickInfo.selectedFaces[0];
				if (faceIdx >= 0)
				{
					BSPFACE32& face = map->faces[faceIdx];
					BSPPLANE plane = map->getPlaneFromFace(&face);
					BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
					width = height = 0;

					if (texinfo.iMiptex >= 0 && texinfo.iMiptex < map->textureCount)
					{
						int texOffset = ((int*)map->textures)[texinfo.iMiptex + 1];
						if (texOffset >= 0)
						{
							BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
							width = tex.nWidth;
							height = tex.nHeight;
							memcpy(textureName, tex.szName, MAXTEXTURENAME);
							memcpy(textureName2, tex.szName, MAXTEXTURENAME);
						}
						else
						{
							textureName[0] = '\0';
							textureName2[0] = '\0';
						}
					}
					else
					{
						textureName[0] = '\0';
						textureName2[0] = '\0';
					}

					miptex = texinfo.iMiptex;

					vec3 xv, yv;
					bestplane = TextureAxisFromPlane(plane, xv, yv);

					rotateX = AngleFromTextureAxis(texinfo.vS, true, bestplane);
					rotateY = AngleFromTextureAxis(texinfo.vT, false, bestplane);

					scaleX = texinfo.vS.length() > EPSILON ? (1.0f / texinfo.vS.length()) : 1.0f;
					scaleY = texinfo.vT.length() > EPSILON ? (1.0f / texinfo.vT.length()) : 1.0f;

					shiftX = texinfo.shiftS;
					shiftY = texinfo.shiftT;

					isSpecial = texinfo.nFlags & TEX_SPECIAL;

					textureId = (ImTextureID)(size_t)mapRenderer->getFaceTextureId(faceIdx);
					validTexture = true;

					for (int i = 0; i < MAX_LIGHTMAPS; i++)
					{
						tmpStyles[i] = face.nStyles[i];
					}

					lightmapSizes.clear();

					int lmSize[2];
					map->GetFaceLightmapSize(faceIdx, lmSize);
					lightmapSizes.push_back({lmSize[0], lmSize[1]});

					// show default values if not all faces share the same values
					for (size_t i = 1; i < app->pickInfo.selectedFaces.size(); i++)
					{
						int faceIdx2 = app->pickInfo.selectedFaces[i];
						map->GetFaceLightmapSize((int)faceIdx2, lmSize);
						lightmapSizes.push_back({lmSize[0], lmSize[1]});
						BSPFACE32& face2 = map->faces[faceIdx2];
						BSPTEXTUREINFO& texinfo2 = map->texinfos[face2.iTextureInfo];

						float face2ScaleX = texinfo2.vS.length() > EPSILON ? (1.0f / texinfo2.vS.length()) : 1.0f;
						float face2ScaleY = texinfo2.vT.length() > EPSILON ? (1.0f / texinfo2.vT.length()) : 1.0f;
						if (std::abs(scaleX - face2ScaleX) > 0.0001f)
							scaleX = 1.0f;
						if (std::abs(scaleY - face2ScaleY) > 0.0001f)
							scaleY = 1.0f;

						if (std::abs(shiftX - texinfo2.shiftS) > 0.0001f)
							shiftX = 0;
						if (std::abs(shiftY - texinfo2.shiftT) > 0.0001f)
							shiftY = 0;

						if (isSpecial == !(texinfo2.nFlags & TEX_SPECIAL))
							isSpecial = false;

						if (texinfo2.iMiptex != miptex)
						{
							validTexture = false;
							textureId = NULL;
							width = 0;
							height = 0;
							textureName[0] = '\0';
						}
					}
					for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++)
					{
						int edgeIdx = map->surfedges[e];
						BSPEDGE32 edge = map->edges[abs(edgeIdx)];
						vec3 v = edgeIdx > 0 ? map->verts[edge.iVertex[0]] : map->verts[edge.iVertex[1]];
						edgeVerts.push_back(v);
					}
				}
			}
			else
			{
				scaleX = scaleY = shiftX = shiftY = 0.0f;
				width = height = 0;
				textureId = NULL;
				textureName[0] = '\0';
			}

			checkFaceErrors();
		}
		lastPickCount = pickCount;

		ImGuiStyle& style = ImGui::GetStyle();
		float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
		float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.5f;

		ImGui::PushItemWidth(inputWidth);

		if (app->pickInfo.selectedFaces.size() == 1)
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0422)), lightmapSizes[0][0], lightmapSizes[0][1], lightmapSizes[0][0] * lightmapSizes[0][1]).c_str());

		ImGui::Text(get_localized_string(LANG_1169).c_str());
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted(get_localized_string(LANG_0872).c_str());
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		bool itemDeactivatedAfterEdit = false;

		if (ImGui::DragFloat(get_localized_string(LANG_0873).c_str(), &scaleX, 0.001f, 0, 0, "X: %.3f"))
		{
			if (!std::isnan(scaleX) && !std::isinf(scaleX) && std::abs(scaleX) > 0.00001f)
			{
				scaledX = true;
			}
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
			itemDeactivatedAfterEdit = true;

		ImGui::SameLine();
		if (ImGui::DragFloat(get_localized_string(LANG_0874).c_str(), &scaleY, 0.001f, 0, 0, "Y: %.3f"))
		{
			if (!std::isnan(scaleY) && !std::isinf(scaleY) && std::abs(scaleY) > 0.00001f)
			{
				scaledY = true;
			}
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
			itemDeactivatedAfterEdit = true;

		ImGui::Dummy(ImVec2(0, 8));

		ImGui::Text(get_localized_string(LANG_0875).c_str());
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted(get_localized_string(LANG_0876).c_str());
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		if (ImGui::DragFloat(get_localized_string(LANG_0877).c_str(), &shiftX, 0.1f, 0, 0, "X: %.3f"))
		{
			shiftedX = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
			itemDeactivatedAfterEdit = true;

		ImGui::SameLine();
		if (ImGui::DragFloat(get_localized_string(LANG_0878).c_str(), &shiftY, 0.1f, 0, 0, "Y: %.3f"))
		{
			shiftedY = true;
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
			itemDeactivatedAfterEdit = true;

		ImGui::PopItemWidth();

		inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.3f;
		ImGui::PushItemWidth(inputWidth);

		ImGui::Text(get_localized_string(LANG_0879).c_str());
		ImGui::SameLine();
		ImGui::TextDisabled(get_localized_string(LANG_0880).c_str());

		if (ImGui::DragFloat(get_localized_string(LANG_0881).c_str(), &rotateX, 0.01f, 0, 0, "X: %.3f"))
		{
			updatedTexVec = true;
			if (rotateX > 360.0f)
				rotateX = 360.0f;
			if (rotateX < -360.0f)
				rotateX = -360.0f;
			if (lockRotate)
				rotateY = rotateX - 180.0f;
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
			itemDeactivatedAfterEdit = true;

		ImGui::SameLine();

		if (ImGui::DragFloat(get_localized_string(LANG_0882).c_str(), &rotateY, 0.01f, 0, 0, "Y: %.3f"))
		{
			updatedTexVec = true;
			if (rotateY > 360.0f)
				rotateY = 360.0f;
			if (rotateY < -360.0f)
				rotateY = -360.0f;
			if (lockRotate)
				rotateX = rotateY + 180.0f;
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
			itemDeactivatedAfterEdit = true;

		ImGui::SameLine();

		ImGui::Checkbox(get_localized_string(LANG_0883).c_str(), &lockRotate);

		// Handle continuous sliders in real time
		bool anySliderActive = scaledX || scaledY || shiftedX || shiftedY || updatedTexVec;
		if (anySliderActive)
		{
			for (size_t i = 0; i < app->pickInfo.selectedFaces.size(); i++)
			{
				int faceIdx = app->pickInfo.selectedFaces[i];
				BSPTEXTUREINFO* texinfo = map->get_unique_texinfo((int)faceIdx);

				if (shiftedX)
				{
					texinfo->shiftS = shiftX;
				}
				if (shiftedY)
				{
					texinfo->shiftT = shiftY;
				}

				if (updatedTexVec)
				{
					texinfo->vS = AxisFromTextureAngle(rotateX, true, bestplane);
					texinfo->vT = AxisFromTextureAngle(rotateY, false, bestplane);
					if (!std::isnan(scaleX) && !std::isinf(scaleX) && std::abs(scaleX) > 0.00001f && texinfo->vS.length() > EPSILON)
						texinfo->vS = texinfo->vS.normalize(1.0f / scaleX);
					if (!std::isnan(scaleY) && !std::isinf(scaleY) && std::abs(scaleY) > 0.00001f && texinfo->vT.length() > EPSILON)
						texinfo->vT = texinfo->vT.normalize(1.0f / scaleY);
				}

				if (scaledX && !std::isnan(scaleX) && !std::isinf(scaleX) && std::abs(scaleX) > 0.00001f)
				{
					if (texinfo->vS.length() > EPSILON)
						texinfo->vS = texinfo->vS.normalize(1.0f / scaleX);
				}
				if (scaledY && !std::isnan(scaleY) && !std::isinf(scaleY) && std::abs(scaleY) > 0.00001f)
				{
					if (texinfo->vT.length() > EPSILON)
						texinfo->vT = texinfo->vT.normalize(1.0f / scaleY);
				}

				mapRenderer->updateFaceUVs((int)faceIdx);
			}

			if (itemDeactivatedAfterEdit)
			{
				map->resize_all_lightmaps(true);
				mapRenderer->reloadLightmapsSync();
				for (size_t i = 0; i < app->pickInfo.selectedFaces.size(); i++)
				{
					mapRenderer->highlightFace((int)app->pickInfo.selectedFaces[i], 1);
				}

				map->getBspRender()->pushUndoState("Edit face", EDIT_MODEL_LUMPS);

				scaledX = scaledY = shiftedX = shiftedY = updatedTexVec = false;
				pickCount++;
				vertPickCount++;
			}
		}

		if (app->pickInfo.selectedFaces.size() > 1)
		{
			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0887).c_str());
			ImGui::DragFloat(get_localized_string(LANG_0888).c_str(), &verts_merge_epsilon, 0.1f, 0.0f, 1000.0f);
			if (ImGui::Button(get_localized_string(LANG_0889).c_str()))
			{
				for (auto faceIdx : app->pickInfo.selectedFaces)
				{
					vec3 lastvec = vec3();
					BSPFACE32& face = map->faces[faceIdx];
					for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++)
					{
						int edgeIdx = map->surfedges[e];
						BSPEDGE32 edge = map->edges[abs(edgeIdx)];

						vec3& vec = edgeIdx > 0 ? map->verts[edge.iVertex[0]] : map->verts[edge.iVertex[1]];

						for (int v = 0; v < map->vertCount; v++)
						{
							if (map->verts[v].z == vec.z && VectorCompare(map->verts[v], vec, verts_merge_epsilon))
							{
								if (vec != lastvec)
								{
									vec = map->verts[v];
									lastvec = vec;
									break;
								}
							}
						}
					}
				}

				map->remove_unused_model_structures(CLEAN_VERTICES);

				app->reloading = true;
				map->getBspRender()->reload();
				app->reloading = false;

				map->resize_all_lightmaps(true);
				mapRenderer->reloadLightmapsSync();
				for (size_t i = 0; i < app->pickInfo.selectedFaces.size(); i++)
				{
					mapRenderer->highlightFace((int)app->pickInfo.selectedFaces[i], 1);
				}

				map->getBspRender()->pushUndoState("Merge Vertices", EDIT_MODEL_LUMPS);
				pickCount++;
				vertPickCount++;
			}
			ImGui::Separator();
		}

		ImGui::PopItemWidth();

		ImGui::Text(get_localized_string(LANG_1131).c_str());
		if (ImGui::Checkbox(get_localized_string(LANG_0890).c_str(), &isSpecial))
		{
			std::set<int> modelRefreshes;
			for (size_t i = 0; i < app->pickInfo.selectedFaces.size(); i++)
			{
				int faceIdx = app->pickInfo.selectedFaces[i];
				BSPTEXTUREINFO* texinfo = map->get_unique_texinfo((int)faceIdx);
				if (!isSpecial)
					texinfo->nFlags &= ~TEX_SPECIAL;
				else
					texinfo->nFlags |= TEX_SPECIAL;

				int modelIdx = map->get_model_from_face((int)faceIdx);
				if (modelIdx >= 0)
					modelRefreshes.insert(modelIdx);

				mapRenderer->updateFaceUVs((int)faceIdx);
			}

			for (int m : modelRefreshes)
			{
				mapRenderer->refreshModel(m);
			}

			map->resize_all_lightmaps(true);
			mapRenderer->reloadLightmapsSync();
			for (size_t i = 0; i < app->pickInfo.selectedFaces.size(); i++)
			{
				mapRenderer->highlightFace((int)app->pickInfo.selectedFaces[i], 1);
			}
			mapRenderer->pushUndoState("Toggle Special Flag", EDIT_MODEL_LUMPS);
			pickCount++;
			vertPickCount++;
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Used with invisible faces to bypass the surface extent limit."
								   "\nLightmaps may break in strange ways if this is used on a normal face.");
			ImGui::EndTooltip();
		}
		ImGui::Dummy(ImVec2(0, 8));

		ImGui::Text(get_localized_string(LANG_0891).c_str());
		ImGui::SetNextItemWidth(inputWidth);
		if (!validTexture)
		{
			ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
		}

		bool enterPressed = ImGui::InputText(get_localized_string(LANG_0892).c_str(), textureName2, MAXTEXTURENAME, ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		ImGui::Text(fmt::format("#{}", miptex).c_str());

		ImGui::SameLine();

		bool applyTextureClicked = ImGui::Button("APPLY");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Apply texture or create one new.");
			ImGui::EndTooltip();
		}

		if (!validTexture)
		{
			ImGui::PopStyleColor();
		}

		if ((applyTextureClicked || enterPressed) && textureName2[0] != '\0')
		{
			unsigned int newMiptex = 0;
			validTexture = false;

			for (int i = 0; i < map->textureCount; i++)
			{
				int texOffset = ((int*)map->textures)[i + 1];
				if (texOffset >= 0)
				{
					BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
					if (strcasecmp(tex.szName, textureName2) == 0)
					{
						validTexture = true;
						newMiptex = i;
						break;
					}
				}
			}
			if (!validTexture)
			{
				for (auto& s : mapRenderer->wads)
				{
					if (s->hasTexture(textureName2))
					{
						WADTEX wadTex = s->readTexture(textureName2);
						COLOR3* imageData = ConvertWadTexToRGB(wadTex);

						validTexture = true;
						newMiptex = map->add_texture(textureName2, (unsigned char*)imageData, wadTex.nWidth, wadTex.nHeight);
						mapRenderer->reuploadTextures();
						mapRenderer->preRenderFaces();

						delete[] imageData;
						break;
					}
				}
			}
			if (!validTexture)
			{
				validTexture = true;
				COLOR3 rndColor;
				rndColor.r = 50 + rand() % 206;
				rndColor.g = 50 + rand() % 206;
				rndColor.b = 50 + rand() % 206;

				width = 256;
				height = 256;

				std::vector<COLOR3> img((size_t)width * (size_t)height, rndColor);

				newMiptex = map->add_texture(textureName2, (unsigned char*)&img[0], width, height);

				mapRenderer->reuploadTextures();
				mapRenderer->preRenderFaces();
			}

			if (validTexture)
			{
				std::set<int> modelRefreshes;
				for (size_t i = 0; i < app->pickInfo.selectedFaces.size(); i++)
				{
					int faceIdx = app->pickInfo.selectedFaces[i];
					BSPTEXTUREINFO* texinfo = map->get_unique_texinfo(faceIdx);
					texinfo->iMiptex = newMiptex;
					int modelIdx = map->get_model_from_face(faceIdx);
					if (modelIdx >= 0)
						modelRefreshes.insert(modelIdx);
					mapRenderer->updateFaceUVs(faceIdx);
				}

				for (int m : modelRefreshes)
				{
					mapRenderer->refreshModel(m);
				}

				memcpy(textureName, textureName2, MAXTEXTURENAME);
				miptex = newMiptex;

				map->resize_all_lightmaps(true);
				mapRenderer->reloadLightmapsSync();
				for (size_t i = 0; i < app->pickInfo.selectedFaces.size(); i++)
				{
					mapRenderer->highlightFace((int)app->pickInfo.selectedFaces[i], 1);
				}
				mapRenderer->pushUndoState("Change Face Texture", EDIT_MODEL_LUMPS);

				textureId = (ImTextureID)(size_t)mapRenderer->getFaceTextureId((int)app->pickInfo.selectedFaces[0]);

				int texOffset = ((int*)map->textures)[miptex + 1];
				if (texOffset >= 0)
				{
					BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
					width = tex.nWidth;
					height = tex.nHeight;
				}

				pickCount++;
				vertPickCount++;
			}
		}

		ImGui::SameLine();
		ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0893)), width, height).c_str());

		ImVec2 imgSize = ImVec2(inputWidth * 2 - 2, inputWidth * 2 - 2);
		if (ImGui::ImageButton("##show_texbrowser", textureId, imgSize, ImVec2(0, 0), ImVec2(1, 1)))
		{
			showTextureBrowser = true;
		}

		ImGui::PushItemWidth(inputWidth);

		if (app->pickInfo.selectedFaces.size() == 1)
		{
			ImGui::Separator();
			bool styleEdited = false;
			if (ImGui::DragInt("# 1:", &tmpStyles[0], 1, 0, 255))
				styleEdited = true;
			ImGui::SameLine();
			if (ImGui::DragInt("# 2:", &tmpStyles[1], 1, 0, 255))
				styleEdited = true;
			if (ImGui::DragInt("# 3:", &tmpStyles[2], 1, 0, 255))
				styleEdited = true;
			ImGui::SameLine();
			if (ImGui::DragInt("# 4:", &tmpStyles[3], 1, 0, 255))
				styleEdited = true;

			if (styleEdited || (ImGui::IsItemDeactivatedAfterEdit() && stylesChanged))
			{
				int faceIdx = (int)app->pickInfo.selectedFaces[0];
				BSPFACE32& face = map->faces[faceIdx];
				for (int n = 0; n < MAX_LIGHTMAPS; n++)
				{
					face.nStyles[n] = (unsigned char)tmpStyles[n];
				}
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					mapRenderer->pushUndoState("Edit Face Styles", EDIT_MODEL_LUMPS);
					stylesChanged = false;
					pickCount++;
					vertPickCount++;
				}
				else
				{
					stylesChanged = true;
				}
			}

			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0885).c_str());
			ImGui::SameLine();
			ImGui::TextDisabled(get_localized_string(LANG_0886).c_str());

			std::string tmplabel = "##unklabel";

			int edgeIdx = 0;
			for (auto& v : edgeVerts)
			{
				edgeIdx++;
				tmplabel = fmt::format(fmt::runtime(get_localized_string(LANG_0423)), edgeIdx);
				if (ImGui::DragFloat(tmplabel.c_str(), &v.x, 0.1f, 0, 0, "T1: %.3f"))
				{
					updatedFaceVec = true;
				}

				tmplabel = fmt::format(fmt::runtime(get_localized_string(LANG_0424)), edgeIdx);
				ImGui::SameLine();
				if (ImGui::DragFloat(tmplabel.c_str(), &v.y, 0.1f, 0, 0, "T2: %.3f"))
				{
					updatedFaceVec = true;
				}

				tmplabel = fmt::format(fmt::runtime(get_localized_string(LANG_0425)), edgeIdx);
				ImGui::SameLine();
				if (ImGui::DragFloat(tmplabel.c_str(), &v.z, 0.1f, 0, 0, "T3: %.3f"))
				{
					updatedFaceVec = true;
				}
			}

			if (ImGui::Button("COPY VERTS"))
			{
				std::string outstr = "";
				for (auto& s : edgeVerts)
				{
					outstr += s.toKeyvalueString() + "\n";
				}
				ImGui::SetClipboardText(outstr.c_str());
			}

			ImGui::SameLine();

			if (ImGui::Button("APPLY VERTS"))
			{
				int faceIdx = (int)app->pickInfo.selectedFaces[0];
				BSPFACE32& face = map->faces[faceIdx];
				int vecId = 0;
				for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges && vecId < (int)edgeVerts.size(); e++, vecId++)
				{
					int surfEdgeVal = map->surfedges[e];
					BSPEDGE32 edge = map->edges[abs(surfEdgeVal)];
					vec3& v = surfEdgeVal > 0 ? map->verts[edge.iVertex[0]] : map->verts[edge.iVertex[1]];
					v = edgeVerts[vecId];
				}

				int modelIdx = map->get_model_from_face(faceIdx);
				if (modelIdx >= 0)
					mapRenderer->refreshModel(modelIdx);

				mapRenderer->updateFaceUVs(faceIdx);
				map->resize_all_lightmaps(true);
				mapRenderer->reloadLightmapsSync();
				mapRenderer->highlightFace(faceIdx, 1);

				unsigned int targetLumps = FL_PLANES | FL_TEXTURES | FL_VERTICES | FL_NODES | FL_TEXINFO | FL_FACES | FL_LIGHTING | FL_CLIPNODES | FL_LEAVES | FL_EDGES | FL_SURFEDGES | FL_MODELS;
				map->getBspRender()->pushUndoState("Edit Face Vertices", targetLumps);

				updatedFaceVec = false;
				pickCount++;
				vertPickCount++;
			}

			ImGui::SameLine();

			if (ImGui::Button("RESET VERTS"))
			{
				edgeVerts.clear();
				int faceIdx = (int)app->pickInfo.selectedFaces[0];
				BSPFACE32& face = map->faces[faceIdx];
				for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++)
				{
					int surfEdgeVal = map->surfedges[e];
					BSPEDGE32 edge = map->edges[abs(surfEdgeVal)];
					vec3 v = surfEdgeVal > 0 ? map->verts[edge.iVertex[0]] : map->verts[edge.iVertex[1]];
					edgeVerts.push_back(v);
				}
				updatedFaceVec = false;
			}

			ImGui::Text("Lightmap offs: %X", map->faces[app->pickInfo.selectedFaces[0]].nLightmapOffset);
		}

		ImGui::PopItemWidth();
		reloadLimits();
		checkFaceErrors();
	}
	else
	{
		Bsp* map = app->getSelectedMap();
		if (!map || app->pickMode == PICK_OBJECT)
		{
			ImGui::Text(get_localized_string(LANG_1130).c_str());
			ImGui::End();
			return;
		}
		BspRenderer* mapRenderer = map->getBspRender();
		if (!mapRenderer || !mapRenderer->texturesLoaded)
		{
			ImGui::Text(get_localized_string(LANG_0871).c_str());
			ImGui::End();
			return;
		}

		static int last_leaf = -1;
		static bool new_last_leaf = false;
		static int last_leaf_mdl = 0;
		static std::vector<int> vis_leafs;
		static std::vector<int> invis_leafs;
		static bool leaf_decompress = false;
		static unsigned char* visData = NULL;
		static bool vis_debugger_press = false;
		static std::vector<int> face_leaf_list;
		static std::vector<int> leaf_faces;
		static bool auto_update_leaf = true;
		static std::vector<int> last_faces;

		int rowSize = (((map->leafCount - 1) + 63) & ~63) >> 3;
		if (leaf_decompress && last_leaf != -1 && last_leaf < map->leafCount)
		{
			if (visData)
			{
				delete[] visData;
				visData = NULL;
			}
			visData = new unsigned char[rowSize];
			memset(visData, 0, rowSize);
			if (map->leaves[last_leaf].nVisOffset >= 0)
				DecompressVis(map->visdata + map->leaves[last_leaf].nVisOffset, visData, rowSize, map->leafCount - 1, map->visDataLength - map->leaves[last_leaf].nVisOffset);
			vis_leafs.clear();
			invis_leafs.clear();
			std::vector<int> visLeafs;
			map->modelLeafs(0, visLeafs);

			for (auto l : visLeafs)
			{
				if (l == 0)
					continue;
				if (CHECKVISBIT(visData, l - 1))
				{
					vis_leafs.push_back(l);
				}
				else
				{
					invis_leafs.push_back(l);
				}
			}
		}
		leaf_decompress = false;

		if ((last_leaf != mapRenderer->curLeafIdx && auto_update_leaf) || new_last_leaf)
		{
			if (auto_update_leaf && app->clipnodeRenderHull <= 0)
			{
				if (last_leaf != mapRenderer->curLeafIdx)
				{
					last_leaf = mapRenderer->curLeafIdx;
					leaf_decompress = true;
				}
			}
			if (new_last_leaf)
			{
				leaf_decompress = true;
			}

			if (last_leaf < 0 && last_leaf >= map->leafCount)
			{
				leaf_decompress = false;
			}
			else
			{
				BSPLEAF32& tmpLeaf = map->leaves[last_leaf];

				mapRenderer->leafCube->mins = tmpLeaf.nMins;
				mapRenderer->leafCube->maxs = tmpLeaf.nMaxs;

				g_app->pointEntRenderer->genCubeBuffers(mapRenderer->leafCube);
				std::vector<int> leafNodes;
				map->get_leaf_nodes(last_leaf, leafNodes);

				if (leafNodes.size())
				{
					BSPNODE32 node = map->nodes[leafNodes[0]];

					mapRenderer->nodeCube->mins = node.nMins;
					mapRenderer->nodeCube->maxs = node.nMaxs;

					g_app->pointEntRenderer->genCubeBuffers(mapRenderer->nodeCube);

					// BSPPLANE plane = map->planes[node.iPlane];

					// float d = dotProduct(plane.vNormal, cameraOrigin) - plane.fDist;

					// mapRenderer->nodePlaneCube->mins = node.nMins;
					// mapRenderer->nodePlaneCube->maxs = node.nMaxs;

					// mapRenderer->nodePlaneCube->mins += plane.vNormal * d;
					// mapRenderer->nodePlaneCube->maxs += plane.vNormal * d;

					// g_app->pointEntRenderer->genCubeBuffers(mapRenderer->nodePlaneCube);
				}

				leaf_faces = map->getLeafFaces(last_leaf);
				last_leaf_mdl = map->get_model_from_leaf(last_leaf);
			}
			new_last_leaf = false;
		}

		if (leaf_decompress)
		{
			ImGui::TextUnformatted("Decompressing...");
		}
		else
		{
			ImGuiStyle& style = ImGui::GetStyle();

			if (!app->pickInfo.selectedFaces.empty())
			{
				if (last_faces != app->pickInfo.selectedFaces)
				{
					if (vis_debugger_press)
					{
						vis_debugger_press = false;
						mapRenderer->preRenderFaces();
					}
					face_leaf_list = map->getFaceLeafs((int)app->pickInfo.selectedFaces[0]);
				}

				last_faces = app->pickInfo.selectedFaces;
				ImVec4 errorColor = {1.0, 0.0, 0.0, 1.0};
				ImGui::PushStyleColor(ImGuiCol_Text, errorColor);
				ImGui::TextUnformatted("Faces");

				if (ImGui::Button("DELETE"))
				{
					std::sort(app->pickInfo.selectedFaces.begin(), app->pickInfo.selectedFaces.end());

					while (app->pickInfo.selectedFaces.size())
					{
						map->remove_face((int)app->pickInfo.selectedFaces[app->pickInfo.selectedFaces.size() - 1]);
						app->pickInfo.selectedFaces.pop_back();
					}

					map->save_undo_lightmaps();
					map->resize_all_lightmaps();

					mapRenderer->pushUndoState("DELETE FACES", EDIT_MODEL_LUMPS);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Selected faces now totally removed from map!");
					ImGui::EndTooltip();
				}
				ImGui::SameLine();

				if (ImGui::Button("REMOVE PVS"))
				{
					auto selected_faces = app->pickInfo.selectedFaces;
					std::sort(selected_faces.begin(), selected_faces.end());

					while (selected_faces.size())
					{
						map->leaf_del_face((int)selected_faces[selected_faces.size() - 1], -1);
						selected_faces.pop_back();
					}

					mapRenderer->preRenderFaces();
					mapRenderer->pushUndoState("REMOVE FACES FROM PVS", EDIT_MODEL_LUMPS);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Selected faces will be removed from leaves and make this faces invisible!");
					ImGui::EndTooltip();
				}

				if (ImGui::Button("MAKE VISIBLE ANYWHERE"))
				{
					auto selected_faces = app->pickInfo.selectedFaces;
					std::sort(selected_faces.begin(), selected_faces.end());

					while (selected_faces.size())
					{
						map->leaf_add_face((int)selected_faces[selected_faces.size() - 1], -1);
						selected_faces.pop_back();
					}
					mapRenderer->preRenderFaces();

					map->update_lump_pointers();

					mapRenderer->pushUndoState("MAKE FACES VISIBLE IN ALL LEAFS", FL_LEAVES | FL_MARKSURFACES);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Selected faces will be added to all leaves and make those faces visible from anything!");
					ImGui::EndTooltip();
				}

				if (ImGui::Button(fmt::format("DEL FROM {} LEAF", last_leaf).c_str()))
				{
					auto selected_faces = app->pickInfo.selectedFaces;
					std::sort(selected_faces.begin(), selected_faces.end());
					while (selected_faces.size())
					{
						map->leaf_del_face((int)selected_faces[selected_faces.size() - 1], last_leaf);
						selected_faces.pop_back();
					}

					mapRenderer->preRenderFaces();

					mapRenderer->pushUndoState("MAKE FACES INVISIBLE FOR CURRENT LEAF", FL_LEAVES | FL_MARKSURFACES);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Selected faces will be removed from current leaf for invisibility!");
					ImGui::EndTooltip();
				}

				if (ImGui::Button(fmt::format("ADD TO {} LEAF", last_leaf).c_str()))
				{
					auto selected_faces = app->pickInfo.selectedFaces;
					std::sort(selected_faces.begin(), selected_faces.end());
					while (selected_faces.size())
					{
						map->leaf_add_face((int)selected_faces[selected_faces.size() - 1], last_leaf);
						selected_faces.pop_back();
					}

					mapRenderer->preRenderFaces();

					mapRenderer->pushUndoState("MAKE FACES VISIBLE FOR CURRENT LEAF", FL_LEAVES | FL_MARKSURFACES);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Selected faces will be added to current leaf for visibility!");
					ImGui::EndTooltip();
				}

				ImGui::PopStyleColor();

				ImGui::TextUnformatted("Used in leaves:");
				style.FrameBorderSize = 1.0f;

				ImGui::BeginChild("##faceleaflist", ImVec2(0, 120), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

				ImGuiListClipper leaf_clipper;
				leaf_clipper.Begin((int)face_leaf_list.size());

				while (leaf_clipper.Step())
				{
					for (int line_no = leaf_clipper.DisplayStart; line_no < leaf_clipper.DisplayEnd; line_no++)
					{
						if (ImGui::Selectable(std::to_string(face_leaf_list[line_no]).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
						{
							if (ImGui::IsMouseDoubleClicked(0))
							{
							}
						}
					}
				}

				ImGui::EndChild();
			}

			bool updatedLeafVec = false;

			vec3 mins = vec3();
			vec3 maxs = vec3();

			int vertIdx = 0;

			if (last_faces.size() == 1)
			{
				BSPFACE32 face = map->faces[last_faces[0]];

				BSPPLANE& tmpPlane = map->planes[face.iPlane];
				ImGui::PushItemWidth(105);
				ImGui::TextUnformatted(fmt::format("Plane [{}] side [{}] type [{}]", face.iPlane, face.nPlaneSide, tmpPlane.nType).c_str());

				maxs = tmpPlane.vNormal;
				float dist = tmpPlane.fDist;

				vertIdx++;
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0423)), vertIdx).c_str(), &maxs.x, 0.0f, 0, 0, "X:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0424)), vertIdx).c_str(), &maxs.y, 0.0f, 0, 0, "Y:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx).c_str(), &maxs.z, 0.0f, 0, 0, "Z:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx).c_str(), &dist, 0.0f, 0, 0, "DIST:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				if (updatedLeafVec)
				{
					tmpPlane.vNormal = maxs;
					tmpPlane.fDist = dist;

					/*mapRenderer->nodePlaneCube->mins = { -32,-32,-32 };
					mapRenderer->nodePlaneCube->maxs = { 32, 32, 32 };

					mapRenderer->nodePlaneCube->mins += tmpPlane.vNormal;
					mapRenderer->nodePlaneCube->maxs += tmpPlane.vNormal;

					g_app->pointEntRenderer->genCubeBuffers(mapRenderer->nodePlaneCube);*/
					updatedLeafVec = false;
					mapRenderer->pushUndoState("UPDATE MODEL PLANE MINS/MAXS", FL_NODES);
				}
				ImGui::PopItemWidth();
			}

			ImGui::Separator();

			ImVec4 errorColor = {1.0, 0.0, 0.0, 1.0};
			ImGui::PushStyleColor(ImGuiCol_Text, errorColor);
			ImGui::TextUnformatted("Leaves");
			ImGui::PopStyleColor(1);

			if (ImGui::Checkbox("Auto update", &auto_update_leaf) && auto_update_leaf)
			{
				if (last_leaf >= 0)
				{
					leaf_decompress = true;
				}
			}

			if (!auto_update_leaf)
			{
				ImGui::TextUnformatted("Enter leaf number:");
				int tmp_new_leaf = last_leaf;
				if (ImGui::InputInt("##inputleaf", &tmp_new_leaf, 1, 1))
				{
					if (tmp_new_leaf != last_leaf && last_leaf >= 0 && last_leaf < map->leafCount)
					{
						last_leaf = tmp_new_leaf;
						new_last_leaf = true;
					}
				}
				if (ImGui::Button("GO TO##LEAF"))
				{
					BSPLEAF32& leaf = map->leaves[last_leaf];
					app->goToCoords(getCenter(leaf.nMins, leaf.nMaxs));
				}
			}

			// if (!auto_update_leaf)
			//{
			//	if (ImGui::Button("Update leaf"))
			//	{
			//		if (last_leaf >= 0)
			//		{
			//			leaf_decompress = true;
			//		}
			//	}
			// }

			ImGui::TextUnformatted(fmt::format("Leaf list. Leaf:{}", last_leaf).c_str());
			ImGui::TextUnformatted(fmt::format("Leaf model id:{}", last_leaf_mdl).c_str());

			float flContents = 0.0f;

			if (last_leaf >= 0 && last_leaf < map->leafCount)
			{
				flContents = map->leaves[last_leaf].nContents * 1.0f;

				ImGui::TextUnformatted(fmt::format("Vis offset:{}", map->leaves[last_leaf].nVisOffset).c_str());
				ImGui::TextUnformatted("Contents:");
				ImGui::SameLine();
				ImGui::PushItemWidth(30);
				if (ImGui::DragFloat("##leaf1", &flContents, 0.0f, 0, 0, "%.0f"))
				{
					if (flContents != map->leaves[last_leaf].nContents * 1.0f)
						updatedLeafVec = true;
				}
				ImGui::PopItemWidth();
			}

			if (ImGui::Button(get_localized_string(LANG_0645).c_str()))
			{
				if (!g_app->reloading)
				{
					vis_debugger_press = true;

					std::vector<int> visLeafs;
					map->modelLeafs(0, visLeafs);

					for (int l = 0; l < map->leafCount - 1; l++)
					{
						if (std::find(visLeafs.begin(), visLeafs.end(), l) != visLeafs.end())
						{
							if (l == 0)
								continue;
							if (l == last_leaf || CHECKVISBIT(visData, l - 1))
							{
							}
							else
							{
								auto faceList = map->getLeafFaces(l);
								for (const auto& idx : faceList)
								{
									mapRenderer->highlightFace(idx, 1);
								}
							}
						}
						else
						{
							auto faceList = map->getLeafFaces(l + 1);
							for (const auto& idx : faceList)
							{
								mapRenderer->highlightFace(idx, 3);
							}
						}
					}

					for (auto l : visLeafs)
					{
						if (l == 0)
							continue;
						if (l == last_leaf || CHECKVISBIT(visData, l - 1))
						{
							auto faceList = map->getLeafFaces(l);
							for (const auto& idx : faceList)
							{
								mapRenderer->highlightFace(idx, 2);
							}
						}
					}
				}
			}

			if (ImGui::Button(get_localized_string("LANG_SELECT_LEAF_FACES").c_str()))
			{
				app->pickInfo.selectedFaces.clear();
				mapRenderer->preRenderFaces();
				auto faceList = map->getLeafFaces(last_leaf);
				for (const auto& idx : faceList)
				{
					app->pickInfo.selectedFaces.push_back(idx);
					mapRenderer->highlightFace(idx, 2);
				}
				pickCount++;
			}

			ImGui::BeginChild("##leaffacelist", ImVec2(0, 120), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

			ImGuiListClipper face_clipper;
			face_clipper.Begin((int)leaf_faces.size());

			while (face_clipper.Step())
			{
				for (int line_no = face_clipper.DisplayStart; line_no < face_clipper.DisplayEnd; line_no++)
				{
					if (ImGui::Selectable(std::to_string(leaf_faces[line_no]).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
					{
						if (ImGui::IsMouseDoubleClicked(0))
						{
						}
					}
				}
			}

			ImGui::EndChild();

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.0, 0.0, 1.0, 1.0});
			ImGui::TextUnformatted("Blue is visible.");
			ImGui::PopStyleColor();

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0, 0.0, 0.0, 1.0});
			ImGui::TextUnformatted("Red is invisible.");
			ImGui::PopStyleColor();

			if (last_leaf == 0)
				ImGui::BeginDisabled();
			ImGui::TextUnformatted("Double click for edit");
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Double click for change leaf visibility for current leaf.");
				ImGui::EndTooltip();
			}
			if (last_leaf == 0)
				ImGui::EndDisabled();

			style.FrameBorderSize = 1.0f;

			ImGui::BeginChild("##leaflist", ImVec2(0, 240), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

			ImGuiListClipper clipper;
			clipper.Begin((int)(vis_leafs.size() + invis_leafs.size()));
			bool vis_print = true;

			bool need_compress = false;

			while (clipper.Step())
			{
				for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
				{
					if (vis_print && line_no >= (int)vis_leafs.size())
					{
						vis_print = false;
					}

					if (last_leaf == 0)
						ImGui::BeginDisabled();
					ImGui::PushStyleColor(ImGuiCol_Text, vis_print ? ImVec4{0.0, 0.0, 1.0, 1.0} : ImVec4{1.0, 0.0, 0.0, 1.0});
					if (vis_print)
					{
						if (ImGui::Selectable(std::to_string(vis_leafs[line_no]).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
						{
							if (ImGui::IsMouseDoubleClicked(0))
							{
								int tmpleaf = vis_leafs[line_no];
								vis_leafs.erase(vis_leafs.begin() + line_no);
								invis_leafs.push_back(tmpleaf);
								vis_print = true;
								need_compress = true;
							}
						}
					}
					else
					{
						if (ImGui::Selectable(std::to_string(invis_leafs[line_no - vis_leafs.size()]).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
						{
							if (ImGui::IsMouseDoubleClicked(0))
							{
								int tmpleaf = invis_leafs[line_no - vis_leafs.size()];
								invis_leafs.erase(invis_leafs.begin() + (line_no - vis_leafs.size()));
								vis_leafs.push_back(tmpleaf);
								vis_print = true;
								need_compress = true;
							}
						}
					}
					if (last_leaf == 0)
						ImGui::EndDisabled();
					ImGui::PopStyleColor();
				}
			}
			clipper.End();

			ImGui::EndChild();
			if (last_leaf == 0)
				ImGui::BeginDisabled();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.0, 0.0, 1.0, 1.0});

			if (ImGui::Button("Mark all visible"))
			{
				invis_leafs.clear();
				vis_leafs.clear();

				std::vector<int> visLeafs;
				map->modelLeafs(0, visLeafs);

				for (auto l : visLeafs)
				{
					print_log("{}\n", l);
					vis_leafs.push_back(l);
				}
				need_compress = true;
			}

			ImGui::PopStyleColor();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0, 0.0, 0.0, 1.0});

			if (ImGui::Button("Mark all invisible"))
			{
				invis_leafs.clear();
				vis_leafs.clear();

				std::vector<int> visLeafs;
				map->modelLeafs(0, visLeafs);

				for (auto l : visLeafs)
				{
					print_log("{}\n", l);
					invis_leafs.push_back(l);
				}
				need_compress = true;
			}
			ImGui::PopStyleColor();

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.0, 0.0, 1.0, 1.0});

			if (ImGui::Button("Mark visible for all"))
			{
				unsigned char* tmpVisData = new unsigned char[rowSize];
				unsigned char* tmpCompressed = new unsigned char[g_limits.maxMapLeaves / 8];

				// ADD ONE LEAF TO ALL VISIBILITY BYTES
				for (int i = 1; i < map->leafCount; i++)
				{
					if (map->leaves[i].nVisOffset >= 0)
					{
						memset(tmpVisData, 0, rowSize);
						DecompressVis(map->visdata + map->leaves[i].nVisOffset, tmpVisData, rowSize, map->leafCount - 1, map->visDataLength - map->leaves[i].nVisOffset);

						if (last_leaf > 0)
							SETVISBIT(tmpVisData, last_leaf - 1);

						memset(tmpCompressed, 0, g_limits.maxMapLeaves / 8);
						int size = CompressVis(tmpVisData, rowSize, tmpCompressed, g_limits.maxMapLeaves / 8);

						map->leaves[i].nVisOffset = map->visDataLength;

						unsigned char* newVisLump = new unsigned char[map->visDataLength + size];
						memcpy(newVisLump, map->visdata, map->visDataLength);
						memcpy(newVisLump + map->visDataLength, tmpCompressed, size);
						map->replace_lump(LUMP_VISIBILITY, newVisLump, map->visDataLength + size);
						delete[] newVisLump;
					}
				}

				delete[] tmpCompressed;
				delete[] tmpVisData;

				// repack visdata
				auto removed = map->remove_unused_model_structures(CLEAN_VISDATA);

				if (!removed.allZero())
					removed.print_delete_stats(1);

				mapRenderer->pushUndoState("UPDATE LEAF VISIBILITY", FL_VISIBILITY);
			}

			ImGui::PopStyleColor();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0, 0.0, 0.0, 1.0});

			if (ImGui::Button("Mark invisible for all"))
			{
				unsigned char* tmpVisData = new unsigned char[rowSize];
				unsigned char* tmpCompressed = new unsigned char[g_limits.maxMapLeaves / 8];

				// ADD ONE LEAF TO ALL VISIBILITY BYTES
				for (int i = 1; i < map->leafCount; i++)
				{
					if (map->leaves[i].nVisOffset >= 0)
					{
						memset(tmpVisData, 0, rowSize);
						DecompressVis(map->visdata + map->leaves[i].nVisOffset, tmpVisData, rowSize, map->leafCount - 1, map->visDataLength - map->leaves[i].nVisOffset);

						if (last_leaf > 0)
							CLEARVISBIT(tmpVisData, last_leaf - 1);

						memset(tmpCompressed, 0, g_limits.maxMapLeaves / 8);
						int size = CompressVis(tmpVisData, rowSize, tmpCompressed, g_limits.maxMapLeaves / 8);

						map->leaves[i].nVisOffset = map->visDataLength;

						unsigned char* newVisLump = new unsigned char[map->visDataLength + size];
						memcpy(newVisLump, map->visdata, map->visDataLength);
						memcpy(newVisLump + map->visDataLength, tmpCompressed, size);
						map->replace_lump(LUMP_VISIBILITY, newVisLump, map->visDataLength + size);
						delete[] newVisLump;
					}
				}

				delete[] tmpCompressed;
				delete[] tmpVisData;

				// repack visdata
				auto removed = map->remove_unused_model_structures(CLEAN_VISDATA);

				if (!removed.allZero())
					removed.print_delete_stats(1);

				mapRenderer->pushUndoState("UPDATE LEAF VISIBILITY", FL_VISIBILITY);
			}
			ImGui::PopStyleColor();

			if (last_leaf == 0)
				ImGui::EndDisabled();

			BSPLEAF32& tmpLeaf = map->leaves[last_leaf];
			mins = tmpLeaf.nMins;
			maxs = tmpLeaf.nMaxs;

			ImGui::TextUnformatted("Leaf mins/maxs");
			ImGui::PushItemWidth(105);
			vertIdx++;
			if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0423)), vertIdx).c_str(), &mins.x, 0.0f, 0, 0, "X1:%.2f"))
			{
				if (mins != tmpLeaf.nMins)
					updatedLeafVec = true;
			}

			vertIdx++;
			ImGui::SameLine();
			if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0424)), vertIdx).c_str(), &mins.y, 0.0f, 0, 0, "Y1:%.2f"))
			{
				if (mins != tmpLeaf.nMins)
					updatedLeafVec = true;
			}

			vertIdx++;
			ImGui::SameLine();
			if (ImGui::DragFloat((fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx)).c_str(), &mins.z, 0.0f, 0, 0, "Z1:%.2f"))
			{
				if (mins != tmpLeaf.nMins)
					updatedLeafVec = true;
			}

			vertIdx++;
			if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0423)), vertIdx).c_str(), &maxs.x, 0.0f, 0, 0, "X2:%.2f"))
			{
				if (maxs != tmpLeaf.nMaxs)
					updatedLeafVec = true;
			}

			vertIdx++;
			ImGui::SameLine();
			if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0424)), vertIdx).c_str(), &maxs.y, 0.0f, 0, 0, "Y2:%.2f"))
			{
				if (maxs != tmpLeaf.nMaxs)
					updatedLeafVec = true;
			}

			vertIdx++;
			ImGui::SameLine();
			if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx).c_str(), &maxs.z, 0.0f, 0, 0, "Z2:%.2f"))
			{
				if (maxs != tmpLeaf.nMaxs)
					updatedLeafVec = true;
			}
			vertIdx++;
			ImGui::PopItemWidth();
			if (updatedLeafVec)
			{
				tmpLeaf.nMins = mins;
				tmpLeaf.nMaxs = maxs;

				mapRenderer->leafCube->mins = tmpLeaf.nMins;
				mapRenderer->leafCube->maxs = tmpLeaf.nMaxs;

				map->leaves[last_leaf].nContents = (int)std::round(flContents);

				g_app->pointEntRenderer->genCubeBuffers(mapRenderer->leafCube);
				updatedLeafVec = false;

				mapRenderer->pushUndoState("EDIT LEAF", FL_LEAVES);
			}

			std::vector<int> leafNodes{};
			map->get_leaf_nodes(last_leaf, leafNodes);

			if (leafNodes.size())
			{
				int nodeIdx = leafNodes[0];
				BSPNODE32& tmpNode = map->nodes[nodeIdx];

				mins = tmpNode.nMins;
				maxs = tmpNode.nMaxs;

				if (ImGui::Button("Same as leaf"))
				{
					mins = tmpLeaf.nMins;
					maxs = tmpLeaf.nMaxs;
					updatedLeafVec = true;
				}

				ImGui::TextUnformatted(fmt::format("Leaf node [{}] mins/maxs", nodeIdx).c_str());

				ImGui::PushItemWidth(105);
				vertIdx++;
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0423)), vertIdx).c_str(), &mins.x, 0.0f, 0, 0, "X1:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0424)), vertIdx).c_str(), &mins.y, 0.0f, 0, 0, "Y1:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx).c_str(), &mins.z, 0.0f, 0, 0, "Z1:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0423)), vertIdx).c_str(), &maxs.x, 0.0f, 0, 0, "X2:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0424)), vertIdx).c_str(), &maxs.y, 0.0f, 0, 0, "Y2:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx).c_str(), &maxs.z, 0.0f, 0, 0, "Z2:%.2f"))
				{
					updatedLeafVec = true;
				}
				vertIdx++;
				ImGui::PopItemWidth();
				if (updatedLeafVec)
				{
					tmpNode.nMins = mins;
					tmpNode.nMaxs = maxs;

					mapRenderer->nodeCube->mins = tmpNode.nMins;
					mapRenderer->nodeCube->maxs = tmpNode.nMaxs;

					g_app->pointEntRenderer->genCubeBuffers(mapRenderer->nodeCube);
					updatedLeafVec = false;
					mapRenderer->pushUndoState("UPDATE LEAF NODE MINS/MAXS", FL_NODES);
				}
			}

			if (leafNodes.size())
			{
				int nodeIdx = leafNodes[0];
				BSPNODE32& tmpNode = map->nodes[nodeIdx];

				ImGui::PushItemWidth(105);
				ImGui::TextUnformatted(fmt::format("Node [{}] plane [{}]", nodeIdx, tmpNode.iPlane).c_str());

				BSPPLANE& tmpPlane = map->planes[tmpNode.iPlane];
				maxs = tmpPlane.vNormal;
				float dist = tmpPlane.fDist;

				vertIdx++;
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0423)), vertIdx).c_str(), &maxs.x, 0.0f, 0, 0, "X:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0424)), vertIdx).c_str(), &maxs.y, 0.0f, 0, 0, "Y:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx).c_str(), &maxs.z, 0.0f, 0, 0, "Z:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx).c_str(), &dist, 0.0f, 0, 0, "DIST:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				if (updatedLeafVec)
				{
					tmpPlane.vNormal = maxs;
					tmpPlane.fDist = dist;

					/*mapRenderer->nodePlaneCube->mins = { -32,-32,-32 };
					mapRenderer->nodePlaneCube->maxs = { 32, 32, 32 };

					mapRenderer->nodePlaneCube->mins += tmpPlane.vNormal;
					mapRenderer->nodePlaneCube->maxs += tmpPlane.vNormal;

					g_app->pointEntRenderer->genCubeBuffers(mapRenderer->nodePlaneCube);*/
					updatedLeafVec = false;
					mapRenderer->pushUndoState("UPDATE LEAF NODE MINS/MAXS", FL_NODES);
				}
				ImGui::PopItemWidth();
			}

			if (ImGui::Button("Create duplicate"))
			{
				last_leaf = map->clone_world_leaf(last_leaf);
				BSPLEAF32& leaf = map->leaves[last_leaf];
				app->goToCoords(getCenter(leaf.nMins, leaf.nMaxs));
				mapRenderer->pushUndoState("DUPLICATE LEAF", FL_LEAVES | FL_NODES | FL_PLANES | FL_MARKSURFACES | FL_VISIBILITY);
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Create new leaf with same settings.");
				ImGui::TextUnformatted("(BUT NOT WORKING!)");
				ImGui::EndTooltip();
			}

			if (need_compress)
			{
				leaf_decompress = true;
				memset(visData, 0, rowSize);

				for (auto sel : vis_leafs)
				{
					SETVISBIT(visData, sel - 1);
				}

				// for (auto unsel : invis_leafs)
				//{
				//	CLEARVISBIT(visData, unsel - 1);
				// }

				unsigned char* compressed = new unsigned char[g_limits.maxMapLeaves * 8];
				memset(compressed, 0, g_limits.maxMapLeaves / 8);
				int size = CompressVis(visData, rowSize, compressed, g_limits.maxMapLeaves / 8);

				map->leaves[last_leaf].nVisOffset = map->visDataLength;
				unsigned char* newVisLump = new unsigned char[map->visDataLength + size];
				memcpy(newVisLump, map->visdata, map->visDataLength);
				memcpy(newVisLump + map->visDataLength, compressed, size);
				map->replace_lump(LUMP_VISIBILITY, newVisLump, map->visDataLength + size);
				delete[] newVisLump;

				delete[] compressed;

				auto removed = map->remove_unused_model_structures(CLEAN_VISDATA);

				if (!removed.allZero())
					removed.print_delete_stats(1);

				mapRenderer->pushUndoState("UPDATE VIS LUMP", FL_LEAVES | FL_MARKSURFACES);
			}
		}
	}

	if (g_app->curLeftMouse != GLFW_RELEASE || g_app->oldLeftMouse != GLFW_RELEASE)
	{
		scroll_x = ImGui::GetScrollX();
		scroll_y = ImGui::GetScrollY();
	}
	ImGui::End();
}

StatInfo Gui::calcStat(std::string name, unsigned int val, unsigned int max, bool isMem)
{
	StatInfo stat;
	const float meg = 1024 * 1024;
	float percent = max != 0 ? (val / (float)max) * 100 : 0;

	ImVec4 color;

	if (percent >= 100)
	{
		color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
	}
	else if (percent >= 90)
	{
		color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
	}
	else if (percent >= 75)
	{
		color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
	}
	else
	{
		color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	std::string tmp;
	// std::string out;

	stat.name = std::move(name);

	if (isMem)
	{
		tmp = fmt::format("{:>8.2f}", val / meg);
		stat.val = std::string(tmp);

		tmp = fmt::format("{:>8.2f}", max / meg);
		stat.max = std::string(tmp);
	}
	else
	{
		tmp = fmt::format("{:>8}", val);
		stat.val = std::string(tmp);

		tmp = fmt::format("{:>8}", max);
		stat.max = std::string(tmp);
	}
	tmp = fmt::format("{:3.1f}%", percent);
	stat.fullness = std::string(tmp);
	stat.color = color;

	stat.progress = max != 0 ? (float)val / (float)max : 0;

	return stat;
}

ModelInfo Gui::calcModelStat(Bsp* map, STRUCTUSAGE* modelInfo, unsigned int val, unsigned int max, bool isMem)
{
	ModelInfo stat;

	std::string classname = modelInfo->modelIdx == 0 ? "worldspawn" : "???";
	std::string targetname = modelInfo->modelIdx == 0 ? "" : "???";
	for (size_t k = 0; k < map->ents.size(); k++)
	{
		if (map->ents[k]->getBspModelIdx() == modelInfo->modelIdx)
		{
			targetname = map->ents[k]->keyvalues["targetname"];
			classname = map->ents[k]->keyvalues["classname"];
			stat.entIdx = (int)k;
		}
	}

	stat.classname = std::move(classname);
	stat.targetname = std::move(targetname);

	std::string tmp;

	const float meg = 1024 * 1024;
	float percent = (val / (float)max) * 100;

	if (isMem)
	{
		tmp = fmt::format("{:8.1f}", val / meg);
		stat.val = std::to_string(val);

		tmp = fmt::format("{:>5.1f}", max / meg);
		stat.usage = tmp;
	}
	else
	{
		stat.model = "*" + std::to_string(modelInfo->modelIdx);
		stat.val = std::to_string(val);
	}
	if (percent >= 0.1f)
	{
		tmp = fmt::format("{:6.1f}%", percent);
		stat.usage = std::string(tmp);
	}

	return stat;
}
