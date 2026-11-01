// Isolated 128-source cloud experiment using the ordinary engine bootstrap.
#define main CloudStressPlatformMain
#define CreateGameEngine CloudStressOriginalFactory
#include "../Main/PosixMain.cpp"
#undef CreateGameEngine
#undef main

#include "CloudStressScene.h"
#include "Common/GlobalData.h"
#include "GameClient/Display.h"
#include "GameClient/MapUtil.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "SdlDevice/GameClient/SdlInput.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

extern void GameEngine_startSkirmish(Int numPlayers);
namespace {
CloudStressScene::Settings stressSettings;
bool started = false, valid = false;
unsigned frameLimit = 0;
void updateTitle()
{
    if (SDL_Window *window = SdlInput_gameWindow()) {
        const std::string title = "Cloud stress | " + std::to_string(stressSettings.sources.count)
            + " sources | " + CloudStressScene::paletteName()
            + (stressSettings.reference ? " | production reference" : " | shared volume")
            + " | 1 green 2 blue 3 gamma 4 nuclear 5 mixed | R replay | Space pause | Esc close";
        SDL_SetWindowTitle(window, title.c_str());
    }
}
class CloudStressEngine : public SdlGameEngine {
public:
    explicit CloudStressEngine(const WindowRequest &request) : SdlGameEngine(request) {}
    void init(int argc, char **argv) override
    {
        SdlGameEngine::init(argc, argv);
        AsciiString map = TheMapCache->getUserMapDir();
        map.concat("\\CloudPreview\\CloudPreview.map");
        const MapMetaData *metadata = TheMapCache->findMap(map);
        if (!metadata || !metadata->m_isMultiplayer) {
            std::fprintf(stderr, "cloud_stress_preview: generated map was not registered: %s\n", map.str());
            setQuitting(TRUE); return;
        }
        TheWritableGlobalData->m_mapName = map;
        TheWritableGlobalData->m_autoSkirmishPlayers = 2;
        TheWritableGlobalData->m_showNetBox = FALSE;
        TheWritableGlobalData->m_healthBarMode = 0;
        TheWritableGlobalData->m_cloudStyle = 0;
        GameEngine_startSkirmish(2);
        startupTime = SDL_GetTicks();
    }
    ~CloudStressEngine() override
    {
        valid = started && CloudStressScene::validationPassed();
        CloudStressScene::shutdown();
    }
    void update() override
    {
        if (started && frameLimit && TheGameLogic->getFrame() >= frameLimit) {
            setQuitting(TRUE); return;
        }
        if (!started && TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame()
            && !TheGameLogic->isLoadingMap() && TheGameLogic->getFrame() > 0) {
            started = CloudStressScene::initialize(stressSettings);
            if (!started) { setQuitting(TRUE); return; }
            updateTitle();
        }
        if (!started && SDL_GetTicks() - startupTime > 30000) {
            std::fprintf(stderr, "cloud_stress_preview: timed out waiting for map\n");
            setQuitting(TRUE); return;
        }
        if (started) {
            CloudStressScene::update();
            if (!CloudStressScene::healthy()) { setQuitting(TRUE); return; }
        }
        SdlGameEngine::update();
    }
    void serviceWindowsOS() override
    {
        if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) return;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) setQuitting(TRUE);
            else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) setIsActive(TRUE);
            else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) setIsActive(FALSE);
            else if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
                && (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_SPACE || event.key.key == SDLK_R
                    || event.key.key == SDLK_F12 || (event.key.key >= SDLK_1 && event.key.key <= SDLK_5))) {
                if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) continue;
                if (event.key.key == SDLK_ESCAPE) setQuitting(TRUE);
                else if (started) {
                    if (event.key.key == SDLK_SPACE) TheGameLogic->setGamePaused(!TheGameLogic->isGamePaused(), FALSE);
                    else if (event.key.key == SDLK_R) CloudStressScene::replay();
                    else if (event.key.key == SDLK_F12) { if (TheDisplay) TheDisplay->takeScreenShot(); }
                    else {
                        const int palettes[] = {0, 2, 3, 1, -1};
                        CloudStressScene::setPalette(palettes[event.key.key - SDLK_1]);
                    }
                    updateTitle();
                }
            } else SdlInput_dispatch(event);
        }
    }
