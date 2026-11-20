/* Native optional HD artwork manager. The worker owns files; this file only presents snapshots. */
#include "PreRTS.h"
#include "Common/HDAssetsManager.h"
#include "Common/GlobalData.h"
#include "Common/EarlyCommandLine.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/GameText.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetProgressBar.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Shell.h"
#include "GameLogic/GameLogic.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#ifdef __linux__
#include <SDL3/SDL.h>
#endif

#ifdef __linux__

namespace {
using HDAssets::PackId;
using HDAssets::PackSnapshot;
using HDAssets::PackState;
using HDAssets::manager;
enum Page { PACKS, SOURCES, INVITATION, REMOVAL, DETAILS };
WindowLayout *menu = NULL;
GameWindow *returnFocus = NULL;
GameWinDrawFunc nativeDraw = NULL;
Page page = PACKS;
HDAssets::Snapshot view;
int sourceOffset = 1, editSource = -1, removePack = -1;
std::string localError;
std::chrono::steady_clock::time_point lastRefresh;
const char *pages[] = { "Packs", "Sources", "Invitation", "Removal", "Details" };
const char *titles[] = { "Manage", "Sources", "Welcome", "RemoveTitle", "Details" };
const char *titleFallbacks[] = { "Manage HD Assets", "Download Sources", "Optional HD Assets", "Remove HD Assets", "Details" };

UnicodeString unicode(const std::string &value)
{
    UnicodeString result;
    result.translate(AsciiString(value.c_str()));
    return result;
}
UnicodeString tr(const char *name, const char *fallback)
{
    std::string key = std::string("GUI:HDAssets") + name;
    Bool found = FALSE;
    UnicodeString result;
    if (TheGameText) result = TheGameText->fetch(key.c_str(), &found);
    return found ? result : unicode(fallback);
}
GameWindow *control(const std::string &name)
{
    return menu ? TheWindowManager->winGetWindowFromId(menu->getFirstWindow(),
        TheNameKeyGenerator->nameToKey(AsciiString(("HDAssetsMenu.wnd:" + name).c_str()))) : NULL;
}
void label(const std::string &name, const UnicodeString &value)
{
    if (GameWindow *w = control(name)) {
        if (w->winGetText() != value) GadgetStaticTextSetText(w, value);
    }
}
void caption(const std::string &name, const char *key, const char *fallback)
{
    if (GameWindow *w = control(name)) {
        const UnicodeString value = tr(key, fallback);
        if (w->winGetText() != value) w->winSetText(value);
    }
}
void hide(const std::string &name, bool hidden)
{
    if (GameWindow *w = control(name)) w->winHide(hidden);
}
void enable(const std::string &name, bool enabled)
{
    if (GameWindow *w = control(name)) w->winEnable(enabled);
}
void refresh();
void show(Page next)
{
    page = next;
    for (int i = 0; i < 5; ++i) hide(pages[i], i != page);
    hide("Panel", page == INVITATION);
    hide("InvitationPanel", page != INVITATION);
    label(page == INVITATION ? "InvitationTitle" : "Title", tr(titles[page], titleFallbacks[page]));
    lastRefresh = std::chrono::steady_clock::time_point();
    refresh();
}
void close()
{
    if (!menu) return;
    TheWindowManager->winUnsetModal(menu->getFirstWindow());
    WindowLayout *old = menu;
    menu = NULL;
    old->destroyWindows();
    old->deleteInstance();
    TheWindowManager->winSetFocus(returnFocus);
    returnFocus = NULL;
}
void details(const std::string &error)
{
    localError = error;
    label("DetailsText", unicode(error));
    show(DETAILS);
}
const char *stateKey(PackState state)
{
    switch (state) {
    case PackState::Missing: return "Missing";
    case PackState::Checking: return "Checking";
    case PackState::Queued: return "Queued";
    case PackState::Downloading: return "Downloading";
    case PackState::Paused: return "Paused";
    case PackState::Verifying: return "Verifying";
    case PackState::InstalledManaged: return "Installed";
    case PackState::InstalledManual: return "Manual";
    case PackState::RemovalPending: return "RemovalPending";
    case PackState::Error: return "Error";
    }
    return "Missing";
}
std::string amount(uint64_t bytes)
{
    char out[64];
    if (bytes >= 1000000000ULL) std::snprintf(out, sizeof(out), "%.2f GB", bytes / 1000000000.0);
    else std::snprintf(out, sizeof(out), "%.1f MB", bytes / 1000000.0);
    return out;
}
void refresh()
{
    if (!menu) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - lastRefresh < std::chrono::milliseconds(250)) return;
    lastRefresh = now;
    const bool wasBusy = view.busy;
    view = manager().snapshot(); // mutex-protected values only: never scan files on the UI thread
    if (page == PACKS) {
        bool paused = false, restart = false;
        uint64_t done = 0, total = 0;
        for (size_t i = 0; i < view.packs.size() && i < 4; ++i) {
            const PackSnapshot &p = view.packs[i];
            const std::string n = std::to_string(i);
            const char *nameKeys[] = {"Textures", "Units", "Maps", "Skins"};
            label("PackName" + n, tr(nameKeys[i], p.title.c_str()));
            label("PackSize" + n, unicode(p.displaySize));
            UnicodeString status = tr(stateKey(p.state), stateKey(p.state));
            if ((p.state == PackState::Downloading || p.state == PackState::Paused) && p.total) {
                status.concat(unicode("  " + amount(p.downloaded) + " / " + amount(p.total)));
            }
            label("PackState" + n, status);
            const bool managed = p.state == PackState::InstalledManaged;
            const bool manual = p.state == PackState::InstalledManual;
            const bool subsetRemoval = p.removableBytes > 0 && !managed && p.state != PackState::RemovalPending;
            hide("PackRemoveOwned" + n, !subsetRemoval);
            enable("PackRemoveOwned" + n, subsetRemoval && !view.busy);
            if (GameWindow *action = control("PackAction" + n)) {
                Int w, h;
                action->winGetSize(&w, &h);
                action->winSetSize(w, (Int)((subsetRemoval ? 20 : 28) * w / 126.0f + 0.5f));
            }
            caption("PackAction" + n, manual ? "OpenFolder" : managed ? "Remove" : "Download",
                manual ? "Open Folder" : managed ? "Remove" : "Download");
            enable("PackAction" + n, manual || (!view.busy &&
                (managed || p.state == PackState::Missing || p.state == PackState::Error)));
            paused |= p.state == PackState::Paused;
            restart |= p.restartRequired || p.state == PackState::RemovalPending;
            if (p.state == PackState::Downloading || p.state == PackState::Paused ||
                p.state == PackState::Queued || p.state == PackState::Verifying) {
                done += p.downloaded; total += p.total;
            }
        }
        caption("Pause", paused ? "Resume" : "Pause", paused ? "Resume" : "Pause");
        hide("Pause", !view.busy);
        hide("CancelDownload", !view.busy);
        hide("FullSet", view.busy);
        enable("SourcesButton", !view.busy);
        hide("ErrorDetails", view.error.empty() && localError.empty());
        label("Progress", view.busy ? unicode(amount(done) + " / " + amount(total)) : unicode(""));
        hide("ProgressBar", !view.busy);
        if (GameWindow *bar = control("ProgressBar"))
            GadgetProgressBarSetProgress(bar, total ? (int)(100.0 * done / total) : 0);
        label("Note", restart ? tr("RestartRequired", "Restart the game to apply HD asset changes.") :
            tr("RestartNote", "New packs are used after restarting the game."));
        if (wasBusy && !view.busy && restart && view.error.empty()) {
            label("DetailsText", tr("RestartPrompt", "HD asset changes are ready. Restart the game when convenient to apply them.\n\nFor HD textures and models, turn off Classic Graphics under Options > Graphics before restarting."));
            show(DETAILS);
            label("Title", tr("RestartRequiredTitle", "Restart Required"));
        }
    } else if (page == SOURCES) {
        enable("Default", false); // immutable official source; never offered as an editable row
        if (sourceOffset >= (int)view.sources.size()) sourceOffset = std::max(1, sourceOffset - 3);
        for (int row = 0; row < 3; ++row) {
            const int index = sourceOffset + row;
            bool exists = index < (int)view.sources.size();
            std::string value = exists ? view.sources[index] : "";
            if (value.size() > 49) value = value.substr(0, 46) + "...";
            label("Source" + std::to_string(row), unicode(value));
            hide("EditSource" + std::to_string(row), !exists);
            hide("RemoveSource" + std::to_string(row), !exists);
            enable("EditSource" + std::to_string(row), editSource < 0);
            enable("RemoveSource" + std::to_string(row), editSource < 0);
        }
        enable("PreviousSources", sourceOffset > 1 && editSource < 0);
        enable("NextSources", sourceOffset + 3 < (int)view.sources.size() && editSource < 0);
        hide("CancelEdit", editSource < 0);
        caption("SaveSource", editSource >= 0 ? "SaveChanges" : "AddMirror",
            editSource >= 0 ? "Save Changes" : "Add Mirror");
        label("SourceNote", localError.empty() ? tr("Fallback", "Mirrors are tried if the official server is unavailable.") : unicode(localError));
    }
}
void draw(GameWindow *window, WinInstanceData *data)
{
    refresh();
    if (nativeDraw) nativeDraw(window, data);
}
void resetEdit()
{
    editSource = -1;
    localError.clear();
    if (GameWindow *entry = control("MirrorUrl")) GadgetTextEntrySetText(entry, unicode("https://"));
}
void start(const std::vector<PackId> &packs)
{
    std::string error;
    if (!manager().download(packs, error)) details(error);
    else { localError.clear(); show(PACKS); }
}
std::string fileUri(const std::string &path)
{
    const char digits[] = "0123456789ABCDEF";
    std::string result = "file://";
    for (unsigned char c : path) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') result += c;
        else { result += '%'; result += digits[c >> 4]; result += digits[c & 15]; }
    }
    return result;
}
} // namespace

