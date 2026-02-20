#include "HttpRequest.h"
#include "UrlParser.h"
#include <cstring>

static size_t decimalLength(size_t value) {
    size_t len = 1;
    while (value >= 10) {
        value /= 10;
        ++len;
    }
    return len;
}

AsyncHttpRequest::AsyncHttpRequest(HttpMethod method, const String& url)
    : _method(method), _url(url), _port(80), _secure(false), _timeout(10000) {

    parseUrl(url);

    // Set default headers
    setHeader("Connection", "close");
    setHeader("User-Agent", String("ESPAsyncWebClient/") + ESP_ASYNC_WEB_CLIENT_VERSION);
}

AsyncHttpRequest::~AsyncHttpRequest() {}

void AsyncHttpRequest::setHeader(const String& name, const String& value) {
    if (!isValidHttpHeaderName(name) || !isValidHttpHeaderValue(value))
        return;
    String lowerName = name;
    lowerName.toLowerCase();
    // Check if header already exists and update it
    for (auto& header : _headers) {
        if (header.name == lowerName) {
            header.value = value;
            return;
        }
    }
    _headers.push_back(HttpHeader(lowerName, value));
}

void AsyncHttpRequest::removeHeader(const String& name) {
    String lowerName = name;
    lowerName.toLowerCase();
    for (auto it = _headers.begin(); it != _headers.end();) {
        if (it->name == lowerName) {
            it = _headers.erase(it);
        } else {
            ++it;
        }
    }
}

String AsyncHttpRequest::getHeader(const String& name) const {
    String lowerName = name;
    lowerName.toLowerCase();
    for (const auto& header : _headers) {
        if (header.name == lowerName) {
            return header.value;
        }
    }
    return String();
}

String AsyncHttpRequest::buildHttpRequest() const {
    size_t bodyLen = _body.length();
    String request = buildAllHeaders(bodyLen);
    if (!_body.isEmpty()) {
        request += _body;
    } // stream body is written later by client using buildHeadersOnly
    return request;
}

String AsyncHttpRequest::buildHeadersOnly() const {
    return buildAllHeaders(0);
}

String AsyncHttpRequest::buildAllHeaders(size_t extraReserve) const {
    const char* method = methodToString();
    size_t headerLen = 0;
    headerLen += strlen(method) + 1 + _path.length() + strlen(" HTTP/1.1\r\n");
    headerLen += strlen("Host: ") + _host.length() + strlen("\r\n");
    for (const auto& header : _headers) {
        headerLen += header.name.length() + 2 + header.value.length() + 2;
    }
    if (_bodyProvider != nullptr) {
        headerLen += strlen("Content-Length: ") + decimalLength(_streamLength) + 2;
    } else if (!_body.isEmpty()) {
        headerLen += strlen("Content-Length: ") + decimalLength(_body.length()) + 2;
    }
    headerLen += 2;

    String req;
    req.reserve(headerLen + extraReserve);
    req += method;
    req += ' ';
    req += _path;
    req += " HTTP/1.1\r\n";
    req += "Host: ";
    req += _host;
    req += "\r\n";
    for (const auto& header : _headers) {
        req += header.name;
        req += ": ";
        req += header.value;
        req += "\r\n";
    }
    if (_bodyProvider != nullptr) {
        req += "Content-Length: ";
        req += String(_streamLength); // caller must provide accurate length
        req += "\r\n";
    } else if (!_body.isEmpty()) {
        req += "Content-Length: ";
        req += String(_body.length());
        req += "\r\n";
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

const char* AsyncHttpRequest::methodToString() const {
    switch (_method) {
    case HTTP_METHOD_GET:
        return "GET";
    case HTTP_METHOD_POST:
        return "POST";
    case HTTP_METHOD_PUT:
        return "PUT";
    case HTTP_METHOD_DELETE:
        return "DELETE";
    case HTTP_METHOD_HEAD:
        return "HEAD";
    case HTTP_METHOD_PATCH:
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
            uint8_t uc = static_cast<uint8_t>(in[i]);
            char c = static_cast<char>(uc);
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                c == '.' || c == '~') {
                out += c;
            } else {
                out += '%';
                out += hex[(uc >> 4) & 0xF];
                out += hex[uc & 0xF];
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
        removeHeader("Accept-Encoding");
    }
}

void AsyncHttpRequest::setTlsConfig(const AsyncHttpTLSConfig& config) {
    if (!_tlsConfig)
        _tlsConfig.reset(new AsyncHttpTLSConfig());
    *_tlsConfig = config;
}
