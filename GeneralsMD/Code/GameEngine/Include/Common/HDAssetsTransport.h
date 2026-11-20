#ifndef INCLUDED_HD_ASSETS_TRANSPORT_H
#define INCLUDED_HD_ASSETS_TRANSPORT_H
#include <cstdint>
#include <functional>
#include <string>
namespace HDAssets { namespace Transport {
struct URL { std::string host, port, path; };
bool parseFolder(const std::string &, URL &, std::string &error);
// Redirect destinations may contain signed queries; only HTTPS absolute/root-relative targets pass.
bool redirectTarget(const URL &current, const std::string &location, URL &, std::string &error);
// Downloads into an already opened, private regular file. A false proceed() interrupts promptly.
// At most three HTTPS redirects are followed, with independent CA/hostname checks at each hop.
// Transfer coding, compression and responses not matching the pinned length are refused.
bool fetch(const std::string &folder, const std::string &name, uint64_t total, int fd,
    const std::function<bool()> &proceed, const std::function<void(uint64_t)> &progress,
    std::string &error);
// Header validator is separate so malformed/range-ignoring servers can be tested without networking.
bool validateResponse(const std::string &, uint64_t total, uint64_t offset,
    bool &restart, std::string &error);
}}
#endif
