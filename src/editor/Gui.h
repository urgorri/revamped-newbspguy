#include <GL/glew.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "Entity.h"
#include "remap.h"
#include "bsptypes.h"
#include "Texture.h"
#include "qtools/rad.h"
#include <GLFW/glfw3.h>

constexpr ImVec4 COLOR_DEEP_OBSIDIAN = ImVec4(0.043f, 0.047f, 0.063f, 1.000f);
constexpr ImVec4 COLOR_BLOOD_CRIMSON = ImVec4(0.545f, 0.078f, 0.165f, 1.000f);
constexpr ImVec4 COLOR_NIGHTMARE_PURPLE = ImVec4(0.149f, 0.161f, 0.290f, 1.000f);

class BspRenderer;

struct ModelInfo
{
	std::string classname;
	std::string targetname;
	std::string model;
	std::string val;
	std::string usage;
	int entIdx;
};

struct StatInfo
{
	std::string name;
	std::string val;
	std::string max;
	std::string fullness;
	float progress;
	ImVec4 color;
};

struct TextureStyle
{
	float scaleX, scaleY;
	float shiftX, shiftY;
	float rotateX, rotateY;
	bool valid = false;
};

class Renderer;

#define SHOW_IMPORT_OPEN 1
#define SHOW_IMPORT_ADD_NEW 2
#define SHOW_IMPORT_MODEL_ENTITY 3
#define SHOW_IMPORT_MODEL_BSP 4

extern bool filterNeeded;

class Gui
{
	friend class Renderer;

  public:
	Renderer* app;

	bool settingLoaded = false;
	bool updateTransformWidget = true;

	Gui(Renderer* app);

	void init();
	void setupTheme();
	void draw();

	void openContextMenu(int type);
	void openContextMenu(bool empty);
	void copyTexture();
	void pasteTexture();
	void copyStyle();
	void pasteStyle();
	void copyLightmap();
	void pasteLightmap();
	void refresh();
	void OpenFile(const std::string& file);

	bool polycount = false;
	bool showDebugWidget = false;
	bool showKeyvalueWidget = false;
	bool showTransformWidget = false;
	bool showLogWidget = false;
	bool showSettingsWidget = false;
	bool showHelpWidget = false;
	bool showAboutWidget = false;
	int showImportMapWidget_Type = 0;
	bool showImportMapWidget = false;
	bool showMergeMapWidget = false;
	bool showShiftMapDialog = false;
	vec3 shiftMapDelta = vec3(0.0f, 0.0f, 0.0f);
	bool showModentDialog = false;
	bool showLimitsWidget = true;
	bool showFaceEditWidget = false;
	bool showLightmapEditorWidget = false;
	bool showLightmapEditorUpdate = true;
	bool showEntityReport = false;
	bool showGOTOWidget_update = true;
	bool showGOTOWidget = false;
	bool showTextureBrowser = false;
	bool showOverviewWidget = false;
	bool orthoMode = true;
	bool wasInOverview = false;
	vec3 oldCameraOrigin;
	vec3 oldCameraAngles;
	bool reloadSettings = true;
	bool openSavedTabs = false;
	bool allowExternalTextures = false;

	bool entityListChanged = true;
	bool limitsInvalidated = true;
	bool textureBrowserCacheInvalidated = true;

	void invalidateTextureBrowserCache() { textureBrowserCacheInvalidated = true; }

	void recompileLighting();

  private:
	ImGuiIO* imgui_io = NULL;
	int settingsTab = 0;

	ImFont* defaultFont;
	ImFont* smallFont;
	ImFont* largeFont;
	ImFont* consoleFont;
	ImFont* consoleFontLarge;
	float fontSize = 22.f;
	bool shouldReloadFonts = false;
	bool shouldReloadTextureInfo = false;

	Texture* objectIconTexture;
	Texture* faceIconTexture;
	Texture* leafIconTexture;

	Texture* keyvaluesIconTexture = nullptr;
	Texture* transformIconTexture = nullptr;
	Texture* faceEditorIconTexture = nullptr;
	Texture* textureBrowserIconTexture = nullptr;
	Texture* lightmapIconTexture = nullptr;
	Texture* logIconTexture = nullptr;
	Texture* debugIconTexture = nullptr;
	Texture* overviewIconTexture = nullptr;
	Texture* mergeIconTexture = nullptr;

	bool badSurfaceExtents = false;
	bool lightmapTooLarge = false;

	bool loadedLimit[SORT_MODES] = {false};
	std::vector<ModelInfo> limitModels[SORT_MODES];
	bool loadedStats = false;
	std::vector<StatInfo> stats;

	bool anyHullValid[MAX_MAP_HULLS] = {false};

	int guiHoverAxis; // axis being hovered in the transform menu

	int lastMAX_FILTERS = 0;
	std::vector<std::string> lastKeyFilters, lastValueFilters;
	std::vector<int> lastOpFilters;
	std::vector<int> lastLogicFilters;

	int openEmptyContext = -2; // open context menu for rightclicking world/void

	int copiedMiptex = -1;
	TextureStyle copiedStyle;
	LIGHTMAP copiedLightmap = LIGHTMAP();
	std::vector<COLOR3> copiedLightmapData;

	void drawBspContexMenu();
	void drawContextMenu_Entity();
	void drawContextMenu_Face();
	void drawContextMenu_Empty();

	void drawMenuBar();
	void drawMenu_File();
	void drawMenu_Edit();
	void drawMenu_View();
	void drawMenu_Map();
	void drawMenu_Tools();
	void drawMenu_Create();
	void drawMenu_Windows();
	void drawMenu_Help();
	void drawMenu_Debug();
	void drawToolbar();
	void drawPanelsToolbar();
	void drawFpsOverlay();
	void drawStatusMessage();
	void drawStatusBar();
	void drawDebugWidget();
	void drawTextureBrowser();
	void drawOverviewWidget();
	void drawKeyvalueEditor();
	void drawKeyvalueEditor_SmartEditTab(int entIdx);
	void drawKeyvalueEditor_FlagsTab(int entIdx);
	void drawKeyvalueEditor_RawEditTab(int entIdx);
	void drawGOTOWidget();
	void drawMDLWidget();
	void drawTransformWidget();
	void drawLog();
	void drawSettings();
	void drawHelp();
	void drawAbout();
	void drawImportMapWidget();
	void drawMergeWindow();
	void drawShiftMapDialog();
	void drawModentDialog();
	void drawLimits();
	void drawLightMapTool();
	void drawFaceEditorWidget();
	void drawLimitTab(Bsp* map, int sortMode);
	void drawUndoMemUsage(BspRenderer* rend);
	void drawEntityReport();
	StatInfo calcStat(std::string name, unsigned int val, unsigned int max, bool isMem);
	ModelInfo calcModelStat(Bsp* map, STRUCTUSAGE* modelInfo, unsigned int val, unsigned int max, bool isMem);
	void checkValidHulls();
	void reloadLimits();
	void ExportOneBigLightmap(Bsp* map);
	void ExportFaceModel(Bsp* src_map, const std::string& export_path, const std::vector<int>& faceIdxs, int ExportType, bool movemodel);
	void loadFonts();
	void checkFaceErrors();

	bool showRadErrorModal = false;
	std::string radErrorMessage;
	std::string radLogFilePath;
	void drawRadErrorModal();
};

ImVec4 imguiColorFromConsole(unsigned int colors);
int ImportModel(Bsp* map, const std::string& mdl_path, bool noclip = false);