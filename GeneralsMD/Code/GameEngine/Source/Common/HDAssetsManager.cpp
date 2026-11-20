#include "Common/HDAssetsManager.h"
#include "Common/HDAssetsTransport.h"
#ifdef __linux__
#include <mbedtls/sha256.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <limits.h>
#include <deque>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace HDAssets {
#ifdef HD_ASSETS_TESTING
bool fixtureFetch(const std::string &, const std::string &, uint64_t, int, const std::function<bool()> &, const std::function<void(uint64_t)> &, std::string &);
#endif
namespace {
const char *const official = "https://zerohour.gg/gzhr/game/v4.2.0/";
struct Archive { PackId pack; const char *name; uint64_t size; const char *sha; };
const Archive archives[] = {
    {PackId::Skins, "ReforgedSkins.big", 7005486ULL, "8098400caeeebedfcbc3b466f631aabc54dba65e0176f83e63145e5042208358"},
    {PackId::Units, "ReforgedChina.big", 443055048ULL, "6352e286f37da961724fa3d9d04d4a80d496349120a814b5d73012e1c154997d"},
    {PackId::Units, "ReforgedGLA.big", 477024071ULL, "2bf77e9afcd829496ff6928eac839d0534757591c3ec381eeed76b5d5eed661f"},
    {PackId::Units, "ReforgedTurkiye.big", 494805091ULL, "e6170164f8966fee67e377ebecffa37b787c310a653cb6684cd9cabacdf70f2f"},
    {PackId::Units, "ReforgedUSA.big", 363551284ULL, "38cd5b784b06db78b6e299e62ea0b529c7d3ac778c5e9f26f93df643df8bfcab"},
    {PackId::Maps, "ReforgedNeutralA.big", 797092294ULL, "4cd6171f90103a70114820515ec90a52f86e799d51140768d39d586ea770cecf"},
    {PackId::Maps, "ReforgedNeutralB.big", 1233687752ULL, "db27e1fb75f0d592955dad0d5bc21f167bb3370130390000c29a958ef57c5888"},
    {PackId::Maps, "ReforgedNeutralC.big", 1244421459ULL, "67bc049d1085c0c42908ee5c935769aaeeff88aecd85c9b05cde177d5b9642a8"},
    {PackId::Maps, "ReforgedNeutralD.big", 1164755320ULL, "ce412bc38370a448e443adacedf3e0e9db814bcd195b06f98fb74d8208389837"},
    {PackId::Maps, "ReforgedNeutralE.big", 1126858872ULL, "a2bfa55158cf76397c6f851f9963cfca5034ea3db40d19c753223c729271daa8"},
    {PackId::Maps, "ReforgedNeutralF.big", 985174054ULL, "450293e04a96024e5fcbb1c25458bbfe6b8ece881c6aaad7853d7771e273767a"},
    {PackId::Maps, "ReforgedNeutralG.big", 991787911ULL, "a92ffa72fad6ca1d128ba9eb6d90804a5380fb8050d374f0b4b56643f385bf4b"},
    {PackId::Textures, "ReforgedTerrain.big", 227914020ULL, "8e8081dcefbd8dbd004585bbd38f62de0a8e729fc80134c632f4f22f1a3a2f0d"},
    {PackId::Textures, "ReforgedTextures.big", 1068574664ULL, "6647bb2e0cbaaba46a0c4345245303abe8603723932e6b0642e100e508a81f2d"},
};
uint64_t sizeOf(const Archive &a) {
#ifdef HD_ASSETS_TESTING
    (void)a; return 3;
#else
    return a.size;
#endif
}
const char *hashOf(const Archive &a) {
#ifdef HD_ASSETS_TESTING
    (void)a; return "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"; // abc
#else
    return a.sha;
#endif
}
const Archive *record(const std::string &name) { for (const auto &a : archives) if (name == a.name) return &a; return nullptr; }
size_t index(PackId p) { return static_cast<size_t>(p); }
bool valid(PackId p) { return index(p) < 4; }
struct FD {
    int n = -1;
    explicit FD(int fd = -1) : n(fd) {}
    ~FD() { if (n >= 0) close(n); }
    FD(const FD &) = delete;
    FD &operator=(const FD &) = delete;
};
int openDirectory(const std::string &path) {
    if (path.empty() || path[0] != '/') return -1;
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    std::istringstream parts(path); std::string part;
    while (std::getline(parts, part, '/')) {
        if (part.empty()) continue;
        if (part == "." || part == "..") { close(fd); return -1; }
        int next = openat(fd, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(fd); fd = next; if (fd < 0) break;
    }
    return fd;
}
bool ownedRegular(int fd) { struct stat s; return fd >= 0 && !fstat(fd, &s) && S_ISREG(s.st_mode) && s.st_nlink == 1 && s.st_uid == geteuid(); }
int childDirectory(int parent, const char *name, bool strict = true) {
    if (mkdirat(parent, name, 0700) && errno != EEXIST) return -1;
    int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat s{};
    if (fd < 0 || fstat(fd, &s) || !S_ISDIR(s.st_mode) || s.st_uid != geteuid() || (s.st_mode & (strict ? 0022 : 0002))) { if (fd >= 0) close(fd); return -1; }
    return fd;
}
int privateFile(int dir, const char *name, int flags) {
    int fd = openat(dir, name, flags | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (!ownedRegular(fd)) { if (fd >= 0) close(fd); return -1; }
    return fd;
}
bool writeAll(int fd, const std::string &s) {
    size_t offset = 0;
    while (offset < s.size()) { ssize_t n = write(fd, s.data() + offset, s.size() - offset); if (n < 0 && errno == EINTR) continue; if (n <= 0) return false; offset += n; }
    return true;
}
std::string fingerprint(int fd, const std::function<bool()> &proceed) {
    if (lseek(fd, 0, SEEK_SET) < 0) return "";
    mbedtls_sha256_context sha; mbedtls_sha256_init(&sha);
    bool ok = mbedtls_sha256_starts(&sha, 0) == 0;
    unsigned char bytes[262144], digest[32];
    while (ok && proceed()) {
        ssize_t n = read(fd, bytes, sizeof(bytes));
        if (n < 0 && errno == EINTR) continue;
        if (!n) break;
        if (n < 0 || mbedtls_sha256_update(&sha, bytes, n)) { ok = false; break; }
    }
    ok = ok && proceed() && mbedtls_sha256_finish(&sha, digest) == 0;
    mbedtls_sha256_free(&sha);
    if (!ok) return "";
    const char hex[] = "0123456789abcdef"; std::string result;
    for (unsigned char c : digest) { result += hex[c >> 4]; result += hex[c & 15]; }
    return result;
}
struct Owner { uint64_t device = 0, inode = 0; bool pending = false; };
bool same(const Owner &o, const struct stat &s) { return o.device == uint64_t(s.st_dev) && o.inode == uint64_t(s.st_ino) && S_ISREG(s.st_mode) && s.st_nlink == 1; }
}

struct Manager::Impl {
    mutable std::mutex mutex;
    std::condition_variable wake;
    Snapshot view;
    std::map<std::string, Owner> inventory;
    std::set<std::string> verified;
    std::deque<PackId> queue;
    std::thread worker;
    std::atomic<bool> stop{false}, paused{false}, cancelled{false};
    int art = -1, state = -1, liveLock = -1, writerLock = -1;
    bool initialized = false, writable = false, canRemove = false;

    Impl() {
        const char *titles[] = {"Textures", "Units", "Maps", "Skins"};
        for (size_t i = 0; i < 4; ++i) {
            PackSnapshot p; p.id = static_cast<PackId>(i); p.title = titles[i];
            for (const auto &a : archives) if (index(a.pack) == i) p.total += sizeOf(a);
            std::ostringstream text;
            if (p.total < 1000000000ULL)
                text << (p.total + 500000ULL) / 1000000ULL << " MB";
            else
                text << std::fixed << std::setprecision(2) << double(p.total)/1000000000.0 << " GB";
            p.displaySize = text.str(); view.packs.push_back(p);
        }
        view.sources.push_back(official);
    }
    ~Impl() { closeFiles(); }
    void closeFiles() { for (int *fd : {&writerLock, &liveLock, &state, &art}) if (*fd >= 0) { close(*fd); *fd = -1; } }
    void error(const std::string &text) { std::lock_guard<std::mutex> guard(mutex); view.error = text; }
    bool save() { // mutex held, exclusive writer lifetime lock held
        if (!writable) return false;
        std::ostringstream data; data << "ZHR-HD-ASSETS-1\n" << "invited " << view.invitationSeen << '\n';
        for (size_t i = 1; i < view.sources.size(); ++i) data << "source " << std::quoted(view.sources[i]) << '\n';
        for (const auto &entry : inventory) data << "owned " << entry.first << ' ' << entry.second.device << ' ' << entry.second.inode << ' ' << entry.second.pending << '\n';
        // O_EXCL and a fresh random-free per-process serial avoid following/replacing hostile temp files.
        static std::atomic<unsigned> serial{0};
        std::string tmp = "state.tmp-" + std::to_string(getpid()) + "-" + std::to_string(++serial);
        FD fd(privateFile(state, tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL));
        if (fd.n < 0 || !writeAll(fd.n, data.str()) || fsync(fd.n) || renameat(state, tmp.c_str(), state, "state")) {
            if (fd.n >= 0) unlinkat(state, tmp.c_str(), 0);
            view.error = "Cannot save HD Assets settings and ownership safely."; return false;
        }
        // rename is the commit point: after it succeeds memory must retain the new state.
        // A failed directory fsync makes crash durability uncertain, so surface that fact without
        // pretending that an already visible transaction was rolled back.
        int synced; do { synced = fsync(state); } while (synced && errno == EINTR);
        if (synced) view.error = "HD Assets settings were saved, but crash durability could not be confirmed.";
        return true;
    }
    bool load() {
        FD fd(privateFile(state, "state", O_RDONLY));
        if (fd.n < 0) { struct stat s; return fstatat(state, "state", &s, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT; }
        struct stat s; if (fstat(fd.n, &s) || s.st_size > 65536) return false;
        std::string text; char bytes[4096]; ssize_t n;
        while ((n = read(fd.n, bytes, sizeof(bytes))) != 0) { if (n < 0) { if (errno == EINTR) continue; return false; } text.append(bytes, n); }
        std::istringstream lines(text); std::string line;
        if (!std::getline(lines, line) || line != "ZHR-HD-ASSETS-1") return false;
        while (std::getline(lines, line)) {
            std::istringstream row(line); std::string kind; row >> kind;
            if (kind == "invited") { int v; if (!(row >> v) || (v != 0 && v != 1)) return false; view.invitationSeen = v; }
            else if (kind == "source") { std::string source, why; Transport::URL url; if (!(row >> std::quoted(source)) || !Transport::parseFolder(source, url, why) || view.sources.size() >= 9) return false; if (source.back() != '/') source += '/'; if (std::find(view.sources.begin(), view.sources.end(), source) == view.sources.end()) view.sources.push_back(source); }
            else if (kind == "owned") { std::string name; Owner o; int pending; if (!(row >> name >> o.device >> o.inode >> pending) || !record(name) || !o.inode || (pending != 0 && pending != 1) || inventory.count(name)) return false; o.pending = pending; inventory[name] = o; }
            else return false;
            row >> std::ws; if (!row.eof()) return false;
        }
        return true;
    }
    bool matching(int fd, const Archive &a) {
        struct stat before, after;
        if (fd < 0 || fstat(fd, &before) || !S_ISREG(before.st_mode) || before.st_size < 0 || uint64_t(before.st_size) != sizeOf(a)) return false;
        const bool hash = fingerprint(fd, [&] { return !stop.load(); }) == hashOf(a);
        return hash && !fstat(fd, &after) && before.st_dev == after.st_dev && before.st_ino == after.st_ino && before.st_size == after.st_size && before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec && before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
    }
    bool readOnlyArt(const std::string &directory) {
        // Preserve the pre-existing manual-overlay behavior (including user symlinks) without
        // managing any path through that symlink. Only canonical, anchored read descriptors survive.
        closeFiles(); writable = false; canRemove = false;
        char canonical[PATH_MAX];
        if (!realpath((directory + "/ReforgedArt").c_str(), canonical) || (art = openDirectory(canonical)) < 0) return false;
        struct stat child{};
        if (!fstatat(art, ".hd-assets-v420", &child, AT_SYMLINK_NOFOLLOW) && S_ISDIR(child.st_mode)) {
            state = openat(art, ".hd-assets-v420", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (state < 0) return false;
            liveLock = privateFile(state, "live.lock", O_RDONLY);
            if (liveLock < 0 || flock(liveLock, LOCK_SH | LOCK_NB)) return false;
            // Never import ownership in the read-only fallback: it cannot schedule or apply deletion.
        }
        // Without an existing state directory no file can be manager-owned. A later manager never
        // adopts these pre-existing files, so their read-only mount cannot race owned-file removal.
        view.artDirectory = canonical;
        view.error = "HD Assets is read-only for this artwork directory; existing manual art is preserved.";
        return true;
    }
    void applyRemovals() {
        if (!canRemove || !writable) return;
        bool changed = false;
        for (auto i = inventory.begin(); i != inventory.end();) {
            if (!i->second.pending) { ++i; continue; }
            const Archive *a = record(i->first);
            FD fd(openat(art, i->first.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
            struct stat opened{}, path{};
            bool mine = fd.n >= 0 && !fstat(fd.n, &opened) && same(i->second, opened);
            bool absent = fstatat(art, i->first.c_str(), &path, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT;
            if (mine && matching(fd.n, *a) && !fstatat(art, i->first.c_str(), &path, AT_SYMLINK_NOFOLLOW) && same(i->second, path)) {
                if (unlinkat(art, i->first.c_str(), 0)) { view.error = "A scheduled HD archive could not be removed."; ++i; continue; }
                fsync(art);
            } else if (!absent && mine) { // A changed owned file becomes manual; never delete it.
                view.error = "A changed HD archive was preserved as a manual installation.";
            }
            i = inventory.erase(i); changed = true;
        }
        if (changed) save();
    }
    void refresh(PackId pack) { // mutex held
        auto &p = view.packs[index(pack)];
        bool all = true, managed = true, pending = false; p.downloaded = 0; p.removableBytes = 0;
        for (const auto &a : archives) if (a.pack == pack) {
            bool present = verified.count(a.name); all &= present;
            auto owner = inventory.find(a.name);
            managed &= owner != inventory.end();
            pending |= owner != inventory.end() && owner->second.pending;
            if (present) {
                p.downloaded += sizeOf(a);
                struct stat disk{};
                if (owner != inventory.end() && !fstatat(art, a.name, &disk, AT_SYMLINK_NOFOLLOW) && same(owner->second, disk) && uint64_t(disk.st_size) == sizeOf(a)) p.removableBytes += disk.st_size;
            }
        }
        p.state = pending ? PackState::RemovalPending : all ? (managed ? PackState::InstalledManaged : PackState::InstalledManual) : PackState::Missing;
        if (pending) { p.restartRequired = true; p.detail = "Removal is scheduled for a restart. Manual files are preserved."; }
        else if (all) p.detail = managed ? "Verified manager-owned archives." : "Verified manual archives; they will be preserved.";
        else p.detail.clear();
    }
    void scan() {
        bool changed = false;
        for (const auto &a : archives) {
            if (stop) return;
            FD fd(openat(art, a.name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
            bool matches = matching(fd.n, a);
            struct stat s{}; bool regular = fd.n >= 0 && !fstat(fd.n, &s);
            std::lock_guard<std::mutex> guard(mutex);
            if (matches) verified.insert(a.name);
            auto owner = inventory.find(a.name);
            if (owner != inventory.end() && (!matches || !regular || !same(owner->second, s))) { inventory.erase(owner); changed = true; }
        }
        std::lock_guard<std::mutex> guard(mutex);
        for (auto &p : view.packs) refresh(p.id);
        if (!verified.empty() && !view.invitationSeen) { view.invitationSeen = true; changed = true; }
        if (changed && writable) save();
        view.busy = false;
    }
    bool control() const { return !stop.load() && !cancelled.load() && !paused.load(); }
    bool waitPaused(PackId pack) {
        std::unique_lock<std::mutex> guard(mutex);
        if (paused && !stop && !cancelled) { view.packs[index(pack)].state = PackState::Paused; wake.wait(guard, [&] { return !paused || stop || cancelled; }); }
        return !stop && !cancelled;
    }
    bool install(const Archive &a, uint64_t completed) {
        std::string part = std::string(a.name) + ".part";
        FD fd(privateFile(state, part.c_str(), O_RDWR | O_CREAT));
        if (fd.n < 0) { error("Cannot safely open the partial archive."); return false; }
        struct stat partial{};
        if (fstat(fd.n, &partial) || uint64_t(partial.st_size) > sizeOf(a)) {
            // Only a regular manager partial is truncated; never a .big or external path.
            if (ftruncate(fd.n, 0)) return false;
        }
        std::vector<std::string> sources;
        { std::lock_guard<std::mutex> guard(mutex); sources = view.sources; }
        std::string why;
        for (const auto &source : sources) for (unsigned attempt = 0; attempt < 2;) {
            if (!waitPaused(a.pack)) return false;
            {
                std::lock_guard<std::mutex> guard(mutex); auto &p = view.packs[index(a.pack)]; p.state = PackState::Downloading; p.detail = "Downloading " + std::string(a.name) + (source == official ? " from the official source." : " from a saved mirror.");
            }
            bool fetched =
#ifdef HD_ASSETS_TESTING
                fixtureFetch
#else
                Transport::fetch
#endif
                (source, a.name, sizeOf(a), fd.n, [&] { return control(); }, [&](uint64_t amount) { std::lock_guard<std::mutex> guard(mutex); view.packs[index(a.pack)].downloaded = completed + amount; }, why);
            if (fsync(fd.n)) { error("Cannot save the partial archive (check available disk space)."); return false; }
            if (!control()) { if (!waitPaused(a.pack)) return false; continue; }
            ++attempt;
            if (!fetched) continue;
            { std::lock_guard<std::mutex> guard(mutex); view.packs[index(a.pack)].state = PackState::Verifying; view.packs[index(a.pack)].detail = "Checking the official SHA-256 of " + std::string(a.name) + "."; }
            struct stat beforeHash{}, afterHash{};
            if (fstat(fd.n, &beforeHash)) { error("Cannot inspect the partial archive before verification."); return false; }
            std::string hash = fingerprint(fd.n, [&] { return control(); });
            if (!control()) { if (!waitPaused(a.pack)) return false; continue; }
            if (fstat(fd.n, &afterHash) || beforeHash.st_size != afterHash.st_size || beforeHash.st_mtim.tv_sec != afterHash.st_mtim.tv_sec || beforeHash.st_mtim.tv_nsec != afterHash.st_mtim.tv_nsec || beforeHash.st_ctim.tv_sec != afterHash.st_ctim.tv_sec || beforeHash.st_ctim.tv_nsec != afterHash.st_ctim.tv_nsec) { error("The partial archive changed while it was being verified."); return false; }
            if (hash != hashOf(a)) { why = "The archive does not match the official SHA-256; corrupt bytes were discarded."; if (ftruncate(fd.n, 0) || fsync(fd.n)) return false; continue; }
            struct stat s{}, path{};
            if (fstat(fd.n, &s) || !S_ISREG(s.st_mode) || s.st_nlink != 1 || uint64_t(s.st_size) != sizeOf(a) || fstatat(state, part.c_str(), &path, AT_SYMLINK_NOFOLLOW) || path.st_dev != s.st_dev || path.st_ino != s.st_ino) { error("The partial archive changed during verification."); return false; }
            // Link publication is atomic and fails if *any* destination (including a symlink) exists.
            // A crash before inventory commit leaves a manual archive, which is never adopted or deleted.
            if (linkat(state, part.c_str(), art, a.name, 0)) { error("An existing archive was preserved; remove it manually before retrying this pack."); return false; }
            if (unlinkat(state, part.c_str(), 0) || fsync(art) || fsync(state)) { error("The archive was published but its ownership could not be recorded safely."); return false; }
            { std::lock_guard<std::mutex> guard(mutex);
              inventory[a.name] = {uint64_t(s.st_dev), uint64_t(s.st_ino), false}; verified.insert(a.name);
              if (!save()) { inventory.erase(a.name); return false; }
              view.packs[index(a.pack)].restartRequired = true;
            }
            return true;
        }
        error(why.empty() ? "The HD archive could not be downloaded from any source." : why);
        return false;
    }
    void run() {
        scan();
        while (!stop) {
            PackId pack;
            {
                std::unique_lock<std::mutex> guard(mutex);
                wake.wait(guard, [&] { return stop || !queue.empty(); });
                if (stop) break;
                pack = queue.front(); queue.pop_front();
            }
            bool ok = true; uint64_t completed = 0;
            for (const auto &a : archives) if (a.pack == pack) {
                bool exists;
                { std::lock_guard<std::mutex> guard(mutex); exists = verified.count(a.name); }
                if (!exists && !install(a, completed)) { ok = false; break; }
                completed += sizeOf(a);
            }
            std::lock_guard<std::mutex> guard(mutex);
            refresh(pack);
            if (!ok && !cancelled && !stop) { view.packs[index(pack)].state = PackState::Error; view.packs[index(pack)].detail = view.error; }
            if (cancelled || stop) {
                for (PackId pending : queue) refresh(pending);
                queue.clear();
                if (!stop) view.packs[index(pack)].detail = "Cancelled. Verified archives and resumable partials are retained.";
            }
            if (queue.empty()) { view.busy = false; paused = false; }
        }
    }
};
Manager::Manager() : impl(new Impl) {}
Manager::~Manager() { shutdown(); }
Manager &manager() { static Manager instance; return instance; }
void Manager::initialize(const std::string &userDirectory) {
    auto &p = *impl;
    std::lock_guard<std::mutex> guard(p.mutex);
    if (p.initialized) return;
    p.initialized = true;
    std::string directory = userDirectory;
    while (!directory.empty() && (directory.back() == '/' || directory.back() == '\\')) directory.pop_back();

    FD user(openDirectory(directory));
    if (user.n < 0 || (p.art = childDirectory(user.n, "ReforgedArt", false)) < 0 || (p.state = childDirectory(p.art, ".hd-assets-v420")) < 0) {
        if (!p.readOnlyArt(directory)) { p.view.error = "Cannot safely open or lock the HD Assets directory."; return; }
        for (auto &pack : p.view.packs) pack.state = PackState::Checking;
        p.view.busy = true;
        p.worker = std::thread([&p] { p.run(); });
        return;
    }
    p.liveLock = privateFile(p.state, "live.lock", O_RDWR | O_CREAT);
    if (p.liveLock < 0) { p.view.error = "Cannot safely lock the HD Assets directory."; return; }
    p.canRemove = flock(p.liveLock, LOCK_EX | LOCK_NB) == 0;
    if (!p.canRemove && flock(p.liveLock, LOCK_SH | LOCK_NB)) { p.view.error = "Another instance is applying HD Assets changes; restart after it finishes."; return; }
    p.view.artDirectory = directory + "/ReforgedArt";
    p.writerLock = privateFile(p.state, "writer.lock", O_RDWR | O_CREAT);
    p.writable = p.writerLock >= 0 && flock(p.writerLock, LOCK_EX | LOCK_NB) == 0;
    if (!p.load()) { p.inventory.clear(); p.view.sources = {official}; p.view.invitationSeen = true; p.writable = false; p.view.error = "HD Assets settings are invalid; existing archives have been preserved."; }
    else if (!p.writable) p.view.error = "Another running instance controls HD Assets. This instance is read-only.";
    p.applyRemovals();
    if (p.canRemove && flock(p.liveLock, LOCK_SH)) { p.writable = false; p.view.error = "Cannot hold the HD Assets live-instance lock."; return; }
    for (auto &pack : p.view.packs) pack.state = PackState::Checking;
    p.view.busy = true;
    p.worker = std::thread([&p] { p.run(); });
}
Snapshot Manager::snapshot() const { std::lock_guard<std::mutex> guard(impl->mutex); return impl->view; }
bool Manager::invitationNeeded() const { std::lock_guard<std::mutex> guard(impl->mutex); return impl->initialized && impl->writable && !impl->view.busy && !impl->view.invitationSeen; }
void Manager::dismissInvitation() { std::lock_guard<std::mutex> guard(impl->mutex); if (impl->writable) { bool before = impl->view.invitationSeen; impl->view.invitationSeen = true; if (!impl->save()) impl->view.invitationSeen = before; } }
bool Manager::download(const std::vector<PackId> &packs, std::string &error) {
    auto &p = *impl; std::lock_guard<std::mutex> guard(p.mutex); error.clear();
    if (!p.writable || !p.worker.joinable() || p.stop || p.view.busy) { error = p.view.busy ? "Wait for the current HD Assets task." : "HD Assets cannot write settings in this instance."; return false; }
    if (packs.empty()) { error = "Select at least one pack."; return false; }
    std::set<PackId> selected;
    for (PackId id : packs) {
        if (!valid(id) || p.view.packs[index(id)].state == PackState::RemovalPending) { error = "A selected pack is invalid or scheduled for removal."; return false; }
        selected.insert(id);
    }
    bool any = false;
    for (PackId id : selected) for (const auto &a : archives) if (a.pack == id && !p.verified.count(a.name)) any = true;
    if (!any) { error = "All selected packs are already verified."; return false; }
    p.view.error.clear(); p.cancelled = false; p.paused = false;
    for (PackId id : selected) { p.queue.push_back(id); p.view.packs[index(id)].state = PackState::Queued; }
    p.view.busy = true; p.wake.notify_all(); return true;
}
void Manager::pause() { impl->paused = true; }
void Manager::resume() { impl->paused = false; impl->wake.notify_all(); }
void Manager::cancel() { impl->cancelled = true; impl->paused = false; impl->wake.notify_all(); }
bool Manager::scheduleRemoval(PackId id, std::string &error) {
    auto &p = *impl; std::lock_guard<std::mutex> guard(p.mutex); error.clear();
    if (!p.writable || p.view.busy || !valid(id)) { error = "Wait until HD Assets is idle in its controlling instance."; return false; }
    const auto before = p.inventory;
    bool any = false;
    for (auto &entry : p.inventory) if (record(entry.first)->pack == id) { entry.second.pending = true; any = true; }
    if (!any) { error = "This pack has no manager-owned archives. Manual files are preserved."; return false; }
    if (!p.save()) { p.inventory = before; error = p.view.error; return false; }
    p.refresh(id); return true;
}
bool Manager::setSources(const std::vector<std::string> &sources, std::string &error) {
    std::vector<std::string> normalized{official};
    for (std::string source : sources) {
        Transport::URL parsed;
        if (!Transport::parseFolder(source, parsed, error)) return false;
        if (source.back() != '/') source += '/';
        if (std::find(normalized.begin(), normalized.end(), source) == normalized.end()) normalized.push_back(source);
    }
    if (normalized.size() > 9) { error = "At most eight mirror folders can be saved."; return false; }
    auto &p = *impl; std::lock_guard<std::mutex> guard(p.mutex);
    if (!p.writable || p.view.busy) { error = "Wait until HD Assets is idle in its controlling instance."; return false; }
    auto before = p.view.sources; p.view.sources = normalized;
    if (!p.save()) { p.view.sources = before; error = p.view.error; return false; }
    error.clear(); return true;
}
void Manager::shutdown() {
    if (!impl) return;
    impl->stop = true; impl->paused = false; impl->wake.notify_all();
    if (impl->worker.joinable()) impl->worker.join();
    std::lock_guard<std::mutex> guard(impl->mutex);
    impl->view.busy = false; impl->writable = false; impl->closeFiles();
}
}
#endif
