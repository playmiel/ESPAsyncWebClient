#include "HttpResponse.h"

AsyncHttpResponse::AsyncHttpResponse() : _statusCode(0), _contentLength(0) {}

AsyncHttpResponse::~AsyncHttpResponse() {}

String AsyncHttpResponse::getHeader(const String& name) const {
    String lowerName = name;
    lowerName.toLowerCase();
    for (const auto& header : _headers) {
        if (header.name == lowerName) {
            return header.value;
        }
    }
    return String();
}

String AsyncHttpResponse::getTrailer(const String& name) const {
    String lowerName = name;
    lowerName.toLowerCase();
    for (const auto& trailer : _trailers) {
        if (trailer.name == lowerName) {
            return trailer.value;
        }
    }
    return String();
}

void AsyncHttpResponse::setHeader(const String& name, const String& value) {
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

void AsyncHttpResponse::setTrailer(const String& name, const String& value) {
    String lowerName = name;
    lowerName.toLowerCase();
    for (auto& trailer : _trailers) {
        if (trailer.name == lowerName) {
            trailer.value = value;
            return;
        }
    }
    _trailers.push_back(HttpHeader(lowerName, value));
}

void AsyncHttpResponse::appendBody(const char* data, size_t len) {
    if (data && len > 0) {
        _body.concat(data, len);
    }
}

void AsyncHttpResponse::setContentLength(size_t length) {
    _contentLength = length;
}

void AsyncHttpResponse::reserveBody(size_t length) {
    if (length > 0) {
        _body.reserve(length);
    }
}

void AsyncHttpResponse::clear() {
    _statusCode = 0;
    _statusText = "";
    _headers.clear();
    _trailers.clear();
    _body = "";
    _contentLength = 0;
}