bool HDAssetsMenuAvailable()
{
#ifdef __linux__
    // An explicit overlay replaces implicit ReforgedArt discovery. Downloading here
    // would produce packs that this launch mode deliberately never mounts.
    if (explicitOverlayByCommandLine()) return false;
    return TheGameLogic && (!TheGameLogic->isInGame() || TheGameLogic->isInShellGame()) &&
        TheGlobalData && !TheGlobalData->m_headless && !HDAssets::manager().snapshot().artDirectory.empty();
#else
    return false;
#endif
}
void HDAssetsMenuOpen(bool invitation)
{
    if (menu || !HDAssetsMenuAvailable()) return;
    returnFocus = TheWindowManager->winGetFocus();
    menu = TheWindowManager->winCreateLayout(AsciiString("Menus/HDAssetsMenu.wnd"));
    if (!menu) return;
    menu->runInit();
    show(invitation ? INVITATION : PACKS);
    menu->hide(FALSE);
    menu->bringForward();
    TheWindowManager->winSetModal(menu->getFirstWindow());
    TheWindowManager->winSetFocus(menu->getFirstWindow());
}
void HDAssetsMenuOfferInvitation(WindowLayout *layout)
{
#ifdef __linux__
    if (menu || !layout || layout->isHidden() || !TheGlobalData ||
        TheGlobalData->m_headless || !TheGlobalData->m_initialFile.isEmpty() ||
        TheGlobalData->m_autoSkirmishPlayers > 0 || TheGlobalData->m_maxGameFrames > 0 ||
        TheGlobalData->m_directorRecord || TheGlobalData->m_quickMatchOnStart ||
        !TheGlobalData->m_onlineHostGame.isEmpty() || TheGlobalData->m_optionsMenuOnStart || !TheShell || TheShell->top() != layout ||
        !TheShell->isAnimFinished() || !HDAssetsMenuAvailable()) return;
    GameWindow *options = TheWindowManager->winGetWindowFromId(NULL,
        TheNameKeyGenerator->nameToKey(AsciiString("OptionsMenu.wnd:")));
    if (options && !options->winIsHidden()) return;
    if (HDAssets::manager().invitationNeeded()) HDAssetsMenuOpen(true);
#endif
}
void HDAssetsMenuInit(WindowLayout *layout, void *)
{
    menu = layout;
    nativeDraw = menu->getFirstWindow()->winGetDrawFunc();
    menu->getFirstWindow()->winSetDrawFunc(draw);
    resetEdit();
    localError.clear();
}
void HDAssetsMenuUpdate(WindowLayout *, void *) { refresh(); }
void HDAssetsMenuShutdown(WindowLayout *, void *) { close(); }
WindowMsgHandledType HDAssetsMenuInput(GameWindow *, UnsignedInt msg, WindowMsgData one, WindowMsgData two)
{
    if (msg == GWM_CHAR && one == KEY_ESC && BitTest(two, KEY_STATE_UP)) {
        if (page == INVITATION) { manager().dismissInvitation(); close(); }
        else if (page == SOURCES && editSource >= 0) { resetEdit(); show(SOURCES); }
        else if (page != PACKS) show(PACKS);
        else close();
        return MSG_HANDLED;
    }
    return MSG_IGNORED;
}
WindowMsgHandledType HDAssetsMenuSystem(GameWindow *, UnsignedInt msg, WindowMsgData one, WindowMsgData two)
{
    if (msg == GWM_INPUT_FOCUS) {
        if (one == TRUE) *(Bool *)two = TRUE;
        return MSG_HANDLED;
    }
    if (msg != GBM_SELECTED || !one || !menu) return MSG_IGNORED;
    GameWindow *clicked = (GameWindow *)one;
    auto selected = [clicked](const std::string &name) { return clicked == control(name); };
    if (selected("Back")) close();
    else if (selected("SourcesButton")) { sourceOffset = 1; resetEdit(); show(SOURCES); }
    else if (selected("SourcesBack")) { resetEdit(); show(PACKS); }
    else if (selected("DetailsBack")) show(PACKS);
    else if (selected("ErrorDetails")) details(view.error.empty() ? localError : view.error);
    else if (selected("FullSet")) start({PackId::Textures, PackId::Units, PackId::Maps, PackId::Skins});
    else if (selected("Pause")) {
        const bool paused = std::any_of(view.packs.begin(), view.packs.end(),
            [](const PackSnapshot &p) { return p.state == PackState::Paused; });
        if (paused) manager().resume(); else manager().pause();
    } else if (selected("CancelDownload")) manager().cancel();
    else if (selected("InviteSkip")) { manager().dismissInvitation(); close(); }
    else if (selected("InviteChoose")) { manager().dismissInvitation(); show(PACKS); }
    else if (selected("InviteTextures")) { manager().dismissInvitation(); start({PackId::Textures}); }
    else if (selected("RemovalCancel")) show(PACKS);
    else if (selected("RemovalConfirm") && removePack >= 0 && removePack < (int)view.packs.size()) {
        std::string error;
        if (!manager().scheduleRemoval(view.packs[removePack].id, error)) details(error);
        else { show(PACKS); }
    } else if (selected("CancelEdit")) { resetEdit(); show(SOURCES); }
    else if (selected("PreviousSources")) { sourceOffset = std::max(1, sourceOffset - 3); show(SOURCES); }
    else if (selected("NextSources")) { sourceOffset += 3; show(SOURCES); }
    else if (selected("SaveSource")) {
        AsciiString entry;
        entry.translate(GadgetTextEntryGetText(control("MirrorUrl")));
        std::vector<std::string> sources = view.sources;
        if (editSource >= 1 && editSource < (int)sources.size()) sources[editSource] = entry.str();
        else sources.push_back(entry.str());
        std::string error;
        if (manager().setSources(sources, error)) resetEdit(); else localError = error;
        show(SOURCES);
    } else {
        for (int i = 0; i < 3; ++i) {
            const int index = sourceOffset + i;
            if (index >= (int)view.sources.size()) continue;
            if (selected("EditSource" + std::to_string(i))) {
                editSource = index;
                GadgetTextEntrySetText(control("MirrorUrl"), unicode(view.sources[index]));
                TheWindowManager->winSetFocus(control("MirrorUrl"));
                show(SOURCES);
                return MSG_HANDLED;
            }
            if (selected("RemoveSource" + std::to_string(i))) {
                auto sources = view.sources;
                sources.erase(sources.begin() + index);
                std::string error;
                if (!manager().setSources(sources, error)) localError = error;
                show(SOURCES);
                return MSG_HANDLED;
            }
        }
        for (size_t i = 0; i < view.packs.size() && i < 4; ++i) {
            const bool removeOwned = selected("PackRemoveOwned" + std::to_string(i));
            if (!removeOwned && !selected("PackAction" + std::to_string(i))) continue;
            const auto &pack = view.packs[i];
            if ((removeOwned || pack.state == PackState::InstalledManaged) && pack.removableBytes > 0) {
                removePack = (int)i;
                UnicodeString message = unicode(pack.title + " (" + amount(pack.removableBytes) + ")\n\n");
                message.concat(tr("RemovalText", "The downloaded files will be removed at the next game restart. Manually installed files are preserved. Restart the game yourself when ready."));
                label("RemovalText", message);
                show(REMOVAL);
            } else if (pack.state == PackState::InstalledManual) {
#ifdef __linux__
                if (!SDL_OpenURL(fileUri(view.artDirectory).c_str())) details(SDL_GetError());
#endif
            } else start({pack.id});
            return MSG_HANDLED;
        }
    }
    if (menu) { lastRefresh = std::chrono::steady_clock::time_point(); refresh(); }
    return MSG_HANDLED;
}

#else
// The asset backend is Linux-only. Keep lexicon targets harmless on other platforms.
bool HDAssetsMenuAvailable() { return false; }
void HDAssetsMenuOpen(bool) {}
void HDAssetsMenuOfferInvitation(WindowLayout *) {}
void HDAssetsMenuInit(WindowLayout *, void *) {}
void HDAssetsMenuUpdate(WindowLayout *, void *) {}
void HDAssetsMenuShutdown(WindowLayout *, void *) {}
WindowMsgHandledType HDAssetsMenuInput(GameWindow *, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType HDAssetsMenuSystem(GameWindow *, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
#endif
