#include "Common/HDAssetsTransport.h"
#ifdef __linux__
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace HDAssets { namespace Transport {
using Clock = std::chrono::steady_clock;
bool parseFolder(const std::string &text, URL &out, std::string &error)
{
    error.clear();
    auto fail = [&]() { error = "Use an HTTPS archive folder URL without credentials, query or fragment."; return false; };
    if (text.size() > 2048 || text.compare(0, 8, "https://") != 0) return fail();
    for (unsigned char c : text) if (c <= 32 || c >= 127 || c == '\\' || c == '"' || c == '<' || c == '>') return fail();
    if (text.find_first_of("?#", 8) != std::string::npos) return fail();
    size_t slash = text.find('/', 8);
    std::string authority = text.substr(8, slash == std::string::npos ? slash : slash - 8);
    if (authority.empty() || authority.find('@') != std::string::npos) return fail();
    size_t colon = authority.find(':');
    out.host = authority.substr(0, colon);
    out.port = colon == std::string::npos ? "443" : authority.substr(colon + 1);
    if (out.host.empty() || out.host.front() == '.' || out.host.back() == '.') return fail();
    for (unsigned char c : out.host) if (!std::isalnum(c) && c != '.' && c != '-') return fail();
    if (out.port.empty() || out.port.size() > 5) return fail();
    unsigned port = 0;
    for (unsigned char c : out.port) { if (!std::isdigit(c)) return fail(); port = port * 10 + c - '0'; }
    if (!port || port > 65535) return fail();
    out.path = slash == std::string::npos ? "/" : text.substr(slash);
    if (out.path.back() != '/') out.path += '/';
    for (size_t i = 0; i < out.path.size(); ++i) if (out.path[i] == '%') {
        if (i + 2 >= out.path.size() || !std::isxdigit(static_cast<unsigned char>(out.path[i+1])) || !std::isxdigit(static_cast<unsigned char>(out.path[i+2]))) return fail();
        i += 2;
    }
    return true;
}
bool redirectTarget(const URL &current, const std::string &location, URL &out, std::string &error)
{
    error = "The archive redirect must use a safe HTTPS URL.";
    if (location.empty() || location.size() > 8192 || location.find('#') != std::string::npos) return false;
    for (unsigned char c : location) if (c <= 32 || c >= 127 || c == '\\' || c == '"' || c == '<' || c == '>') return false;
    std::string absolute = location;
    if (location[0] == '/' && (location.size() < 2 || location[1] != '/'))
        absolute = "https://" + current.host + (current.port == "443" ? "" : ":" + current.port) + location;
    size_t query = absolute.find('?');
    const std::string base = absolute.substr(0, query);
    std::string why;
    if (!parseFolder(base, out, why)) return false;
    size_t slash = absolute.find('/', 8);
    out.path = slash == std::string::npos ? "/" : absolute.substr(slash);
    if (slash == std::string::npos && query != std::string::npos) out.path += absolute.substr(query);
    error.clear(); return true;
}
static bool redirected(const std::string &header, const URL &current, URL &target, bool &isRedirect, std::string &error)
{
    isRedirect = false;
    size_t end = header.find("\r\n");
    if (end == std::string::npos) return true;
    const std::string status = header.substr(0, end);
    if (status.size() < 12 || (status.compare(0,9,"HTTP/1.1 ") && status.compare(0,9,"HTTP/1.0 "))) return true;
    const std::string code = status.substr(9, 3);
    isRedirect = code == "301" || code == "302" || code == "303" || code == "307" || code == "308";
    if (!isRedirect) return true;
    if (status.size() > 12 && status[12] != ' ') { error = "Malformed HTTPS redirect status."; return false; }
    std::string location;
    size_t pos = end + 2;
    while (pos < header.size()) {
        end = header.find("\r\n", pos);
        if (end == std::string::npos) return false;
        if (end == pos) break;
        std::string line = header.substr(pos, end - pos);
        size_t sep = line.find(':');
        if (!sep || sep == std::string::npos) { error = "Malformed HTTPS redirect header."; return false; }
        std::string key = line.substr(0, sep);
        for (char &c : key) c = std::tolower(static_cast<unsigned char>(c));
        if (key == "location") {
            if (!location.empty()) { error = "Ambiguous HTTPS redirect location."; return false; }
            std::string value = line.substr(sep + 1);
            size_t a = value.find_first_not_of(" \t"), b = value.find_last_not_of(" \t");
            if (a == std::string::npos) return false;
            location = value.substr(a, b - a + 1);
        }
        pos = end + 2;
    }
    return redirectTarget(current, location, target, error);
}
static bool number(const std::string &s, uint64_t &value)
{
    if (s.empty()) return false;
    value = 0;
    for (unsigned char c : s) { if (!std::isdigit(c) || value > (UINT64_MAX - (c - '0')) / 10) return false; value = value * 10 + c - '0'; }
    return true;
}
bool validateResponse(const std::string &header, uint64_t total, uint64_t offset, bool &restart, std::string &error)
{
    restart = false;
    error = "The HTTPS server returned an invalid archive response.";
    if (offset > total || header.size() > 32768) return false;
    size_t eol = header.find("\r\n");
    if (eol == std::string::npos) return false;
    std::string status = header.substr(0, eol);
    if (status.size() < 12 || (status.compare(0, 9, "HTTP/1.1 ") && status.compare(0, 9, "HTTP/1.0 "))) return false;
    const bool full = status.substr(9, 3) == "200";
    const bool range = status.substr(9, 3) == "206";
    if ((!full && !range) || (status.size() > 12 && status[12] != ' ')) { error = "The HTTPS source refused the archive (redirects are not followed)."; return false; }
    std::map<std::string, std::string> fields;
    size_t pos = eol + 2;
    bool ended = false;
    while (pos < header.size()) {
        eol = header.find("\r\n", pos);
        if (eol == std::string::npos) return false;
        if (eol == pos) { ended = eol + 2 == header.size(); break; }
        std::string line = header.substr(pos, eol - pos);
        size_t sep = line.find(':');
        if (!sep || sep == std::string::npos) return false;
        std::string key = line.substr(0, sep), value = line.substr(sep + 1);
        for (char &c : key) { if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') return false; c = std::tolower(static_cast<unsigned char>(c)); }
        size_t a = value.find_first_not_of(" \t"), b = value.find_last_not_of(" \t");
        value = a == std::string::npos ? "" : value.substr(a, b - a + 1);
        if (!fields.emplace(key, value).second) return false;
        pos = eol + 2;
    }
    if (!ended || fields.count("transfer-encoding") || (fields.count("content-encoding") && fields["content-encoding"] != "identity")) return false;
    uint64_t length;
    if (!number(fields["content-length"], length)) return false;
    if (full) {
        if (length != total || fields.count("content-range")) return false;
        restart = offset != 0;
    } else {
        if (length != total - offset || fields["content-range"] != "bytes " + std::to_string(offset) + "-" + std::to_string(total - 1) + "/" + std::to_string(total)) return false;
    }
    error.clear();
    return true;
}
struct Resolve {
    std::mutex mutex; std::condition_variable ready; bool done = false; int code = EAI_FAIL; addrinfo *list = nullptr;
    ~Resolve() { if (list) freeaddrinfo(list); }
};
static int sendBio(void *context, const unsigned char *data, size_t n)
{
    int r = ::send(*static_cast<int *>(context), data, n, MSG_NOSIGNAL);
    if (r >= 0) return r;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}
static int receiveBio(void *context, unsigned char *data, size_t n)
{
    int r = ::recv(*static_cast<int *>(context), data, n, 0);
    if (r >= 0) return r;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}
struct TLS {
    int fd = -1;
    mbedtls_ssl_context ssl; mbedtls_ssl_config config; mbedtls_x509_crt roots;
    mbedtls_entropy_context entropy; mbedtls_ctr_drbg_context random;
    TLS() { mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&config); mbedtls_x509_crt_init(&roots); mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&random); }
    ~TLS() { if (fd >= 0) close(fd); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&config); mbedtls_x509_crt_free(&roots); mbedtls_ctr_drbg_free(&random); mbedtls_entropy_free(&entropy); }
};
static bool retry(int r) { return r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE || r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET; }
static bool waitIO(int fd, short events, Clock::time_point deadline, const std::function<bool()> &proceed)
{
    while (proceed() && Clock::now() < deadline) {
        pollfd p = {fd, events, 0};
        int r = poll(&p, 1, 100);
        if (r > 0) return !(p.revents & (POLLERR | POLLNVAL));
        if (r < 0 && errno != EINTR) return false;
    }
    return false;
}
static bool fetchAt(const URL &url, uint64_t total, int output,
    const std::function<bool()> &proceed, const std::function<void(uint64_t)> &progress, std::string &error, unsigned redirects)
{
    error = "The HTTPS connection failed or timed out.";
    struct stat status;
    if (fstat(output, &status) || !S_ISREG(status.st_mode) || status.st_size < 0 || uint64_t(status.st_size) > total) { error = "The partial archive is invalid."; return false; }
    uint64_t offset = status.st_size;
    if (offset == total) return true;
    auto dns = std::make_shared<Resolve>();
    // Resolver lifetime is independent of a timed-out request; no dangling stack data or unbounded join.
    std::thread([dns, url]() { addrinfo hints{}; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
        addrinfo *list = nullptr; int code = getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &list);
        std::lock_guard<std::mutex> guard(dns->mutex); dns->list = list; dns->code = code; dns->done = true; dns->ready.notify_all(); }).detach();
    auto deadline = Clock::now() + std::chrono::seconds(15);
    { std::unique_lock<std::mutex> guard(dns->mutex);
      while (!dns->done && proceed() && Clock::now() < deadline) dns->ready.wait_for(guard, std::chrono::milliseconds(100));
      if (!dns->done || dns->code || !proceed()) return false; }
    TLS tls;
    for (addrinfo *a = dns->list; a && proceed() && Clock::now() < deadline; a = a->ai_next) {
        tls.fd = socket(a->ai_family, a->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, a->ai_protocol);
        if (tls.fd < 0) continue;
        int r = connect(tls.fd, a->ai_addr, a->ai_addrlen);
        bool connected = r == 0;
        if (!connected && errno == EINPROGRESS && waitIO(tls.fd, POLLOUT, deadline, proceed)) {
            int problem = 0; socklen_t size = sizeof(problem);
            connected = getsockopt(tls.fd, SOL_SOCKET, SO_ERROR, &problem, &size) == 0 && !problem;
        }
        if (connected) break;
        close(tls.fd); tls.fd = -1;
    }
    if (tls.fd < 0 || !proceed()) return false;
    const char *bundles[] = {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/ca-bundle.pem", "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem", "/etc/ssl/cert.pem"};
    bool roots = false;
    for (const char *path : bundles) if (mbedtls_x509_crt_parse_file(&tls.roots, path) >= 0) { roots = true; break; }
    if (!roots) { error = "No system certificate authority bundle could be loaded."; return false; }
    static const unsigned char personal[] = "zhr HD assets";
    if (mbedtls_ctr_drbg_seed(&tls.random, mbedtls_entropy_func, &tls.entropy, personal, sizeof(personal)-1) ||
        mbedtls_ssl_config_defaults(&tls.config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) return false;
    mbedtls_ssl_conf_authmode(&tls.config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&tls.config, &tls.roots, nullptr);
    mbedtls_ssl_conf_rng(&tls.config, mbedtls_ctr_drbg_random, &tls.random);
    mbedtls_ssl_conf_min_tls_version(&tls.config, MBEDTLS_SSL_VERSION_TLS1_2);
    if (mbedtls_ssl_setup(&tls.ssl, &tls.config) || mbedtls_ssl_set_hostname(&tls.ssl, url.host.c_str())) return false;
    mbedtls_ssl_set_bio(&tls.ssl, &tls.fd, sendBio, receiveBio, nullptr);
    deadline = Clock::now() + std::chrono::seconds(20);
    int r;
    while ((r = mbedtls_ssl_handshake(&tls.ssl)) != 0) {
        if (!retry(r) || !waitIO(tls.fd, r == MBEDTLS_ERR_SSL_WANT_WRITE ? POLLOUT : POLLIN, deadline, proceed)) { error = "TLS certificate/hostname verification or handshake failed."; return false; }
    }
    if (mbedtls_ssl_get_verify_result(&tls.ssl) != 0 || !proceed()) return false;
    std::string request = "GET " + url.path + " HTTP/1.1\r\nHost: " + url.host + (url.port == "443" ? "" : ":" + url.port) + "\r\nUser-Agent: ZeroHourReforged-HD/4.2.0\r\nAccept-Encoding: identity\r\nConnection: close\r\n";
    if (offset) request += "Range: bytes=" + std::to_string(offset) + "-\r\n";
    request += "\r\n";
    deadline = Clock::now() + std::chrono::seconds(30);
    for (size_t sent = 0; sent < request.size();) {
        if (!proceed()) return false;
        r = mbedtls_ssl_write(&tls.ssl, reinterpret_cast<const unsigned char *>(request.data() + sent), request.size() - sent);
        if (r > 0) { sent += r; deadline = Clock::now() + std::chrono::seconds(30); }
        else if (!retry(r) || !waitIO(tls.fd, r == MBEDTLS_ERR_SSL_WANT_WRITE ? POLLOUT : POLLIN, deadline, proceed)) return false;
    }
    std::string header;
    bool headersDone = false;
    uint64_t received = offset;
    unsigned char data[65536];
    while (proceed()) {
        r = mbedtls_ssl_read(&tls.ssl, data, sizeof(data));
        if (r <= 0) {
            if (retry(r) && waitIO(tls.fd, r == MBEDTLS_ERR_SSL_WANT_WRITE ? POLLOUT : POLLIN, deadline, proceed)) continue;
            error = "The HTTPS archive transfer ended early or timed out."; return false;
        }
        deadline = Clock::now() + std::chrono::seconds(30);
        size_t start = 0;
        if (!headersDone) {
            while (start < size_t(r) && !headersDone) {
                header += char(data[start++]);
                if (header.size() > 32768) { error = "HTTPS headers are too large."; return false; }
                if (header.size() >= 4 && header.compare(header.size()-4, 4, "\r\n\r\n") == 0) headersDone = true;
            }
            if (!headersDone) continue;
            URL target; bool isRedirect;
            if (!redirected(header, url, target, isRedirect, error)) return false;
            if (isRedirect) {
                if (redirects >= 3) { error = "The HTTPS source exceeded the three-redirect limit."; return false; }
                // Close the previous socket before opening another independently verified TLS connection.
                close(tls.fd); tls.fd = -1;
                return fetchAt(target, total, output, proceed, progress, error, redirects + 1);
            }
            bool restart;
            if (!validateResponse(header, total, offset, restart, error)) return false;
            if (restart) { if (ftruncate(output, 0)) { error = "Cannot reset the partial archive."; return false; } received = 0; }
            if (lseek(output, received, SEEK_SET) < 0) return false;
        }
        size_t count = size_t(r) - start;
        if (count > total - received) { error = "The server sent more than the pinned archive size."; return false; }
        size_t done = 0;
        while (done < count) {
            ssize_t n = write(output, data + start + done, count - done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { error = "Cannot write the partial archive (check available disk space)."; return false; }
            done += n;
        }
        received += count;
        progress(received);
        if (received == total) { error.clear(); return true; }
    }
    error = "Transfer interrupted.";
    return false;
}
bool fetch(const std::string &folder, const std::string &name, uint64_t total, int output,
    const std::function<bool()> &proceed, const std::function<void(uint64_t)> &progress, std::string &error)
{
    URL url;
    if (!parseFolder(folder, url, error)) return false;
    url.path += name;
    return fetchAt(url, total, output, proceed, progress, error, 0);
}
}}
#endif
