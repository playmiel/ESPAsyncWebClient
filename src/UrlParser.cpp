#include "UrlParser.h"
#include <cctype>
#include <cerrno>
#include <cstdlib>

namespace UrlParser {

static constexpr size_t kMaxUrlLength = 2048;
static constexpr size_t kMaxHostLength = 255;
static constexpr size_t kMaxPathLength = 1900;

static bool startsWith(const std::string& s, const char* prefix) {
    size_t n = 0;
    while (prefix[n] != '\0')
        ++n; // strlen
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

static bool hasInvalidUrlChar(const std::string& url) {
    for (char c : url) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc <= 0x1F || uc == 0x7F || c == '\r' || c == '\n' || c == ' ' || c == '\t')
            return true;
    }
    return false;
}

static bool isValidHostChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

static bool isValidHost(const std::string& host) {
    if (host.empty() || host.size() > kMaxHostLength)
        return false;
    if (host.front() == '.' || host.back() == '.')
        return false;
    for (char c : host) {
        if (!isValidHostChar(c))
            return false;
    }
    return true;
}

static bool parsePort(const std::string& portStr, uint16_t* out) {
    if (!out || portStr.empty())
        return false;
    for (char c : portStr) {
        if (c < '0' || c > '9')
            return false;
    }
    errno = 0;
    char* end = nullptr;
    unsigned long val = std::strtoul(portStr.c_str(), &end, 10);
    if (end == portStr.c_str() || *end != '\0')
        return false;
    if (errno == ERANGE || val > 65535)
        return false;
    *out = static_cast<uint16_t>(val);
    return true;
}

bool parse(const std::string& originalUrl, ParsedUrl& out) {
    if (originalUrl.size() > kMaxUrlLength || hasInvalidUrlChar(originalUrl))
        return false;

    std::string url = originalUrl; // working copy
    out.secure = false;
    out.port = 80;
    out.schemeImplicit = false;

    if (startsWith(url, "https://")) {
        out.secure = true;
        out.port = 443;
        url.erase(0, 8);
    } else if (startsWith(url, "http://")) {
        out.secure = false;
        out.port = 80;
        url.erase(0, 7);
    } else {
        // No scheme provided -> default to HTTPS and signal implicit scheme
        out.secure = true;
        out.port = 443;
        out.schemeImplicit = true;
    }

    // Find first '/' and first '?'
    size_t pathPos = url.find('/');
    size_t queryPos = url.find('?');

    if (pathPos == std::string::npos) {
        if (queryPos == std::string::npos) {
            out.host = url;
            out.path = "/";
        } else {
            out.host = url.substr(0, queryPos);
            out.path = "/" + url.substr(queryPos); // ensure leading '/'
        }
    } else if (queryPos != std::string::npos && queryPos < pathPos) {
        // Query appears before first path '/'
        out.host = url.substr(0, queryPos);
        out.path = "/" + url.substr(queryPos);
    } else {
        out.host = url.substr(0, pathPos);
        out.path = url.substr(pathPos);
    }

    // Extract explicit port if present in host
    size_t colon = out.host.find(':');
    if (colon != std::string::npos) {
        std::string portStr = out.host.substr(colon + 1);
        out.host = out.host.substr(0, colon);
        uint16_t parsedPort = 0;
        if (!parsePort(portStr, &parsedPort))
            return false;
        out.port = parsedPort;
    }

    if (!isValidHost(out.host))
        return false;
    if (out.path.size() > kMaxPathLength)
        return false;

    return !out.host.empty();
}

} // namespace UrlParser
