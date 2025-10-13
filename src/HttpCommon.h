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
#define ESP_ASYNC_WEB_CLIENT_VERSION "1.0.4"
#endif

struct HttpHeader {
    String name;
    String value;

    HttpHeader() {}
    HttpHeader(const String& n, const String& v) : name(n), value(v) {}
};

// Keep README.md "Error Codes" table in sync with this enum.
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
    CONNECTION_CLOSED_MID_BODY = -10, // new explicit code to disambiguate body truncation
    MAX_BODY_SIZE_EXCEEDED = -11
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
        return "HTTPS not implemented";
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
    default:
        return "Network error";
    }
}

#endif // HTTP_COMMON_H