private:
    Uint64 startupTime = 0;
};
unsigned number(const std::string &value, unsigned low, unsigned high)
{
    char *end = nullptr;
    const unsigned long n = std::strtoul(value.c_str(), &end, 10);
    if (value.empty() || *end || value.front() == '-' || n < low || n > high)
        throw std::runtime_error("invalid number: " + value);
    return unsigned(n);
}
float realNumber(const std::string &value, float low, float high)
{
    char *end = nullptr; const float n = std::strtof(value.c_str(), &end);
    if (value.empty() || *end || !std::isfinite(n) || n < low || n > high)
        throw std::runtime_error("invalid number: " + value);
    return n;
}
int paletteNumber(const std::string &value)
{
    if (value == "mixed") return -1;
    if (value == "toxin") return 0;
    if (value == "anthrax") return 2;
    if (value == "gamma") return 3;
    if (value == "nuclear") return 1;
    throw std::runtime_error("invalid palette: " + value);
}
void printHelp()
{
    std::puts("Isolated visual-only 128-cloud stress scene.\n"
        "Usage: cloud_stress_preview [--game-data ZERO_HOUR_FOLDER] [--user-dir FOLDER]\n"
        "  [--fields N] [--palette mixed|toxin|anthrax|gamma|nuclear] [--layout grid|overlap]\n"
        "  [--radius N] [--fringe N] [--spacing N] [--columns N] [--zoom N] [--stagger SECONDS] [--lifetime SECONDS]\n"
        "  [--volume-side 256|512] [--slices N] [--reference] [--uncapped]\n"
        "  [--width N] [--height N] [--frames N] [--screenshot-frame N] [--record FIRST LAST]\n"
        "  [--hidden] [--offscreen] [--sync-bake] [--bake-slices N]\n"
        "  [--freeze-time SECONDS] [--replay-frame N] [--palette-at FRAME PALETTE]\n\n"
        "Default: 128 retained sources, mixed grid with 32 of each approved palette.\n"
        "Each source builds up and fades independently; R replays the release.\n"
        "1 green toxin | 2 blue anthrax | 3 purple gamma | 4 nuclear | 5 mixed\n"
        "Space pause | arrows pan | wheel zoom | middle mouse rotate | F12 screenshot | Esc quit\n"
        "--reference uses the production renderer and requires --fields 1 or 2.\n"
        "--uncapped enables turbo, uncapped rendering and unpaced offscreen presentation.\n"
        "Frames use engine logic time (60/second). Recording uses the engine's supported 60 fps.\n"
        "Sources have no gameplay objects, damage or Classic fallback. Logs/screenshots use a separate profile.");
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
GameEngine *CreateGameEngine() { return NEW CloudStressEngine(s_windowRequest); }
int main(int argc, char **argv)
{
    try {
        std::string data;
        std::filesystem::path userDir = std::filesystem::path(CLOUD_STRESS_DIRECTORY) / "user";
        unsigned width = 1280, height = 800, screenshot = 0, recordFirst = 0, recordLast = 0;
        bool hidden = false, offscreen = false, uncapped = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto value = [&]() -> std::string {
                if (++i == argc) throw std::runtime_error("missing value after " + arg);
                return argv[i];
            };
            if (arg == "--help" || arg == "-h") { printHelp(); return 0; }
            else if (arg == "--game-data" || arg == "--data") data = std::filesystem::absolute(value()).string();
            else if (arg == "--user-dir") userDir = std::filesystem::absolute(value());
            else if (arg == "--fields") stressSettings.sources.count = number(value(), 1, 4096);
            else if (arg == "--palette") stressSettings.sources.palette = paletteNumber(value());
            else if (arg == "--layout") {
                const std::string layout = value();
                if (layout == "grid") stressSettings.sources.layout = CloudStress::Layout::Grid;
                else if (layout == "overlap") stressSettings.sources.layout = CloudStress::Layout::Overlap;
                else throw std::runtime_error("invalid layout: " + layout);
            }
            else if (arg == "--radius") stressSettings.sources.radius = realNumber(value(), 1, 500);
            else if (arg == "--fringe") stressSettings.sources.fringe = realNumber(value(), 0, 500);
            else if (arg == "--zoom") stressSettings.zoom = realNumber(value(), 100, 5000);
            else if (arg == "--spacing") stressSettings.sources.spacing = realNumber(value(), 0, 1000);
            else if (arg == "--columns") stressSettings.sources.columns = number(value(), 0, 4096);
            else if (arg == "--stagger") stressSettings.sources.stagger = realNumber(value(), 0, 600);
            else if (arg == "--lifetime") stressSettings.sources.lifetime = realNumber(value(), .1f, 3600);
            else if (arg == "--volume-side") {
                stressSettings.volumeSide = number(value(), 256, 512);
                if (stressSettings.volumeSide != 256 && stressSettings.volumeSide != 512)
                    throw std::runtime_error("--volume-side must be 256 or 512");
            }
            else if (arg == "--slices") {
                stressSettings.slices = number(value(), 6, 96);
                if (stressSettings.slices % 6) throw std::runtime_error("--slices must be a multiple of 6 (6..96)");
            }
            else if (arg == "--reference") stressSettings.reference = true;
            else if (arg == "--sync-bake") stressSettings.incrementalBaking = false;
            else if (arg == "--bake-slices") stressSettings.bakeSlicesPerFrame = number(value(), 1, 96);
            else if (arg == "--freeze-time") stressSettings.freezeTime = realNumber(value(), 0, 10000);
            else if (arg == "--replay-frame") stressSettings.replayFrame = number(value(), 1, 10000000);
            else if (arg == "--palette-at") {
                stressSettings.paletteFrame = number(value(), 1, 10000000);
                stressSettings.nextPalette = paletteNumber(value());
            }
            else if (arg == "--uncapped") uncapped = true;
            else if (arg == "--width") width = number(value(), 640, 7680);
            else if (arg == "--height") height = number(value(), 480, 4320);
            else if (arg == "--frames") frameLimit = number(value(), 2, 10000000);
            else if (arg == "--screenshot-frame") screenshot = number(value(), 1, 10000000);
            else if (arg == "--record") { recordFirst = number(value(), 1, 10000000); recordLast = number(value(), recordFirst, 10000000); }
            else if (arg == "--hidden") hidden = true;
            else if (arg == "--offscreen") offscreen = true;
            else throw std::runtime_error("unknown argument: " + arg);
        }
        if (stressSettings.reference && stressSettings.sources.count > 2)
            throw std::runtime_error("--reference requires --fields 1 or 2");
        if ((screenshot && frameLimit && screenshot >= frameLimit)
            || (recordLast && frameLimit && recordLast >= frameLimit))
            throw std::runtime_error("capture frames must precede --frames");
        char homeBuffer[4096];
        const std::string taskHome = findHomeDirectory(homeBuffer, sizeof(homeBuffer)) ? homeBuffer : "";
        const auto originalRegistry = playerRegistry(taskHome);
        PosixInstallRequest request = {};
        request.insideAppBundle = true; request.home = taskHome;
        request.registryFile = originalRegistry.string(); request.chooser = nullptr;
        if (!data.empty()) request.arguments = {"-root", data};
        PosixInstallChoice choice;
        if (!PosixChooseInstallRoot(request, choice))
            throw std::runtime_error("game assets not found; pass --game-data followed by your Zero Hour folder");
        data = choice.root;
        const auto check = PosixCheckInstallFolderWith(data, {}, originalRegistry.string());
        if (check != INSTALL_OK) throw std::runtime_error(PosixInstallCheckMessage(check, data));
        std::filesystem::create_directories(userDir);
        copyBaseGameLocation(originalRegistry, userDir / "Registry.ini");
        const auto mapPath = userDir / "Maps/CloudPreview/CloudPreview.map";
        std::filesystem::create_directories(mapPath.parent_path());
        std::filesystem::copy_file(std::filesystem::path(CLOUD_STRESS_DIRECTORY)
            / "overlay/Maps/CloudPreview/CloudPreview.map", mapPath, std::filesystem::copy_options::overwrite_existing);
        if (setenv("ZH_USER_DATA_DIR", userDir.c_str(), 1) != 0) throw std::runtime_error("cannot select stress profile");
        std::vector<std::string> arguments = {
            argv[0], "-root", data, "-overlay", std::string(CLOUD_STRESS_DIRECTORY) + "/overlay",
            "-overlay", CLOUD_STRESS_GAME_OVERLAY,
            "-win", "-xres", std::to_string(width), "-yres", std::to_string(height),
            "-quickstart", "-noshellmap", "-noaudio", "-multiInstance", "-takeover", "-observer",
            "-side", "0", "FactionAmerica", "-side", "1", "FactionChina", "-seed", "7", "-fps", "60"
        };
        if (hidden) arguments.push_back("-hiddenwindow");
        if (offscreen) arguments.push_back("-offscreen");
        if (uncapped) {
            arguments.insert(arguments.end(), {"-turbo", "-noFPSLimit"});
            if (setenv("ZH_OFFSCREEN_HZ", "0", 1) != 0) throw std::runtime_error("cannot select unpaced offscreen mode");
        }
        if (screenshot) arguments.insert(arguments.end(), {"-screenshot", std::to_string(screenshot)});
        if (recordLast) arguments.insert(arguments.end(), {"-video", std::to_string(recordFirst), std::to_string(recordLast), "cloud-stress", "-recordfps", "60"});
        std::vector<char *> raw;
        for (auto &arg : arguments) raw.push_back(arg.data()); raw.push_back(nullptr);
        std::fprintf(stderr, "cloud_stress_preview: assets %s\ncloud_stress_preview: profile %s\n", data.c_str(), userDir.c_str());
        const int result = CloudStressPlatformMain(int(arguments.size()), raw.data());
        return result ? result : (valid ? 0 : 1);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "cloud_stress_preview: %s\n", error.what()); return 1;
    }
}
