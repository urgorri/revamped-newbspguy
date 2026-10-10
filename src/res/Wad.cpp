#include "lang.h"
#include "Wad.h"
#include "log.h"
#include "Settings.h"
#include "Renderer.h"

Wad::Wad(void)
{
	dirEntries.clear();
	fileData.clear();
	textureCache.clear();
	fileLoadedInMemory = false;
}

Wad::Wad(std::string file, CacheMode mode)
	: filename(std::move(file)), cacheMode(mode)
{
	this->wadname = basename(filename);
	dirEntries.clear();
	textureCache.clear();
	fileLoadedInMemory = false;

	if (cacheMode == CACHE_ALL)
	{
		if (!loadFullFile())
		{
			fileData.clear();
			fileLoadedInMemory = false;
		}
	}
}

bool Wad::readInfo()
{
	std::string file = filename;

	if (!fileExists(file))
	{
		print_log(get_localized_string(LANG_0247), filename);
		return false;
	}

	std::ifstream wadFile(file, std::ios::binary);
	if (!wadFile.is_open())
	{
		print_log(get_localized_string(LANG_1043), filename);
		return false;
	}

	wadFile.read((char*)&header, sizeof(WADHEADER));

	if (strncmp(header.szMagic, "WAD3", 4) != 0)
	{
		print_log(get_localized_string(LANG_0249), filename);
		wadFile.close();
		return false;
	}

	wadFile.seekg(0, std::ios::end);
	size_t fileSize = wadFile.tellg();
	if (header.nDirOffset >= (int)fileSize)
	{
		print_log(get_localized_string(LANG_0250), filename);
		wadFile.close();
		return false;
	}

	wadFile.seekg(header.nDirOffset, std::ios::beg);
	dirEntries.clear();
	usableTextures = false;

	for (int i = 0; i < header.nDir; i++)
	{
		WADDIRENTRY tmpWadEntry = WADDIRENTRY();

		wadFile.read((char*)&tmpWadEntry, sizeof(WADDIRENTRY));

		W_CleanupName(tmpWadEntry.szName, tmpWadEntry.szName);
		dirEntries.push_back(tmpWadEntry);

		if (dirEntries[i].nType == 0x43)
			usableTextures = true;
	}

	wadFile.close();

	if (!usableTextures)
	{
		print_log(get_localized_string(LANG_0252), basename(filename));
		if (!dirEntries.size())
		{
			return false;
		}
	}

	if (cacheMode == CACHE_ALL && !fileLoadedInMemory)
	{
		if (!loadFullFile())
		{
			return false;
		}
	}

	return true;
}

bool Wad::loadFullFile()
{
	if (fileLoadedInMemory && fileData.size() > 0)
		return true;

	if (!readFile(filename, fileData))
	{
		print_log(get_localized_string(LANG_1043), filename);
		return false;
	}

	if (fileData.size() < sizeof(WADHEADER))
	{
		fileData.clear();
		print_log(get_localized_string(LANG_0248), filename);
		return false;
	}

	fileLoadedInMemory = true;
	return true;
}

void Wad::unloadFile()
{
	fileData.clear();
	fileLoadedInMemory = false;
}

void Wad::clearCache()
{
	textureCache.clear();
}

void Wad::precacheAllTextures()
{
	if (cacheMode == CACHE_NONE)
		return;

	if (!fileLoadedInMemory && !loadFullFile())
		return;

	textureCache.clear();

	for (int i = 0; i < (int)dirEntries.size(); i++)
	{
		if (dirEntries[i].nType == 0x43)
		{
			std::string texName = dirEntries[i].szName;
			if (textureCache.find(texName) == textureCache.end())
			{
				WADTEX tex = readTextureFromMemory(i);
				textureCache[texName] = tex;
			}
		}
	}
}

int Wad::findTextureIndex(const std::string& texname)
{
	for (int d = 0; d < header.nDir; d++)
	{
		if (strcasecmp(texname.c_str(), dirEntries[d].szName) == 0)
		{
			return d;
		}
	}
	return -1;
}

WADTEX Wad::readTexture(int dirIndex, int* texturetype)
{
	if (dirIndex >= (int)dirEntries.size())
	{
		print_log(get_localized_string(LANG_0253));
		return {};
	}

	std::string name = std::string(dirEntries[dirIndex].szName);
	return readTexture(name, texturetype);
}

WADTEX Wad::readTexture(const std::string& texname, int* texturetype)
{
	auto it = textureCache.find(texname);
	if (it != textureCache.end())
	{
		if (texturetype)
		{
			int idx = findTextureIndex(texname);
			if (idx >= 0)
			{
				*texturetype = dirEntries[idx].nType;
			}
		}
		return it->second;
	}

	WADTEX tex{};

	if (cacheMode == CACHE_ALL || (cacheMode == CACHE_LAZY && fileLoadedInMemory))
	{
		tex = readTextureFromMemory(texname);
	}
	else
	{
		tex = readTextureFromFile(texname);
	}

	if (cacheMode != CACHE_NONE)
	{
		textureCache[texname] = tex;
	}

	if (texturetype)
	{
		int idx = findTextureIndex(texname);
		if (idx >= 0)
		{
			*texturetype = dirEntries[idx].nType;
		}
	}

	return tex;
}

