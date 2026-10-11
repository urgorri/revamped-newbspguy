#include "XASH_csm.h"
#include "util.h"
#include "log.h"
#include "forcecrc32.h"
#include "Settings.h"
#include "Renderer.h"

#include <fstream>
#include <sstream>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

// Helper: parse materials string containing quoted names
void CSMFile::parseMaterialsFromString(const std::string& materialsStr)
{
	std::istringstream ss(materialsStr);
	std::string material;
	while (ss.str().find('"', (size_t)ss.tellg()) != std::string::npos && ss >> std::quoted(material))
	{
		materials.push_back(material);
	}
}

std::string CSMFile::getStringFromMaterials()
{
	std::string ret{};
	for (auto& m : materials)
	{
		ret += "\"" + m + "\" ";
	}
	if (!ret.empty())
		ret.pop_back();
	return ret;
}

CSMFile::CSMFile()
{
	readed = false;
	for (auto& m : model)
		delete m;
	model.clear();
	for (auto& s : sideModel)
		delete s;
	sideModel.clear();
}

CSMFile::CSMFile(std::string path)
{
	csmFilePath = path;
	readed = read(path);
	for (auto& m : model)
		delete m;
	model.clear();
	for (auto& s : sideModel)
		delete s;
	sideModel.clear();
}

CSMFile::~CSMFile()
{
	for (auto& tex : mat_textures)
	{
		delete tex;
	}
	mat_textures.clear();
	for (auto& m : model)
		delete m;
	model.clear();
	for (auto& s : sideModel)
		delete s;
	sideModel.clear();
}

// Load texture helper (searches paths and loads BMP)
Texture* loadBMPTexture(const std::string& fileName)
{
	int width = 0, height = 0;
	unsigned char* indexes = nullptr;
	unsigned char* rgbData = nullptr;
	COLOR3 palette[256];

	if (ReadBMP_PAL(fileName, &indexes, width, height, palette))
	{
		print_log(PRINT_GREEN, "Loaded 8-bit BMP: {} ({}x{})\n", basename(fileName), width, height);
		rgbData = new unsigned char[width * height * 3];
		for (int i = 0; i < width * height; i++)
		{
			COLOR3 color = palette[indexes[i]];
			rgbData[i * 3] = color.b;
			rgbData[i * 3 + 1] = color.g;
			rgbData[i * 3 + 2] = color.r;
		}
		delete[] indexes;
		Texture* tex = new Texture(width, height, rgbData, basename(fileName), false, true);
		tex->upload(Texture::TYPE_TEXTURE);
		return tex;
	}
	if (indexes)
		delete[] indexes;

	if (ReadBMP_RGB(fileName, &rgbData, width, height))
	{
		print_log(PRINT_GREEN, "Loaded RGB BMP: {} ({}x{})\n", basename(fileName), width, height);
		Texture* tex = new Texture(width, height, rgbData, basename(fileName), false, true);
		tex->upload(Texture::TYPE_TEXTURE);
		return tex;
	}

	return missingTex;
}

