// A separate executable using the production platform bootstrap and game renderer.
// Renaming only this translation unit's entry/factory keeps the normal game unchanged.
#define main CloudPreviewPlatformMain
#define CreateGameEngine CloudPreviewOriginalFactory
#include "../Main/PosixMain.cpp"
#undef CreateGameEngine
#undef main

#include "CloudPreviewScene.h"
#include "CloudPreviewVolume.h"
#include "CloudPreviewColors.h"
#include "CloudPreviewColorPanel.h"
#include "Common/GlobalData.h"
#include "GameClient/Display.h"
#include "GameClient/MapUtil.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "SdlDevice/GameClient/SdlInput.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

extern void GameEngine_startSkirmish(Int numPlayers);

namespace {
bool previewStarted = false;
bool previewValid = false;
CloudPreviewScene::Appearance previewAppearance = CloudPreviewScene::Appearance::Volumetric;
CloudPreviewScene::Scenario previewScenario = CloudPreviewScene::Scenario::Tractor;
unsigned previewFrameLimit = 0;
bool previewInteraction = true;
bool previewHeat = true;
bool previewLooping = true;
bool previewColorPanel = true;

struct ScenarioChoice
{
    const char *argument;
    CloudPreviewScene::Scenario scenario;
    SDL_Keycode key;
};

const ScenarioChoice scenarioChoices[] = {
    { "comparison", CloudPreviewScene::Scenario::Comparison, SDLK_F1 },
    { "tractor", CloudPreviewScene::Scenario::Tractor, SDLK_F2 },
    { "anthrax", CloudPreviewScene::Scenario::AnthraxBomb, SDLK_F3 },
    { "gamma", CloudPreviewScene::Scenario::AnthraxGamma, SDLK_F4 },
    { "toxin-building", CloudPreviewScene::Scenario::ToxinBuilding, SDLK_F5 },
    { "nuke", CloudPreviewScene::Scenario::NuclearMissile, SDLK_F6 },
    { "nuke-cannon", CloudPreviewScene::Scenario::NuclearCannon, SDLK_F7 },
    { "reactor", CloudPreviewScene::Scenario::NuclearReactor, SDLK_F8 }
};
constexpr unsigned scenarioCount = sizeof(scenarioChoices) / sizeof(scenarioChoices[0]);

unsigned scenarioPalette()
{
    using Scenario = CloudPreviewScene::Scenario;
    switch (CloudPreviewScene::getScenario()) {
        case Scenario::AnthraxBomb: return 2;
        case Scenario::AnthraxGamma: return 3;
        case Scenario::NuclearMissile:
        case Scenario::NuclearCannon:
        case Scenario::NuclearReactor: return 1;
        default: return 0;
    }
}

void showPaletteScene(unsigned palette)
{
    using Scenario = CloudPreviewScene::Scenario;
    const Scenario scenarios[] = {Scenario::Tractor, Scenario::NuclearMissile,
        Scenario::AnthraxBomb, Scenario::AnthraxGamma};
    if (palette >= 4) return;
    CloudPreviewScene::setAppearance(CloudPreviewScene::Appearance::Volumetric);
    if (CloudPreviewScene::getScenario() != scenarios[palette])
        CloudPreviewScene::setScenario(scenarios[palette]);
    else if (std::strncmp(CloudPreviewScene::scenarioPhase(), "clear", 5) == 0)
        CloudPreviewScene::replayScenario();
    // Show Scene plays the selected source. Ordinary edits preserve pause and
    // source timing, so artists can inspect an exact frame while tuning colors.
    if (TheGameLogic && TheGameLogic->isGamePaused()) TheGameLogic->setGamePaused(FALSE, FALSE);
}

CloudPreviewScene::Scenario parseScenario(const std::string &value)
{
    for (const ScenarioChoice &choice : scenarioChoices)
        if (value == choice.argument) return choice.scenario;
    throw std::runtime_error("invalid scenario: " + value
        + "; choose comparison, tractor, anthrax, gamma, toxin-building, nuke, nuke-cannon, or reactor");
}

bool isScenarioKey(SDL_Keycode key)
{
    if (key == SDLK_LEFTBRACKET || key == SDLK_RIGHTBRACKET || key == SDLK_B || key == SDLK_L)
        return true;
    for (const ScenarioChoice &choice : scenarioChoices)
        if (key == choice.key) return true;
    return false;
}

void handleScenarioKey(SDL_Keycode key)
{
    const auto previous = CloudPreviewScene::getScenario();
    if (key == SDLK_B) {
        CloudPreviewScene::replayScenario();
    } else if (key == SDLK_L) {
        CloudPreviewScene::setLooping(!CloudPreviewScene::isLooping());
    } else if (key == SDLK_LEFTBRACKET || key == SDLK_RIGHTBRACKET) {
        unsigned index = 0;
        for (; index < scenarioCount; ++index)
            if (scenarioChoices[index].scenario == CloudPreviewScene::getScenario()) break;
        index = key == SDLK_RIGHTBRACKET ? (index + 1) % scenarioCount
            : (index + scenarioCount - 1) % scenarioCount;
        CloudPreviewScene::setScenario(scenarioChoices[index].scenario);
    } else {
        for (const ScenarioChoice &choice : scenarioChoices)
            if (key == choice.key) { CloudPreviewScene::setScenario(choice.scenario); break; }
    }
    if (previous != CloudPreviewScene::getScenario()) CloudPreviewColorPanel::selectPalette(scenarioPalette());
}

void updatePreviewTitle()
{
    if (SDL_Window *window = SdlInput_gameWindow()) {
        const std::string title = std::string(CloudPreviewScene::scenarioName())
            + " | " + CloudPreviewScene::scenarioPhase()
            + " | " + CloudPreviewScene::appearanceName()
            + (CloudPreviewScene::isLooping() ? " | Loop ON" : " | Loop OFF")
            + (CloudPreviewVolume::isInteractionEnabled() ? " | Volume wakes ON" : " | Volume wakes OFF")
            + (CloudPreviewVolume::isHeatEnabled() ? " | H: nuclear heat ON" : " | H: nuclear heat OFF")
            + " | P: colors | F1-F8/[]: scene | 1/2/C: appearance | B: replay L: loop | Space: pause Esc: close";
        SDL_SetWindowTitle(window, title.c_str());
    }
}

class CloudPreviewEngine : public SdlGameEngine
{
public:
    explicit CloudPreviewEngine(const WindowRequest &request) : SdlGameEngine(request) {}
    void init(int argc, char **argv) override
    {
        SdlGameEngine::init(argc, argv);
        // Use the indexed user-map name directly. The game's -map short-path
        // expander is designed for relative names and strips a POSIX root slash.
        AsciiString map = TheMapCache->getUserMapDir();
        map.concat("\\CloudPreview\\CloudPreview.map");
        const MapMetaData *metadata = TheMapCache->findMap(map);
        if (!metadata || !metadata->m_isMultiplayer) {
            std::fprintf(stderr, "cloud_preview: generated preview map was not registered: %s\n", map.str());
            setQuitting(TRUE);
            return;
        }
        TheWritableGlobalData->m_mapName = map;
        TheWritableGlobalData->m_autoSkirmishPlayers = 2;
        TheWritableGlobalData->m_showNetBox = FALSE;
        TheWritableGlobalData->m_healthBarMode = 0;
        // The laboratory's shoreline is a reference for the current water
        // renderer. This is local to the preview and never saves game options.
        TheWritableGlobalData->m_waterStyle = 1;
        GameEngine_startSkirmish(2);
        startupTime = SDL_GetTicks();
    }
    ~CloudPreviewEngine() override
    {
        previewValid = previewStarted && CloudPreviewScene::validationPassed();
        CloudPreviewColorPanel::shutdown();
        CloudPreviewScene::shutdown();
    }