WADTEX Wad::readTextureFromMemory(int dirIndex)
{
	if (dirIndex >= (int)dirEntries.size())
		return {};

	if (!fileLoadedInMemory && !loadFullFile())
		return {};

	WADDIRENTRY& entry = dirEntries[dirIndex];

	if (entry.bCompression)
	{
		print_log(get_localized_string(LANG_0254));
		return {};
	}

	int offset = entry.nFilePos;
	if (offset < 0 || offset + (int)sizeof(BSPMIPTEX) > (int)fileData.size())
		return {};

	BSPMIPTEX mtex = BSPMIPTEX();
	memcpy((char*)&mtex, &fileData[offset], sizeof(BSPMIPTEX));
	offset += sizeof(BSPMIPTEX);

	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0255), mtex.szName, mtex.nWidth, mtex.nHeight);

	int w = mtex.nWidth;
	int h = mtex.nHeight;

	int szAll = calcMipsSize(w, h) + sizeof(short) + /*pal size*/ sizeof(COLOR3) * 256;

	if (offset + szAll > (int)fileData.size())
		return {};

	WADTEX tex;
	tex.data.resize(szAll);
	memcpy(tex.data.data(), &fileData[offset], szAll);

	memcpy(tex.szName, mtex.szName, MAXTEXTURENAME);

	for (int i = 0; i < MIPLEVELS; i++)
		tex.nOffsets[i] = mtex.nOffsets[i];
	tex.nWidth = w;
	tex.nHeight = h;

	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0256), tex.szName, tex.nWidth, tex.nHeight);

	return tex;
}

WADTEX Wad::readTextureFromMemory(const std::string& texname)
{
	int idx = findTextureIndex(texname);
	if (idx < 0)
		return {};

	return readTextureFromMemory(idx);
}

WADTEX Wad::readTextureFromFile(int dirIndex)
{
	if (dirIndex >= (int)dirEntries.size())
		return {};

	WADDIRENTRY& entry = dirEntries[dirIndex];

	if (entry.bCompression)
	{
		print_log(get_localized_string(LANG_0254));
		return {};
	}

	std::ifstream wadFile(filename, std::ios::binary);
	if (!wadFile.is_open())
		return {};

	wadFile.seekg(entry.nFilePos, std::ios::beg);

	BSPMIPTEX mtex = BSPMIPTEX();
	wadFile.read((char*)&mtex, sizeof(BSPMIPTEX));

	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0255), mtex.szName, mtex.nWidth, mtex.nHeight);

	int w = mtex.nWidth;
	int h = mtex.nHeight;

	int szAll = calcMipsSize(w, h) + sizeof(short) + sizeof(COLOR3) * 256;

	WADTEX tex;
	tex.data.resize(szAll);
	wadFile.read((char*)tex.data.data(), szAll);
	wadFile.close();

	memcpy(tex.szName, mtex.szName, MAXTEXTURENAME);

	for (int i = 0; i < MIPLEVELS; i++)
		tex.nOffsets[i] = mtex.nOffsets[i];

	tex.nWidth = w;
	tex.nHeight = h;

	if (g_settings.verboseLogs)
		print_log(get_localized_string(LANG_0256), tex.szName, tex.nWidth, tex.nHeight);

	return tex;
}

WADTEX Wad::readTextureFromFile(const std::string& texname)
{
	int idx = findTextureIndex(texname);
	if (idx < 0)
		return {};

	return readTextureFromFile(idx);
}

bool Wad::hasTexture(const std::string& texname)
{
	if (textureCache.find(texname) != textureCache.end())
		return true;

	for (int d = 0; d < header.nDir; d++)
		if (strcasecmp(texname.c_str(), dirEntries[d].szName) == 0)
			return true;
	return false;
}

bool Wad::hasTexture(int dirIndex)
{
	if (dirIndex >= (int)dirEntries.size())
	{
		return false;
	}
	return true;
}

bool Wad::write(const std::vector<WADTEX>& textures)
{
	return write(filename, textures);
}

