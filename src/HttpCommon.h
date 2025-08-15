#ifndef HTTP_COMMON_H
#define HTTP_COMMON_H

#include <Arduino.h>

struct HttpHeader {
    String name;
    String value;
    
    HttpHeader() {}
    HttpHeader(const String& n, const String& v) : name(n), value(v) {}
};

enum HttpClientError {
    CONNECTION_FAILED = -1,
    HEADER_PARSE_FAILED = -2,
    CONNECTION_CLOSED = -3,
    REQUEST_TIMEOUT = -4
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
        default:
            return "Network error";
    }
}

#endif // HTTP_COMMON_H
