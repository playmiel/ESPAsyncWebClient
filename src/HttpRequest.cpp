#include "HttpRequest.h"
#include "UrlParser.h"

String AsyncHttpRequest::_emptyString = "";

AsyncHttpRequest::AsyncHttpRequest(HttpMethod method, const String& url)
    : _method(method), _url(url), _port(80), _secure(false), _timeout(10000) {

    parseUrl(url);

    // Set default headers
    setHeader("Connection", "close");
    setHeader("User-Agent", "ESPAsyncWebClient/1.0.2");
}

AsyncHttpRequest::~AsyncHttpRequest() {}

void AsyncHttpRequest::setHeader(const String& name, const String& value) {
    // Check if header already exists and update it
    for (auto& header : _headers) {
        if (header.name.equalsIgnoreCase(name)) {
            header.value = value;
            return;
        }
    }
    // Add new header
    _headers.push_back(HttpHeader(name, value));
}

const String& AsyncHttpRequest::getHeader(const String& name) const {
    for (const auto& header : _headers) {
        if (header.name.equalsIgnoreCase(name)) {
            return header.value;
        }
    }
    return _emptyString;
}

String AsyncHttpRequest::buildHttpRequest() const {
    String request = methodToString() + " " + _path + " HTTP/1.1\r\n";
    request += "Host: " + _host + "\r\n";

    // Add all headers
    for (const auto& header : _headers) {
        request += header.name + ": " + header.value + "\r\n";
    }

    // Add content length if we have a full (non-streamed) body
    if (!_body.isEmpty()) {
        request += "Content-Length: " + String(_body.length()) + "\r\n";
    } else if (_bodyProvider != nullptr) {
        request += "Content-Length: " + String(_streamLength) + "\r\n"; // caller must provide accurate length
    }

    request += "\r\n";

    // Add body if present
    if (!_body.isEmpty()) {
        request += _body;
    } // stream body is written later by client using buildHeadersOnly

    return request;
}

String AsyncHttpRequest::buildHeadersOnly() const {
    String req = methodToString() + " " + _path + " HTTP/1.1\r\n";
    req += "Host: " + _host + "\r\n";
    for (const auto& header : _headers) {
        req += header.name + ": " + header.value + "\r\n";
    }
    if (_bodyProvider != nullptr) {
        req += "Content-Length: " + String(_streamLength) + "\r\n";
    } else if (!_body.isEmpty()) {
        req += "Content-Length: " + String(_body.length()) + "\r\n";
    }
    req += "\r\n";
    return req;
}

bool AsyncHttpRequest::parseUrl(const String& url) {
    UrlParser::ParsedUrl parsed;
    if (!UrlParser::parse(std::string(url.c_str()), parsed)) {
        return false;
    }
    _secure = parsed.secure;
    _port = parsed.port;
    _host = parsed.host.c_str();
    _path = parsed.path.c_str();
    _queryFinalized = true; // parsed URL already has path/query
    return true;
}

String AsyncHttpRequest::methodToString() const {
    switch (_method) {
    case HTTP_GET:
        return "GET";
    case HTTP_POST:
        return "POST";
    case HTTP_PUT:
        return "PUT";
    case HTTP_DELETE:
        return "DELETE";
    case HTTP_HEAD:
        return "HEAD";
    case HTTP_PATCH:
        return "PATCH";
    default:
        return "GET";
    }
}

void AsyncHttpRequest::addQueryParam(const String& key, const String& value) {
    // If URL originally had query we append using &
    if (_queryFinalized) {
        // first call after parseUrl -> detect if _path already has ?
        if (_path.indexOf('?') == -1) {
            _path += '?';
        } else if (!_path.endsWith("?") && !_path.endsWith("&")) {
            _path += '&';
        }
        _queryFinalized = false;
    } else {
        if (!_path.endsWith("?") && !_path.endsWith("&"))
            _path += '&';
    }
    // Basic URL encoding for space and a few chars (minimal to avoid heavy code)
    auto encode = [](const String& in) -> String {
        String out;
        const char* hex = "0123456789ABCDEF";
        for (size_t i = 0; i < in.length(); ++i) {
            char c = in[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                c == '.' || c == '~') {
                out += c;
            } else {
                out += '%';
                out += hex[(c >> 4) & 0xF];
                out += hex[c & 0xF];
            }
        }
        return out;
    };
    _path += encode(key);
    _path += '=';
    _path += encode(value);
}

void AsyncHttpRequest::finalizeQueryParams() {
    _queryFinalized = true; // nothing else needed
}

void AsyncHttpRequest::setBasicAuth(const String& user, const String& pass) {
    String creds = user + ":" + pass;
    // simple Base64 (minimal) encoding to avoid pulling large libs
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String out;
    size_t len = creds.length();
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint8_t)creds[i] << 16;
        if (i + 1 < len)
            v |= (uint8_t)creds[i + 1] << 8;
        if (i + 2 < len)
            v |= (uint8_t)creds[i + 2];
        out += b64[(v >> 18) & 0x3F];
        out += b64[(v >> 12) & 0x3F];
        if (i + 1 < len)
            out += b64[(v >> 6) & 0x3F];
        else
            out += '=';
        if (i + 2 < len)
            out += b64[v & 0x3F];
        else
            out += '=';
    }
    setHeader("Authorization", String("Basic ") + out);
}

void AsyncHttpRequest::enableGzipAcceptEncoding(bool enable) {
    _acceptGzip = enable;
    if (enable) {
        setHeader("Accept-Encoding", "gzip");
    } else {
        // no removal to keep code simple
    }
}