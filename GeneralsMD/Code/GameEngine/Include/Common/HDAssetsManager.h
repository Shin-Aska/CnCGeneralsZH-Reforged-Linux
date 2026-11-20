#ifndef INCLUDED_HD_ASSETS_MANAGER_H
#define INCLUDED_HD_ASSETS_MANAGER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Linux's optional, pinned v4.2.0 artwork. No engine objects are touched by the worker.
namespace HDAssets {
enum class PackId { Textures, Units, Maps, Skins };
enum class PackState { Missing, Checking, Queued, Downloading, Paused, Verifying,
    InstalledManaged, InstalledManual, RemovalPending, Error };
struct PackSnapshot {
    PackId id;
    std::string title, displaySize;
    PackState state = PackState::Missing;
    uint64_t downloaded = 0, total = 0, removableBytes = 0;
    std::string detail;
    bool restartRequired = false;
};
struct Snapshot {
    std::vector<PackSnapshot> packs;
    std::vector<std::string> sources;
    std::string artDirectory, error;
    bool invitationSeen = false, busy = false;
};
class Manager {
public:
    Manager();
    ~Manager();
    Manager(const Manager &) = delete;
    Manager &operator=(const Manager &) = delete;
    // Call before mounting the package overlay: pending removals run only here.
    void initialize(const std::string &userDirectory);
    Snapshot snapshot() const;
    bool invitationNeeded() const;
    void dismissInvitation();
    bool download(const std::vector<PackId> &, std::string &error);
    void pause();
    void resume();
    void cancel();
    bool scheduleRemoval(PackId, std::string &error);
    // HTTPS release-folder URLs; the immutable official source is always first.
    bool setSources(const std::vector<std::string> &, std::string &error);
    void shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
Manager &manager();
}
#endif
