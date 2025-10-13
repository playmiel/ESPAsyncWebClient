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

void AsyncHttpResponse::setHeader(const String& name, const String& value) {
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

void AsyncHttpResponse::appendBody(const char* data, size_t len) {
    if (data && len > 0) {
        _body.concat(data, len);
    }
}

void AsyncHttpResponse::setContentLength(size_t length) {
    _contentLength = length;
    if (length > 0) {
        _body.reserve(static_cast<unsigned int>(length));
    }
}

void AsyncHttpResponse::clear() {
    _statusCode = 0;
    _statusText = "";
    _headers.clear();
    _body = "";
    _contentLength = 0;
}