bool Wad::write(const std::string& _filename, const std::vector<WADTEX>& textures)
{
	this->filename = _filename;

	std::ofstream myFile(filename, std::ios::trunc | std::ios::binary);

	header.szMagic[0] = 'W';
	header.szMagic[1] = 'A';
	header.szMagic[2] = 'D';
	header.szMagic[3] = '3';
	header.nDir = (int)textures.size();

	int tSize = (int)(sizeof(BSPMIPTEX) * textures.size());
	for (size_t i = 0; i < textures.size(); i++)
	{
		int w = textures[i].nWidth;
		int h = textures[i].nHeight;

		int szAll = calcMipsSize(w, h) + sizeof(short) + /* pal num */ sizeof(COLOR3) * 256;

		szAll = (szAll + 3) & ~3; // 4 bytes padding

		tSize += szAll;
	}

	if (tSize > 0)
	{
		header.nDirOffset = (int)(sizeof(WADHEADER) + tSize);
		myFile.write((char*)&header, sizeof(WADHEADER));

		for (size_t i = 0; i < textures.size(); i++)
		{
			BSPMIPTEX miptex = BSPMIPTEX();
			memcpy(miptex.szName, textures[i].szName, MAXTEXTURENAME);

			int w = textures[i].nWidth;
			int h = textures[i].nHeight;
			int sz = w * h;	   // miptex 0
			int sz2 = sz / 4;  // miptex 1
			int sz3 = sz2 / 4; // miptex 2
			int sz4 = sz3 / 4; // miptex 3
			int szAll = sz + sz2 + sz3 + sz4 + sizeof(short) /* pal num*/ + sizeof(COLOR3) * 256;

			int padding = ((szAll + 3) & ~3) - szAll; // 4 bytes padding

			miptex.nWidth = w;
			miptex.nHeight = h;
			miptex.nOffsets[0] = sizeof(BSPMIPTEX);
			miptex.nOffsets[1] = sizeof(BSPMIPTEX) + sz;
			miptex.nOffsets[2] = sizeof(BSPMIPTEX) + sz + sz2;
			miptex.nOffsets[3] = sizeof(BSPMIPTEX) + sz + sz2 + sz3;

			myFile.write((char*)&miptex, sizeof(BSPMIPTEX));

			// 256 palette
			((unsigned char*)textures[i].data.data())[sz + sz2 + sz3 + sz4] = 0x00;
			((unsigned char*)textures[i].data.data())[sz + sz2 + sz3 + sz4 + 1] = 0x01;

			myFile.write((char*)textures[i].data.data(), szAll);
			if (padding > 0)
			{
				unsigned char* zeropad = new unsigned char[padding];
				memset(zeropad, 0, padding);
				myFile.write((const char*)zeropad, padding);
				delete[] zeropad;
			}
		}

		int offset = sizeof(WADHEADER);
		for (size_t i = 0; i < textures.size(); i++)
		{
			WADDIRENTRY entry = WADDIRENTRY();
			entry.nFilePos = offset;

			int szAll = calcMipsSize(textures[i].nWidth, textures[i].nHeight) + sizeof(short) + /* pal num */ sizeof(COLOR3) * 256;

			szAll = (szAll + 3) & ~3; // 4 bytes padding

			entry.nDiskSize = szAll + sizeof(BSPMIPTEX);
			entry.nSize = szAll + sizeof(BSPMIPTEX);
			entry.nType = 0x43; // Texture
			entry.bCompression = false;
			entry.nDummy = 0;

			for (int k = 0; k < MAXTEXTURENAME; k++)
				memcpy(entry.szName, textures[i].szName, MAXTEXTURENAME);

			offset += szAll + sizeof(BSPMIPTEX);

			myFile.write((char*)&entry, sizeof(WADDIRENTRY));
		}
	}
	else
	{
		header.nDirOffset = 0;
		myFile.write((char*)&header, sizeof(WADHEADER));
	}

	myFile.close();

	return true;
}

WADTEX::WADTEX()
{
	data.clear();
	szName[0] = '\0';
	nWidth = nHeight = 0;
	nOffsets[0] = nOffsets[1] = nOffsets[2] = nOffsets[3] = 0;
}

WADTEX::WADTEX(BSPMIPTEX* tex, unsigned char* palette, unsigned short colors)
{
	if (!tex || tex->nWidth == 0 || tex->nHeight == 0)
	{
		szName[0] = '\0';
		nWidth = nHeight = 0;
		nOffsets[0] = nOffsets[1] = nOffsets[2] = nOffsets[3] = 0;
		return;
	}
	memcpy(szName, tex->szName, MAXTEXTURENAME);

	nWidth = tex->nWidth;
	nHeight = tex->nHeight;
	for (int i = 0; i < MIPLEVELS; i++)
		nOffsets[i] = tex->nOffsets[i] /* - sizeof(BSPMIPTEX)*/;

	if (nOffsets[0] <= 0)
	{
		return;
	}

	int sz = calcMipsSize(tex->nWidth, tex->nHeight);

	unsigned char* texdata = ((unsigned char*)tex) + tex->nOffsets[0];
	if (palette)
	{
		data = std::vector<unsigned char>(texdata, texdata + sz);
		data.push_back(0);
		data.push_back(0);
		*(unsigned short*)(data.data() + sz) = colors;
		data.insert(data.end(), palette, palette + sizeof(COLOR3) * colors);
	}
	else
	{
		data = std::vector<unsigned char>(texdata, texdata + (sz + sizeof(short) + sizeof(COLOR3) * 256));
	}
}