// Load texture for a material name. Returns missingTex if not found.
Texture* CSMFile::loadTextureForMaterial(const std::string& materialName)
{
	if (materialName.empty())
	{
		return whiteTex;
	}
	if (materialName == "@null")
	{
		return clipTex_rgba;
	}
	if (materialName == "@skip")
	{
		return skyTex_rgba;
	}
	std::vector<std::string> searchPaths;
	searchPaths.push_back(".");

	if (!csmFilePath.empty())
	{
		std::string csmDir = stripFileName(csmFilePath);
		if (!csmDir.empty() && csmDir != ".")
			searchPaths.push_back(csmDir);
	}

	if (header.version == 3 && header.pathes[0] != '\0')
	{
		std::string pathsStr = header.pathes;
		std::vector<std::string> paths = splitString(pathsStr, ";");
		for (const auto& path : paths)
		{
			std::string trimmed = trimSpaces(path);
			if (!trimmed.empty())
				searchPaths.push_back(trimmed);
		}
	}

	std::string textureName = materialName;
	if (!ends_with(toLowerCase(textureName), ".bmp"))
		textureName += ".bmp";

	for (const auto& searchPath : searchPaths)
	{
		std::string texturePath = (searchPath == ".") ? textureName : (searchPath + "/" + textureName);
		if (fileExists(texturePath))
		{
			print_log(PRINT_BLUE, "Found texture at: {}\n", texturePath);
			Texture* tex = loadBMPTexture(texturePath);
			if (tex)
				return tex;
		}

		if (dirExists(searchPath))
		{
			try
			{
				for (const auto& entry : fs::directory_iterator(searchPath))
				{
					if (entry.is_directory())
					{
						std::string subdirPath = entry.path().string();
						std::string testPath = subdirPath + "/" + textureName;
						if (fileExists(testPath))
						{
							print_log(PRINT_BLUE, "Found texture in subdirectory: {}\n", testPath);
							Texture* tex = loadBMPTexture(testPath);
							if (tex)
								return tex;
						}
					}
				}
			}
			catch (...)
			{ /* ignore filesystem errors */
			}
		}
	}

	std::string foundPath;
	if (FindPathInAssets(NULL, textureName, foundPath, true))
	{
		print_log(PRINT_BLUE, "Found texture via FindPathInAssets: {}\n", foundPath);
		Texture* tex = loadBMPTexture(foundPath);
		if (tex)
			return tex;
	}

	print_log(PRINT_GREEN, "Texture not found for material: {}\n", materialName);
	return missingTex;
}

void CSMFile::loadTextures()
{
	for (auto& tex : mat_textures)
		delete tex;
	mat_textures.clear();

	for (const auto& material : materials)
	{
		Texture* tex = loadTextureForMaterial(material);
		mat_textures.push_back(tex);
	}

	print_log(PRINT_BLUE, "Loaded {} textures for CSM model\n", mat_textures.size());
}

bool CSMFile::validate()
{
	if (header.face_size != sizeof(csm_face))
	{
		print_log(PRINT_RED, "Error: Invalid CSM header face size {}!\n", header.face_size);
		return false;
	}
	if (header.vertex_size != sizeof(csm_vertex))
	{
		print_log(PRINT_RED, "Error: Invalid CSM header vertex size {}!\n", header.vertex_size);
		return false;
	}

	if (faces.empty() || vertices.empty())
	{
		print_log(PRINT_RED, "Error: Empty CSM structure!\n");
		return false;
	}

	for (auto& f : faces)
	{
		if (f.material >= materials.size())
		{
			print_log(PRINT_RED, "Error: Invalid material {} in face! Materials count: {}\n",
					  f.material, materials.size());
			return false;
		}

		for (int i = 0; i < 3; i++)
		{
			if (f.index[i] >= vertices.size())
			{
				print_log(PRINT_RED, "Error: Invalid index[{}] in face = {}! Vertices: {}\n",
						  i, f.index[i], vertices.size());
				return false;
			}
		}
	}

	if (header.version == 3 && header.sides_count > 0)
	{
		for (auto& s : sides)
		{
			if (s.material >= materials.size())
			{
				print_log(PRINT_RED, "Error: Invalid material in side!\n");
				return false;
			}
			if (s.firstpoint + s.numpoints > points.size())
			{
				print_log(PRINT_RED, "Error: Invalid point indices in side!\n");
				return false;
			}
		}
	}

	return true;
}

// Read CSM v3 header and blocks
bool CSMFile::read_v3(std::ifstream& file)
{
	file.seekg(0, std::ios::beg);
	if (!file.read(reinterpret_cast<char*>(&header), sizeof(header)))
		return false;

	// materials
	if (header.mat_size > 0)
	{
		file.seekg(header.mat_ofs);
		std::string matstr;
		matstr.resize(header.mat_size);
		file.read((char*)matstr.data(), header.mat_size);
		if (!matstr.empty())
			parseMaterialsFromString(matstr);
	}

	// vertices
	if (header.vertex_count > 0)
	{
		file.seekg(header.vertex_ofs);
		vertices.resize(header.vertex_count);
		size_t expected = sizeof(csm_vertex) * header.vertex_count;
		file.read((char*)vertices.data(), expected);
	}

	// faces
	if (header.faces_count > 0)
	{
		file.seekg(header.faces_ofs);
		faces.resize(header.faces_count);
		size_t expected = sizeof(csm_face) * header.faces_count;
		file.read((char*)faces.data(), expected);
	}

	// sides (v3)
	if (header.sides_count > 0)
	{
		file.seekg(header.sides_ofs);
		sides.resize(header.sides_count);
		file.read((char*)sides.data(), (size_t)header.side_size * header.sides_count);
	}

	// points (v3)
	if (header.points_count > 0)
	{
		file.seekg(header.points_ofs);
		points.resize(header.points_count);
		file.read((char*)points.data(), (size_t)header.point_size * header.points_count);
	}

	return true;
}