    void update() override
    {
        // The game's -maxframes ends the match and clears its objects before
        // teardown. Stop our host directly, while the live scene can be checked.
        if (previewStarted && previewFrameLimit && TheGameLogic->getFrame() >= previewFrameLimit) {
            setQuitting(TRUE);
            return;
        }
        // MSG_NEW_GAME sets the mode before the deferred map load. Wait for the
        // first completed logic tick so terrain, players and pathfinding exist.
        if (!previewStarted && TheGameLogic && TheGameLogic->isInGame()
            && !TheGameLogic->isInShellGame() && !TheGameLogic->isLoadingMap()
            && TheGameLogic->getFrame() > 0) {
			// This executable owns its two-field scene and A/B controls. Keep its
			// Classic side independent of the normal game's saved cloud setting.
			TheWritableGlobalData->m_cloudStyle = 0;
            CloudPreviewScene::setScenario(previewScenario);
            // Scene and appearance are independent startup choices. Every
            // source supports the original and volumetric appearances.
            CloudPreviewScene::setAppearance(previewAppearance);
            CloudPreviewScene::setLooping(previewLooping);
            previewStarted = CloudPreviewScene::initialize();
            if (!previewStarted) {
                std::fprintf(stderr, "cloud_preview: scene initialization failed\n");
                setQuitting(TRUE);
                return;
            }
            CloudPreviewVolume::setInteractionEnabled(previewInteraction);
            CloudPreviewVolume::setHeatEnabled(previewHeat);
            if (!CloudPreviewColorPanel::initialize()) {
                std::fprintf(stderr, "cloud_preview: color panel initialization failed\n");
                previewStarted = false;
                setQuitting(TRUE);
                return;
            }
            CloudPreviewColorPanel::selectPalette(scenarioPalette());
            CloudPreviewColorPanel::setVisible(previewColorPanel);
            updatePreviewTitle();
        }
        // Check readiness first: a slow but successful synchronous map load must
        // not be mistaken for a startup that is still stuck in the menus.
        if (!previewStarted && SDL_GetTicks() - startupTime > 30000) {
            std::fprintf(stderr, "cloud_preview: timed out waiting for the preview map\n");
            setQuitting(TRUE);
            return;
        }
        if (previewStarted) {
            CloudPreviewScene::update();
            // Scenario phases change with logic time, but the title must also
            // refresh while paused and after a scene is selected in that state.
            const Uint64 now = SDL_GetTicks();
            if (now - lastTitleUpdate >= 1000) {
                updatePreviewTitle();
                lastTitleUpdate = now;
            }
        }
        SdlGameEngine::update();
    }

