/**
 * Lightweight URL parsing utility extracted from AsyncHttpRequest to allow
 * host (native) unit testing without requiring Arduino framework headers.
 *
 * Supported forms (mirrors original behaviour):
 *  - http://host
 *  - https://host
 *  - host (defaults to http)
 *  - host:port/path?query
 *  - host?query   (query before first '/')
 *  - http(s)://host?query (same as above)
 */
#pragma once

#include <string>
#include <cstdint>

namespace UrlParser {

struct ParsedUrl {
    std::string host;
    std::string path; // always begins with '/'
    uint16_t port = 80;
    bool secure = false;
};

// Parse URL into components. Returns false if host empty after parsing.
bool parse(const std::string& url, ParsedUrl& out);

} // namespace UrlParser