// Read CSM v2 by mapping fields directly into v3 structures without creating v2 structs.
bool CSMFile::read_v2(std::ifstream& file)
{
	file.seekg(0, std::ios::beg);

	// Read primitive fields of v2 header sequentially.
	unsigned int ident = 0;
	unsigned int version = 0;
	unsigned int flags = 0;
	unsigned int lmGroups = 0;
	unsigned int reserved0[4] = {0};
	vec3 model_mins;
	vec3 model_maxs;
	unsigned int reserved1[4] = {0};

	unsigned int mat_ofs = 0;
	unsigned int mat_size = 0;
	unsigned int faces_ofs = 0;
	unsigned int face_size = 0;
	unsigned int faces_count = 0;
	unsigned int vertex_ofs = 0;
	unsigned int vertex_size = 0;
	unsigned int vertex_count = 0;

	// Read fields in the same order as original v2 header layout.
	if (!file.read(reinterpret_cast<char*>(&ident), sizeof(ident)))
		return false;
	if (!file.read(reinterpret_cast<char*>(&version), sizeof(version)))
		return false;
	if (!file.read(reinterpret_cast<char*>(&flags), sizeof(flags)))
		return false;
	if (!file.read(reinterpret_cast<char*>(&lmGroups), sizeof(lmGroups)))
		return false;
	if (!file.read(reinterpret_cast<char*>(reserved0), sizeof(reserved0)))
		return false;
	if (!file.read(reinterpret_cast<char*>(&model_mins), sizeof(model_mins)))
		return false;
	if (!file.read(reinterpret_cast<char*>(&model_maxs), sizeof(model_maxs)))
		return false;
	if (!file.read(reinterpret_cast<char*>(reserved1), sizeof(reserved1)))
		return false;

	if (!file.read(reinterpret_cast<char*>(&mat_ofs), sizeof(mat_ofs)))
		return false;
	if (!file.read(reinterpret_cast<char*>(&mat_size), sizeof(mat_size)))
		return false;

	if (!file.read(reinterpret_cast<char*>(&faces_ofs), sizeof(faces_ofs)))
		return false;
	if (!file.read(reinterpret_cast<char*>(&face_size), sizeof(face_size)))
		return false;
	if (!file.read(reinterpret_cast<char*>(&faces_count), sizeof(faces_count)))
		return false;

	if (!file.read(reinterpret_cast<char*>(&vertex_ofs), sizeof(vertex_ofs)))
		return false;
	if (!file.read(reinterpret_cast<char*>(&vertex_size), sizeof(vertex_size)))
		return false;
	if (!file.read(reinterpret_cast<char*>(&vertex_count), sizeof(vertex_count)))
		return false;

	// Map into v3 header
	header.ident = ident;
	header.version = IDCSM_VERSION; // treat as v3 internally
	header.header_size = sizeof(csm_header);
	header.flags = flags;
	memset(header.pathes, 0, sizeof(header.pathes));
	header.lmGroups = lmGroups;
	header.dtGroups = 0;
	header.model_mins = model_mins;
	header.model_maxs = model_maxs;

	header.mat_ofs = mat_ofs;
	header.mat_size = mat_size;

	header.faces_ofs = faces_ofs;
	header.face_size = sizeof(csm_face);
	header.faces_count = faces_count;

	header.vertex_ofs = vertex_ofs;
	header.vertex_size = sizeof(csm_vertex);
	header.vertex_count = vertex_count;

	header.sides_ofs = 0;
	header.side_size = sizeof(csm_side);
	header.sides_count = 0;
	header.points_ofs = 0;
	header.point_size = sizeof(vec3);
	header.points_count = 0;

	// Read materials (string table)
	if (header.mat_size > 0)
	{
		file.seekg(header.mat_ofs);
		std::string matstr;
		matstr.resize(header.mat_size);
		file.read((char*)matstr.data(), header.mat_size);
		if (!matstr.empty())
			parseMaterialsFromString(matstr);
	}

	// Read vertices: v2 vertex layout matches v3 csm_vertex in this codebase.
	if (header.vertex_count > 0)
	{
		file.seekg(header.vertex_ofs);
		vertices.resize(header.vertex_count);
		size_t expected = sizeof(csm_vertex) * header.vertex_count;
		file.read((char*)vertices.data(), expected);
	}

	// Read faces: v2 face fields are read sequentially and mapped into csm_face
	if (header.faces_count > 0)
	{
		file.seekg(header.faces_ofs);
		faces.resize(header.faces_count);

		for (unsigned int i = 0; i < header.faces_count; ++i)
		{
			// Read v2 face primitives directly
			unsigned short matIdx = 0;
			unsigned short edgeFlags = 0;
			unsigned int vertIdx[3] = {0, 0, 0};
			int lmGroup = -1;
			// v2 had uvs[2] each containing vec2 uv[3]
			struct
			{
				vec2 uv[3];
			} uvs[2];

			if (!file.read(reinterpret_cast<char*>(&matIdx), sizeof(matIdx)))
				return false;
			if (!file.read(reinterpret_cast<char*>(&edgeFlags), sizeof(edgeFlags)))
				return false;
			if (!file.read(reinterpret_cast<char*>(vertIdx), sizeof(vertIdx)))
				return false;
			if (!file.read(reinterpret_cast<char*>(&lmGroup), sizeof(lmGroup)))
				return false;
			if (!file.read(reinterpret_cast<char*>(&uvs), sizeof(uvs)))
				return false;

			// Map into v3 face
			csm_face face;
			face.material = matIdx;
			face.flags = edgeFlags;
			face.index[0] = vertIdx[0];
			face.index[1] = vertIdx[1];
			face.index[2] = vertIdx[2];
			face.lmGroup = lmGroup;
			face.dtGroup = -1;
			// copy uvs into tc
			for (int t = 0; t < 2; ++t)
			{
				for (int v = 0; v < 3; ++v)
				{
					face.tc[t].uv[v] = uvs[t].uv[v];
				}
			}
			faces[i] = face;
		}
	}

	return true;
}