    void serviceWindowsOS() override
    {
        if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) return;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (previewStarted && CloudPreviewColorPanel::handle(event)) {
                const int palette = CloudPreviewColorPanel::takePreviewRequest();
                if (palette >= 0) showPaletteScene(static_cast<unsigned>(palette));
                updatePreviewTitle();
                continue;
            }
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                setQuitting(TRUE);
            } else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                setIsActive(TRUE);
            } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                setIsActive(FALSE);
            } else if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
                && (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_SPACE)) {
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    if (event.key.key == SDLK_ESCAPE) setQuitting(TRUE);
                    else if (previewStarted)
                        TheGameLogic->setGamePaused(!TheGameLogic->isGamePaused(), FALSE);
                }
            } else if (previewStarted
                && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
                && isScenarioKey(event.key.key)) {
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    handleScenarioKey(event.key.key);
                    updatePreviewTitle();
                }
            } else if (previewStarted
                && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
                && (event.key.key == SDLK_1 || event.key.key == SDLK_2
                    || event.key.key == SDLK_3 || event.key.key == SDLK_C)) {
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    using Appearance = CloudPreviewScene::Appearance;
                    Appearance appearance = CloudPreviewScene::getAppearance();
                    if (event.key.key == SDLK_1) appearance = Appearance::Original;
                    else if (event.key.key == SDLK_2 || event.key.key == SDLK_3)
                        appearance = Appearance::Volumetric;
                    else appearance = appearance == Appearance::Original
                        ? Appearance::Volumetric : Appearance::Original;
                    CloudPreviewScene::setAppearance(appearance);
                    updatePreviewTitle();
                }
            } else if (previewStarted
                && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
                && (event.key.key == SDLK_Q || event.key.key == SDLK_E
                    || event.key.key == SDLK_R || event.key.key == SDLK_F12
                    || event.key.key == SDLK_I || event.key.key == SDLK_H)) {
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    // Handle inspection directly so it also works while the
                    // game's paused input path is suppressing normal commands.
                    if (event.key.key == SDLK_I) {
                        CloudPreviewVolume::setInteractionEnabled(!CloudPreviewVolume::isInteractionEnabled());
                        updatePreviewTitle();
                    } else if (event.key.key == SDLK_H) {
                        CloudPreviewVolume::setHeatEnabled(!CloudPreviewVolume::isHeatEnabled());
                        updatePreviewTitle();
                    } else if (event.key.key == SDLK_F12) {
                        if (TheDisplay) TheDisplay->takeScreenShot();
                    } else if (TheTacticalView) {
                        if (event.key.key == SDLK_R) {
                            TheTacticalView->setAngleAndPitchToDefault();
                            TheTacticalView->setZoomToHeight(520.0f);
                        } else {
                            const Real delta = event.key.key == SDLK_Q ? -0.785398163f : 0.785398163f;
                            TheTacticalView->setAngle(TheTacticalView->getAngle() + delta);
                        }
                    }
                }
            } else {
                SdlInput_dispatch(event);
            }
        }
    }
