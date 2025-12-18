#ifndef HTTP_COMMON_H
#define HTTP_COMMON_H

#include <Arduino.h>

// Feature flags (can be overridden before including library headers)
#ifndef ASYNC_HTTP_ENABLE_GZIP_DECODE
#define ASYNC_HTTP_ENABLE_GZIP_DECODE                                                                                  \
    0 // 0 = only set Accept-Encoding header (no inflation). 1 = future: enable minimal gzip inflate.
#endif

// Library version (single source of truth inside code). Keep in sync with library.json and library.properties.
#ifndef ESP_ASYNC_WEB_CLIENT_VERSION
#define ESP_ASYNC_WEB_CLIENT_VERSION "1.1.4"
#endif

struct HttpHeader {
    String name;
    String value;

    HttpHeader() {}
    HttpHeader(const String& n, const String& v) : name(n), value(v) {}
};

struct AsyncHttpTLSConfig {
    String caCert;
    String clientCert;
    String clientPrivateKey;
    String fingerprint;                  // SHA-256 hex (colon optional)
    bool insecure = false;               // true => skip CA verification
    uint32_t handshakeTimeoutMs = 12000; // fallback if not overridden
};

enum HttpClientError {
    CONNECTION_FAILED = -1,
    HEADER_PARSE_FAILED = -2,
    CONNECTION_CLOSED = -3,
    REQUEST_TIMEOUT = -4,
    HTTPS_NOT_SUPPORTED = -5,
    CHUNKED_DECODE_FAILED = -6,
    CONNECT_TIMEOUT = -7,
    BODY_STREAM_READ_FAILED = -8,
    ABORTED = -9,
    CONNECTION_CLOSED_MID_BODY = -10,
    MAX_BODY_SIZE_EXCEEDED = -11,
    TOO_MANY_REDIRECTS = -12,
    HEADERS_TOO_LARGE = -13,
    TLS_HANDSHAKE_FAILED = -14,
    TLS_CERT_INVALID = -15,
    TLS_FINGERPRINT_MISMATCH = -16,
    TLS_HANDSHAKE_TIMEOUT = -17
};

inline const char* httpClientErrorToString(HttpClientError error) {
    switch (error) {
    case CONNECTION_FAILED:
        return "Failed to initiate connection";
    case HEADER_PARSE_FAILED:
        return "Failed to parse response headers";
    case CONNECTION_CLOSED:
        return "Connection closed before headers received";
    case REQUEST_TIMEOUT:
        return "Request timeout";
    case HTTPS_NOT_SUPPORTED:
        return "HTTPS transport unavailable";
    case CHUNKED_DECODE_FAILED:
        return "Failed to decode chunked body";
    case CONNECT_TIMEOUT:
        return "Connect timeout";
    case BODY_STREAM_READ_FAILED:
        return "Body stream read failed";
    case ABORTED:
        return "Aborted by user";
    case CONNECTION_CLOSED_MID_BODY:
        return "Connection closed mid-body";
    case MAX_BODY_SIZE_EXCEEDED:
        return "Body exceeds configured maximum";
    case TOO_MANY_REDIRECTS:
        return "Too many redirects";
    case HEADERS_TOO_LARGE:
        return "Response headers exceed configured maximum";
    case TLS_HANDSHAKE_FAILED:
        return "TLS handshake failed";
    case TLS_CERT_INVALID:
        return "TLS certificate validation failed";
    case TLS_FINGERPRINT_MISMATCH:
        return "TLS fingerprint mismatch";
    case TLS_HANDSHAKE_TIMEOUT:
        return "TLS handshake timeout";
    default:
        return "Network error";
    }
}

// Basic validation to prevent request-line / header injection when user-provided strings are used.
// This is intentionally strict: CR/LF and ASCII control characters are rejected.
inline bool isValidHttpHeaderName(const String& name) {
    if (name.length() == 0)
        return false;
    for (size_t i = 0; i < name.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(name.charAt(i));
        bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '!' ||
                  c == '#' || c == '$' || c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' || c == '-' ||
                  c == '.' || c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
        if (!ok)
            return false;
    }
    return true;
}

inline bool isValidHttpHeaderValue(const String& value) {
    for (size_t i = 0; i < value.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(value.charAt(i));
        if (c == '\r' || c == '\n' || c == 0x00)
            return false;
        if ((c < 0x20 && c != '\t') || c == 0x7F)
            return false;
    }
    return true;
}

#endif // HTTP_COMMON_H