bool CSMFile::read(const std::string& filePath)
{
	std::ifstream file(filePath, std::ios::binary);
	if (!file)
	{
		print_log(PRINT_RED, "Error: Failed to open file for reading: {}\n", filePath);
		return false;
	}

	unsigned int ident = 0;
	if (!file.read(reinterpret_cast<char*>(&ident), sizeof(ident)))
	{
		print_log(PRINT_RED, "Error: Failed to read ident\n");
		return false;
	}

	if (ident != IDCSMMODHEADER)
	{
		print_log(PRINT_RED, "Error: Invalid CSM ident: 0x{:X}\n", ident);
		return false;
	}

	unsigned int version = 0;
	if (!file.read(reinterpret_cast<char*>(&version), sizeof(version)))
	{
		print_log(PRINT_RED, "Error: Failed to read version\n");
		return false;
	}

	// Re-open or reposition to start for full parsing
	file.clear();
	file.seekg(0, std::ios::beg);

	bool result = false;
	if (version == 3)
	{
		result = read_v3(file);
	}
	else if (version == 2)
	{
		// Map v2 into v3 in-place without creating v2 C++ structs
		result = read_v2(file);
	}
	else
	{
		print_log(PRINT_RED, "Error: Unsupported CSM version {}: {}\n", version, filePath);
		return false;
	}

	if (result)
	{
		loadTextures();
		readed = true;
	}

	return result;
}

