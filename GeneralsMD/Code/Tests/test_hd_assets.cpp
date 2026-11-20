// Deterministic storage/ownership fixtures: HD_ASSETS_TESTING uses three-byte SHA-256-pinned
// archives and substitutes only the network entry point. No game/user data or network is used.
#include "Common/HDAssetsManager.h"
#include "Common/HDAssetsTransport.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace HDAssets;
namespace fs = std::filesystem;
#define REQUIRE(x) do { if (!(x)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x); } while (0)
namespace {
std::mutex callsMutex;
std::vector<std::string> calls;
std::atomic<int> mode{0}, offsets{0}; // 1 official failure, 2 corruption once, 3 slow transfer
struct Temp {
    std::string path;
    Temp() { char p[] = "/tmp/zh-hd-assets-test-XXXXXX"; char *made = mkdtemp(p); REQUIRE(made); path = made; }
    ~Temp() { fs::remove_all(path); }
    std::string art() const { return path + "/ReforgedArt"; }
};
void put(const std::string &path, const std::string &data) { std::ofstream f(path); f << data; REQUIRE(f.good()); }
std::string get(const std::string &path) { std::ifstream f(path); return std::string(std::istreambuf_iterator<char>(f), {}); }
Snapshot idle(Manager &m) {
    for (unsigned i = 0; i < 500; ++i) { auto s = m.snapshot(); if (!s.busy) return s; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    throw std::runtime_error("manager did not become idle");
}
PackSnapshot pack(Manager &m, PackId id) { return m.snapshot().packs[static_cast<size_t>(id)]; }
void resetCalls(int value) { std::lock_guard<std::mutex> g(callsMutex); calls.clear(); mode = value; offsets = 0; }
}
namespace HDAssets {
bool fixtureFetch(const std::string &source, const std::string &name, uint64_t total, int fd,
    const std::function<bool()> &proceed, const std::function<void(uint64_t)> &progress, std::string &error)
{
    { std::lock_guard<std::mutex> g(callsMutex); calls.push_back(source); }
    if (mode == 4 && name == "ReforgedTextures.big") { error = "fixture second archive failure"; return false; }
    if (mode == 1 && source.find("zerohour.gg") != std::string::npos) { error = "fixture official failure"; return false; }
    struct stat s{}; REQUIRE(!fstat(fd, &s)); offsets = int(s.st_size);
    REQUIRE(total == 3); REQUIRE(lseek(fd, s.st_size, SEEK_SET) >= 0);
    const char *bytes = mode.exchange(mode == 2 ? 0 : mode.load()) == 2 ? "bad" : "abc";
    for (uint64_t i = s.st_size; i < total; ++i) {
        if (mode == 3) std::this_thread::sleep_for(std::chrono::milliseconds(90));
        if (!proceed()) { error = "fixture interrupted"; return false; }
        REQUIRE(write(fd, bytes + i, 1) == 1); progress(i + 1);
    }
    error.clear(); return true;
}
}
static void responseTests() {
    Transport::URL url; std::string error; bool restart = false;
    REQUIRE(Transport::parseFolder("https://example.org:8443/releases/v4.2.0", url, error));
    REQUIRE(url.path == "/releases/v4.2.0/" && url.port == "8443");
    Transport::URL destination;
    REQUIRE(Transport::redirectTarget(url, "https://cdn.test/file.big?signature=abc%2Fxyz&expires=42", destination, error));
    REQUIRE(destination.host == "cdn.test" && destination.path == "/file.big?signature=abc%2Fxyz&expires=42");
    REQUIRE(Transport::redirectTarget(url, "/file.big?signature=abc", destination, error));
    REQUIRE(destination.host == "example.org" && destination.port == "8443");
    for (const auto *bad : {"http://cdn.test/a", "//cdn.test/a", "https://user@cdn.test/a", "https://cdn.test/a#fragment", "https://cdn.test/a\r\nX: a"}) REQUIRE(!Transport::redirectTarget(url, bad, destination, error));
    for (const auto *bad : {"http://example.org/", "https://x@y/", "https://example.org/?token=x", "https://example.org/#x", "https://example.org:0/", "https://example.org/%zz", "https://example.org/\r\nx", "https://example.org\\x/"}) REQUIRE(!Transport::parseFolder(bad, url, error));
    REQUIRE(Transport::validateResponse("HTTP/1.1 206 Partial Content\r\nContent-Length: 2\r\nContent-Range: bytes 1-2/3\r\n\r\n", 3, 1, restart, error));
    REQUIRE(!restart);
    REQUIRE(Transport::validateResponse("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n", 3, 1, restart, error)); REQUIRE(restart);
    for (const auto *bad : {"HTTP/1.1 206 Partial Content\r\nContent-Length: 2\r\nContent-Range: bytes 0-1/3\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 3\r\ncontent-length: 3\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\n", "HTTP/1.1 302 Found\r\nContent-Length: 3\r\n\r\n"}) REQUIRE(!Transport::validateResponse(bad, 3, 1, restart, error));
}
static void ownershipAndRestart() {
    Temp t; std::string error; resetCalls(0);
    Manager first; first.initialize(t.path); REQUIRE(idle(first).error.empty());
    REQUIRE(first.invitationNeeded()); first.dismissInvitation(); REQUIRE(!first.invitationNeeded());
    REQUIRE(first.setSources({"https://mirror.test/v4.2.0", "https://second.test/assets/"}, error));
    REQUIRE(first.snapshot().sources.size() == 3); REQUIRE(first.snapshot().sources[1] == "https://mirror.test/v4.2.0/");
    REQUIRE(first.download({PackId::Textures}, error)); idle(first);
    REQUIRE(pack(first, PackId::Textures).state == PackState::InstalledManaged);
    REQUIRE(pack(first, PackId::Textures).restartRequired);
    REQUIRE(first.scheduleRemoval(PackId::Textures, error));
    REQUIRE(fs::exists(t.art() + "/ReforgedTerrain.big"));
    Manager reader; reader.initialize(t.path); idle(reader);
    REQUIRE(fs::exists(t.art() + "/ReforgedTerrain.big"));
    REQUIRE(!reader.download({PackId::Skins}, error));
    first.shutdown();
    Manager blocked; blocked.initialize(t.path); idle(blocked);
    REQUIRE(fs::exists(t.art() + "/ReforgedTerrain.big")); // reader's shared lifetime lock
    REQUIRE(pack(blocked, PackId::Textures).state == PackState::RemovalPending);
    blocked.shutdown(); reader.shutdown();
    Manager next; next.initialize(t.path); idle(next);
    REQUIRE(!fs::exists(t.art() + "/ReforgedTerrain.big"));
    REQUIRE(!fs::exists(t.art() + "/ReforgedTextures.big"));
    REQUIRE(!next.invitationNeeded()); REQUIRE(next.snapshot().sources.size() == 3);
}
static void manualAndReplacement() {
    std::string error;
    Temp manual; fs::create_directory(manual.art()); put(manual.art() + "/ReforgedSkins.big", "abc");
    Manager m; m.initialize(manual.path); idle(m);
    REQUIRE(pack(m, PackId::Skins).state == PackState::InstalledManual);
    REQUIRE(!m.invitationNeeded()); REQUIRE(!m.scheduleRemoval(PackId::Skins, error));
    m.shutdown(); REQUIRE(get(manual.art() + "/ReforgedSkins.big") == "abc");
    Temp owned; Manager a; a.initialize(owned.path); idle(a); REQUIRE(a.download({PackId::Skins}, error)); idle(a);
    REQUIRE(a.scheduleRemoval(PackId::Skins, error));
    put(owned.art() + "/replacement", "abc"); fs::rename(owned.art() + "/replacement", owned.art() + "/ReforgedSkins.big");
    a.shutdown(); Manager b; b.initialize(owned.path); idle(b);
    REQUIRE(pack(b, PackId::Skins).state == PackState::InstalledManual);
    REQUIRE(get(owned.art() + "/ReforgedSkins.big") == "abc");
    REQUIRE(!b.scheduleRemoval(PackId::Skins, error));
}
static void fallbackAndSafety() {
    Temp t; std::string error; Manager m; m.initialize(t.path); idle(m);
    REQUIRE(m.setSources({"https://mirror.test/v4.2.0/"}, error)); resetCalls(1);
    REQUIRE(m.download({PackId::Skins}, error)); idle(m);
    REQUIRE(pack(m, PackId::Skins).state == PackState::InstalledManaged);
    { std::lock_guard<std::mutex> g(callsMutex); REQUIRE(calls.size() == 3); REQUIRE(calls[0].find("zerohour.gg") != std::string::npos); REQUIRE(calls[1] == calls[0]); REQUIRE(calls[2] == "https://mirror.test/v4.2.0/"); }
    resetCalls(2); REQUIRE(m.download({PackId::Textures}, error)); idle(m);
    REQUIRE(pack(m, PackId::Textures).state == PackState::InstalledManaged); // corrupt official bytes discarded, retried
    REQUIRE(get(t.art() + "/ReforgedTerrain.big") == "abc");
    m.shutdown();
    Temp symlink; Manager n; n.initialize(symlink.path); idle(n); put(symlink.path + "/outside", "keep");
    fs::create_symlink(symlink.path + "/outside", symlink.art() + "/.hd-assets-v420/ReforgedSkins.big.part");
    resetCalls(0); REQUIRE(n.download({PackId::Skins}, error)); idle(n);
    REQUIRE(pack(n, PackId::Skins).state == PackState::Error); REQUIRE(get(symlink.path + "/outside") == "keep");
    REQUIRE(!fs::exists(symlink.art() + "/ReforgedSkins.big"));
    Temp linked; fs::create_directory(linked.art()); fs::create_directory(linked.path + "/elsewhere"); fs::create_directory_symlink(linked.path + "/elsewhere", linked.art() + "/.hd-assets-v420");
    Manager bad; bad.initialize(linked.path); REQUIRE(!bad.snapshot().error.empty()); REQUIRE(!bad.invitationNeeded());
    Temp artLink; fs::create_directory(artLink.path + "/manual-target"); put(artLink.path + "/manual-target/ReforgedSkins.big", "abc");
    fs::create_directory_symlink(artLink.path + "/manual-target", artLink.art());
    Manager ro; ro.initialize(artLink.path); idle(ro);
    REQUIRE(ro.snapshot().artDirectory == artLink.path + "/manual-target");
    REQUIRE(pack(ro, PackId::Skins).state == PackState::InstalledManual);
    REQUIRE(!ro.download({PackId::Textures}, error)); REQUIRE(!ro.scheduleRemoval(PackId::Skins, error));
    REQUIRE(!fs::exists(artLink.path + "/manual-target/.hd-assets-v420"));
}
static void partialMixedAndFailedSave() {
    std::string error;
    Temp partial; Manager m; m.initialize(partial.path); idle(m); resetCalls(4);
    REQUIRE(m.download({PackId::Textures}, error)); idle(m);
    REQUIRE(pack(m, PackId::Textures).state == PackState::Error);
    REQUIRE(pack(m, PackId::Textures).removableBytes == 3);
    REQUIRE(m.scheduleRemoval(PackId::Textures, error));
    REQUIRE(fs::exists(partial.art() + "/ReforgedTerrain.big")); m.shutdown();
    Manager next; next.initialize(partial.path); idle(next); REQUIRE(!fs::exists(partial.art() + "/ReforgedTerrain.big")); next.shutdown();

    Temp mixed; fs::create_directory(mixed.art()); put(mixed.art() + "/ReforgedTerrain.big", "abc");
    Manager n; n.initialize(mixed.path); idle(n); resetCalls(0);
    REQUIRE(n.download({PackId::Textures}, error)); idle(n);
    REQUIRE(pack(n, PackId::Textures).state == PackState::InstalledManual);
    REQUIRE(pack(n, PackId::Textures).removableBytes == 3);
    // Force an atomic replacement failure, then verify an unrelated later save cannot persist it.
    const auto state = mixed.art() + "/.hd-assets-v420/state";
    fs::rename(state, state + ".backup"); fs::create_directory(state);
    REQUIRE(!n.scheduleRemoval(PackId::Textures, error));
    REQUIRE(pack(n, PackId::Textures).state != PackState::RemovalPending);
    fs::remove(state); fs::rename(state + ".backup", state);
    REQUIRE(n.setSources({"https://mirror.test/v4.2.0/"}, error)); n.shutdown();
    Manager restored; restored.initialize(mixed.path); idle(restored);
    REQUIRE(fs::exists(mixed.art() + "/ReforgedTextures.big"));
    REQUIRE(restored.scheduleRemoval(PackId::Textures, error)); restored.shutdown();
    Manager removed; removed.initialize(mixed.path); idle(removed);
    REQUIRE(get(mixed.art() + "/ReforgedTerrain.big") == "abc");
    REQUIRE(!fs::exists(mixed.art() + "/ReforgedTextures.big"));
}
static void pauseCancelResume() {
    Temp t; std::string error; Manager m; m.initialize(t.path); idle(m); resetCalls(3);
    REQUIRE(m.download({PackId::Skins}, error));
    for (unsigned i=0; i<100 && pack(m, PackId::Skins).downloaded == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    m.pause();
    for (unsigned i=0; i<100 && pack(m, PackId::Skins).state != PackState::Paused; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(pack(m, PackId::Skins).state == PackState::Paused);
    uint64_t bytes = pack(m, PackId::Skins).downloaded; REQUIRE(bytes > 0 && bytes < 3);
    m.cancel(); idle(m); REQUIRE(!fs::exists(t.art() + "/ReforgedSkins.big"));
    resetCalls(0); REQUIRE(m.download({PackId::Skins}, error)); idle(m);
    REQUIRE(offsets == int(bytes)); REQUIRE(pack(m, PackId::Skins).state == PackState::InstalledManaged);
}
int main() {
    try { responseTests(); ownershipAndRestart(); manualAndReplacement(); fallbackAndSafety(); partialMixedAndFailedSave(); pauseCancelResume(); std::cout << "HD Assets deterministic fixtures passed\n"; return 0; }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
