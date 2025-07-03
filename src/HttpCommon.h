#ifndef HTTP_COMMON_H
#define HTTP_COMMON_H

#include <Arduino.h>

struct HttpHeader {
    String name;
    String value;
    
    HttpHeader() {}
    HttpHeader(const String& n, const String& v) : name(n), value(v) {}
};

#endif // HTTP_COMMON_H