bool CSMFile::write(const std::string& filePath)
{
	std::ofstream file(filePath, std::ios::binary);
	if (!file)
	{
		print_log(PRINT_RED, "Error: Failed to open file for writing: {}\n", filePath);
		return false;
	}

	// Always write v3 format
	header.ident = IDCSMMODHEADER;
	header.version = IDCSM_VERSION;
	header.header_size = sizeof(csm_header);

	header.vertex_count = static_cast<unsigned int>(vertices.size());
	header.faces_count = static_cast<unsigned int>(faces.size());
	header.sides_count = static_cast<unsigned int>(sides.size());
	header.points_count = static_cast<unsigned int>(points.size());

	std::string matstr = getStringFromMaterials();
	header.mat_size = static_cast<unsigned int>(matstr.size() + 1);
	header.vertex_size = sizeof(csm_vertex);
	header.face_size = sizeof(csm_face);
	header.side_size = sizeof(csm_side);
	header.point_size = sizeof(vec3);

	header.mat_ofs = sizeof(csm_header);
	header.vertex_ofs = header.mat_ofs + header.mat_size;
	header.faces_ofs = header.vertex_ofs + (header.vertex_size * header.vertex_count);
	header.sides_ofs = header.faces_ofs + (header.face_size * header.faces_count);
	header.points_ofs = header.sides_ofs + (header.side_size * header.sides_count);

	file.write(reinterpret_cast<const char*>(&header), sizeof(header));

	// write materials (null-terminated)
	matstr.push_back('\0');
	file.write(matstr.data(), header.mat_size);

	// write vertices
	if (!vertices.empty())
		file.write(reinterpret_cast<const char*>(vertices.data()), (size_t)header.vertex_size * header.vertex_count);

	// write faces
	if (!faces.empty())
		file.write(reinterpret_cast<const char*>(faces.data()), (size_t)header.face_size * header.faces_count);

	// write sides
	if (!sides.empty())
		file.write(reinterpret_cast<const char*>(sides.data()), (size_t)header.side_size * header.sides_count);

	// write points
	if (!points.empty())
		file.write(reinterpret_cast<const char*>(points.data()), (size_t)header.point_size * header.points_count);

	file.close();

	print_log(PRINT_GREEN, "Saved CSM v3: {} materials, {} vertices, {} faces\n",
			  materials.size(), vertices.size(), faces.size());

	return true;
}

void CSMFile::upload()
{
	for (auto& m : model)
		delete m;
	model.clear();

	if (readed && !faces.empty())
	{
		std::map<unsigned int, std::vector<modelVert>> materialVerts;

		for (auto& f : faces)
		{
			if (f.material >= materials.size())
			{
				print_log(PRINT_RED, "Warning: Face has invalid material index {}\n", f.material);
				continue;
			}

			for (int i = 0; i < 3; i++)
			{
				if (f.index[i] >= vertices.size())
				{
					print_log(PRINT_RED, "Warning: Invalid vertex index {} in face\n", f.index[i]);
					continue;
				}

				modelVert vert;
				vert.pos = vertices[f.index[i]].point.flip();
				vert.u = f.tc[0].uv[i].x;
				vert.v = f.tc[0].uv[i].y;

				materialVerts[f.material].push_back(vert);
			}
		}

		for (auto& pair : materialVerts)
		{
			unsigned int matId = pair.first;
			std::vector<modelVert>& verts = pair.second;

			if (verts.empty())
				continue;

			CSM_MDL_MESH* mesh = new CSM_MDL_MESH();
			mesh->matid = matId;
			mesh->buffer = new VertexBuffer(g_app->modelShader, GL_TRIANGLES);
			mesh->verts = verts;

			if (matId < mat_textures.size())
				mesh->texture = mat_textures[matId];
			else
				mesh->texture = missingTex;

			mesh->buffer->setData(&mesh->verts[0], static_cast<int>(mesh->verts.size()), false);
			mesh->buffer->reupload();

			model.push_back(mesh);
		}

		print_log(PRINT_BLUE, "Uploaded {} material meshes with {} total vertices\n",
				  model.size(), vertices.size());
	}
}