private:
    Uint64 startupTime = 0;
    Uint64 lastTitleUpdate = 0;
};

unsigned positiveNumber(const std::string &value, unsigned low, unsigned high)
{
    char *end = nullptr;
    const unsigned long n = std::strtoul(value.c_str(), &end, 10);
    if (value.empty() || *end || n < low || n > high)
        throw std::runtime_error("invalid number: " + value);
    return static_cast<unsigned>(n);
}

void printHelp()
{
    std::puts(
        "Isolated toxin and radiation preview with volumetric smoke.\n"
        "Usage: cloud_preview [--data ZERO_HOUR_FOLDER] [--user-dir FOLDER]\n"
        "       [--width N] [--height N] [--frames N] [--screenshot-frame N]\n"
        "       [--scenario NAME] [--no-loop]\n"
        "       [--classic | --volumetric] [--no-interaction] [--no-heat]\n"
        "       [--no-color-panel] [--hidden | --offscreen]\n\n"
        "Four passive infantry walk through visual-only contamination fields.\n"
        "The tractor release scenario is the default; scenarios loop unless --no-loop is set.\n"
        "--scenario: comparison | tractor | anthrax | gamma | toxin-building | nuke | nuke-cannon | reactor\n"
        "F1: comparison | F2: toxin tractor | F3: anthrax bomb | F4: anthrax gamma\n"
        "F5: toxin building | F6: nuclear missile | F7: nuclear cannon | F8: nuclear reactor\n"
        "[/]: previous/next scenario | B: replay release | L: toggle looping\n"
        "Volumetric smoke is the default. --classic uses the game's original effects;\n"
        "--volumetric selects the new volumes. Both appearances work in every scene.\n"
        "Appearance options keep --scenario (default: tractor).\n"
        "1: original effects | 2: volumetric smoke | C: toggle appearance | 3: alias for 2\n"
        "Appearance changes preserve the current scene and release phase; new scenes keep the appearance.\n"
        "Both appearances switch immediately, even while paused.\n"
        "I: toggle volumetric infantry wakes | Q/E: orbit 45 degrees | R: reset orientation/zoom\n"
        "H: toggle heat distortion for nuclear volumetric clouds (on by default)\n"
        "--no-heat starts with nuclear heat distortion off for comparison\n"
        "P: show/hide RGB, Brightness and Transparency controls for all four cloud types\n"
        "Color panel: Show scene, Reset palette, Save all, Load saved, Copy all\n"
        "Saved palettes load from cloud-colors.ini in the preview user folder. No automatic saves.\n"
        "F12: screenshot (also while paused) | --no-interaction starts with wakes off\n"
        "Arrows: pan | wheel: zoom | middle mouse: rotate | Space: pause | Esc: close\n"
        "Game assets are read from the existing installation. Logs and BMP screenshots\n"
        "go to the separate cloud-preview/user folder beside this build.\n"
        "Preview controls are independent of the game's Cloud style setting.\n"
        "--frames counts engine logic frames (60 per second).");
}

