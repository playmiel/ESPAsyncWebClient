#include "UrlParser.h"

namespace UrlParser {

static bool startsWith(const std::string& s, const char* prefix) {
    size_t n = 0; while (prefix[n] != '\0') ++n; // strlen
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

bool parse(const std::string& originalUrl, ParsedUrl& out) {
    std::string url = originalUrl; // working copy
    out.secure = false;
    out.port = 80;

    if (startsWith(url, "https://")) {
        out.secure = true;
        out.port = 443;
        url.erase(0, 8);
    } else if (startsWith(url, "http://")) {
        out.secure = false;
        out.port = 80;
        url.erase(0, 7);
    } else {
        // No scheme provided -> default http
        out.secure = false;
        out.port = 80;
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
        if (!portStr.empty()) {
            out.port = static_cast<uint16_t>(std::stoi(portStr));
        }
    }

    return !out.host.empty();
}

} // namespace UrlParser