void CSMFile::uploadSides()
{
	for (auto& s : sideModel)
		delete s;
	sideModel.clear();

	if (!showSides || sides.empty() || points.empty())
		return;

	for (auto& s : sides)
	{
		if (s.numpoints < 3)
			continue;
		if (s.firstpoint + s.numpoints > points.size())
			continue;

		CSM_SIDE_MESH* sideMesh = new CSM_SIDE_MESH();
		sideMesh->matid = s.material;
		sideMesh->buffer = new VertexBuffer(g_app->modelShader, GL_TRIANGLES);

		if (s.material < mat_textures.size())
			sideMesh->texture = mat_textures[s.material];
		else
			sideMesh->texture = missingTex;

		std::vector<vec3> poly;
		for (unsigned int i = 0; i < s.numpoints; i++)
			poly.push_back(points[s.firstpoint + i].flip());

		for (unsigned int i = 1; i + 1 < poly.size(); i++)
		{
			for (int j = 0; j < 3; j++)
			{
				modelVert vert;
				int idx = (j == 0) ? 0 : ((j == 1) ? i : i + 1);
				vert.pos = poly[idx];
				vert.u = s.vecs[0].x * vert.pos.x + s.vecs[0].y * vert.pos.y +
						 s.vecs[0].z * vert.pos.z + s.vecs[0].w;
				vert.v = s.vecs[1].x * vert.pos.x + s.vecs[1].y * vert.pos.y +
						 s.vecs[1].z * vert.pos.z + s.vecs[1].w;
				sideMesh->verts.push_back(vert);
			}
		}

		if (!sideMesh->verts.empty())
		{
			sideMesh->buffer->setData(&sideMesh->verts[0], static_cast<int>(sideMesh->verts.size()), false);
			sideMesh->buffer->reupload();
			sideModel.push_back(sideMesh);
		}
		else
		{
			delete sideMesh;
		}
	}
}

void CSMFile::draw()
{
	if (!readed)
		return;

	if (model.empty())
		upload();
	if (showSides && sideModel.empty())
		uploadSides();

	if (!model.empty())
	{
		for (auto& m : model)
		{
			if (m && m->buffer)
			{
				if (m->texture)
					m->texture->bind(0);
				else
					missingTex->bind(0);
				m->buffer->drawFull();
			}
		}
	}

	if (showSides && !sideModel.empty())
	{
		bool wasWireframe = false;
		if (showSides)
		{
			GLint polygonMode[2];
			glGetIntegerv(GL_POLYGON_MODE, polygonMode);
			wasWireframe = (polygonMode[0] == GL_LINE);
			if (!wasWireframe)
				glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		}

		for (auto& s : sideModel)
		{
			if (s && s->buffer)
			{
				if (s->texture)
					s->texture->bind(0);
				else
					missingTex->bind(0);
				s->buffer->drawFull();
			}
		}

		if (showSides && !wasWireframe)
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
}

void CSMFile::printInfo()
{
	if (!readed)
	{
		print_log("CSM file not loaded\n");
		return;
	}

	print_log("CSM Info:\n");
	print_log("  Version: {}\n", header.version);
	print_log("  Materials: {}\n", materials.size());
	print_log("  Vertices: {}\n", vertices.size());
	print_log("  Faces: {}\n", faces.size());
	print_log("  Sides: {}\n", sides.size());
	print_log("  Points: {}\n", points.size());
	print_log("  Bounds: ({:.2f},{:.2f},{:.2f}) to ({:.2f},{:.2f},{:.2f})\n",
			  header.model_mins.x, header.model_mins.y, header.model_mins.z,
			  header.model_maxs.x, header.model_maxs.y, header.model_maxs.z);

	if (!materials.empty())
	{
		print_log("  Material list:\n");
		for (size_t i = 0; i < materials.size(); i++)
			print_log("    [{}]: {}\n", i, materials[i]);
	}
}

std::map<unsigned int, CSMFile*> csm_models;
CSMFile* AddNewXashCsmToRender(const std::string& path, unsigned int sum)
{
	unsigned int crc32 = GetCrc32InMemory((unsigned char*)path.data(), (unsigned int)path.size(), sum);
	if (csm_models.find(crc32) != csm_models.end())
		return csm_models[crc32];
	CSMFile* newModel = new CSMFile(path);
	csm_models[crc32] = newModel;
	return newModel;
}