// This does not ask EarlyOptions for a user directory: that function caches the first
// directory it sees, and the engine must see the preview's isolated directory first.
std::filesystem::path playerRegistry(const std::string &taskHome)
{
    const char *forced = std::getenv("ZH_USER_DATA_DIR");
    if (forced && *forced) return std::filesystem::path(forced) / "Registry.ini";
    const char *xdg = std::getenv("XDG_DATA_HOME");
    const std::filesystem::path base = xdg && *xdg ? std::filesystem::path(xdg)
        : std::filesystem::path(taskHome) / ".local/share";
    return base / "Command and Conquer Generals Zero Hour Data/Registry.ini";
}

// Preserve only the optional base-game location; no personal settings or saves are copied.
void copyBaseGameLocation(const std::filesystem::path &original, const std::filesystem::path &preview)
{
    if (std::filesystem::exists(preview)) return;
    std::ifstream input(original);
    std::string line, location;
    while (std::getline(input, line)) {
        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        std::string key = line.substr(0, equals);
        const size_t first = key.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        key = key.substr(first, key.find_last_not_of(" \t\r") - first + 1);
        if (strcasecmp(key.c_str(), "Generals\\InstallPath") == 0) location = line;
    }
    if (!location.empty()) {
        std::ofstream output(preview);
        output << location << '\n';
        if (!output) throw std::runtime_error("cannot write preview's base-game location");
    }
}
} // namespace

GameEngine *CreateGameEngine()
{
    return NEW CloudPreviewEngine(s_windowRequest);
}

