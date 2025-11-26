#include "HttpResponse.h"

String AsyncHttpResponse::_emptyString = "";

AsyncHttpResponse::AsyncHttpResponse() : _statusCode(0), _contentLength(0) {}

AsyncHttpResponse::~AsyncHttpResponse() {}

const String& AsyncHttpResponse::getHeader(const String& name) const {
    for (const auto& header : _headers) {
        if (header.name.equalsIgnoreCase(name)) {
            return header.value;
        }
    }
    return _emptyString;
}

const String& AsyncHttpResponse::getTrailer(const String& name) const {
    for (const auto& trailer : _trailers) {
        if (trailer.name.equalsIgnoreCase(name)) {
            return trailer.value;
        }
    }
    return _emptyString;
}

void AsyncHttpResponse::setHeader(const String& name, const String& value) {
    // Check if header already exists and update it
    for (auto& header : _headers) {
        if (header.name.equalsIgnoreCase(name)) {
            header.value = value;
            return;
        }
    }
    _headers.push_back(HttpHeader(name, value));
}

void AsyncHttpResponse::setTrailer(const String& name, const String& value) {
    for (auto& trailer : _trailers) {
        if (trailer.name.equalsIgnoreCase(name)) {
            trailer.value = value;
            return;
        }
    }
    _trailers.push_back(HttpHeader(name, value));
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