int main(int argc, char **argv)
{
    try {
        std::string data;
        std::filesystem::path userDir = std::filesystem::path(CLOUD_PREVIEW_DIRECTORY) / "user";
        unsigned width = 1280, height = 800, frames = 0, screenshot = 0;
        bool hidden = false, offscreen = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto value = [&]() -> std::string {
                if (++i == argc) throw std::runtime_error("missing value after " + arg);
                return argv[i];
            };
            if (arg == "--help" || arg == "-h") { printHelp(); return 0; }
            else if (arg == "--data") data = std::filesystem::absolute(value()).string();
            else if (arg == "--user-dir") userDir = std::filesystem::absolute(value());
            else if (arg == "--width") width = positiveNumber(value(), 640, 7680);
            else if (arg == "--height") height = positiveNumber(value(), 480, 4320);
            else if (arg == "--frames") frames = positiveNumber(value(), 1, 10000000);
            else if (arg == "--screenshot-frame") screenshot = positiveNumber(value(), 1, 10000000);
            else if (arg == "--scenario") previewScenario = parseScenario(value());
            else if (arg == "--no-loop") previewLooping = false;
            else if (arg == "--classic") previewAppearance = CloudPreviewScene::Appearance::Original;
            else if (arg == "--volumetric") previewAppearance = CloudPreviewScene::Appearance::Volumetric;
            else if (arg == "--no-interaction") previewInteraction = false;
            else if (arg == "--no-heat") previewHeat = false;
            else if (arg == "--no-color-panel") previewColorPanel = false;
            else if (arg == "--hidden") hidden = true;
            else if (arg == "--offscreen") offscreen = true;
            else throw std::runtime_error("unknown argument: " + arg);
        }
        if (screenshot && frames && screenshot >= frames)
            throw std::runtime_error("--screenshot-frame must be earlier than --frames");
        char homeBuffer[4096];
        const std::string taskHome = findHomeDirectory(homeBuffer, sizeof(homeBuffer)) ? homeBuffer : "";
        const std::filesystem::path originalRegistry = playerRegistry(taskHome);
        PosixInstallRequest request = {};
        request.insideAppBundle = true; // Enable the existing Steam/known-location search.
        request.home = taskHome;
        request.registryFile = originalRegistry.string();
        request.chooser = nullptr;
        if (!data.empty()) request.arguments = { "-root", data };
        PosixInstallChoice choice;
        if (!PosixChooseInstallRoot(request, choice))
            throw std::runtime_error("game assets not found; pass --data followed by your Zero Hour folder");
        data = choice.root;
        const PosixInstallCheck check = PosixCheckInstallFolderWith(data, {}, originalRegistry.string());
        if (check != INSTALL_OK) throw std::runtime_error(PosixInstallCheckMessage(check, data));
        std::filesystem::create_directories(userDir);
        if (!CloudPreviewColors::initialize(userDir.string()))
            std::fprintf(stderr, "cloud_preview: %s; using default colors\n", CloudPreviewColors::lastStatus().c_str());
        std::fprintf(stderr, "cloud_preview: palette file %s\n", CloudPreviewColors::savePath().c_str());
        copyBaseGameLocation(originalRegistry, userDir / "Registry.ini");
        // Release builds index user maps, but do not discover new loose standard
        // maps from an overlay. Keep this generated fixture in our own user root.
        const std::filesystem::path mapPath = userDir / "Maps/CloudPreview/CloudPreview.map";
        std::filesystem::create_directories(mapPath.parent_path());
        std::filesystem::copy_file(std::filesystem::path(CLOUD_PREVIEW_DIRECTORY)
            / "overlay/Maps/CloudPreview/CloudPreview.map", mapPath,
            std::filesystem::copy_options::overwrite_existing);
        if (setenv("ZH_USER_DATA_DIR", userDir.c_str(), 1) != 0)
            throw std::runtime_error("cannot select preview data directory");
        std::vector<std::string> arguments = {
            argv[0], "-root", data,
            "-overlay", std::string(CLOUD_PREVIEW_DIRECTORY) + "/overlay",
            "-overlay", CLOUD_PREVIEW_GAME_OVERLAY,
            "-win", "-xres", std::to_string(width), "-yres", std::to_string(height),
            "-quickstart", "-noshellmap", "-noaudio", "-multiInstance",
            "-takeover", "-observer", "-side", "0", "FactionAmerica",
            "-side", "1", "FactionChina", "-seed", "7", "-fps", "60"
        };
        if (hidden) arguments.push_back("-hiddenwindow");
        if (offscreen) arguments.push_back("-offscreen");
        previewFrameLimit = frames;
        if (screenshot) { arguments.push_back("-screenshot"); arguments.push_back(std::to_string(screenshot)); }
        std::vector<char *> raw;
        for (std::string &arg : arguments) raw.push_back(arg.data());
        raw.push_back(nullptr);
        std::fprintf(stderr, "cloud_preview: assets %s\ncloud_preview: data %s\n", data.c_str(), userDir.c_str());
        const int result = CloudPreviewPlatformMain(static_cast<int>(arguments.size()), raw.data());
        return result ? result : (previewValid ? 0 : 1);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "cloud_preview: %s\n", error.what());
        return 1;
    }
}
