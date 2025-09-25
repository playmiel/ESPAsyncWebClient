#include "HttpRequest.h"

String AsyncHttpRequest::_emptyString = "";

AsyncHttpRequest::AsyncHttpRequest(HttpMethod method, const String& url) 
    : _method(method), _url(url), _port(80), _secure(false), _timeout(10000) {
    
    parseUrl(url);
    
    // Set default headers
    setHeader("Connection", "close");
    setHeader("User-Agent", "ESPAsyncWebClient/1.0.1");
}

AsyncHttpRequest::~AsyncHttpRequest() {
}

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
    
    // Add content length if we have a body
    if (!_body.isEmpty()) {
        request += "Content-Length: " + String(_body.length()) + "\r\n";
    }
    
    request += "\r\n";
    
    // Add body if present
    if (!_body.isEmpty()) {
        request += _body;
    }
    
    return request;
}

bool AsyncHttpRequest::parseUrl(const String& url) {
    String urlCopy = url;
    
    // Check for protocol
    if (urlCopy.startsWith("https://")) {
        _secure = true;
        _port = 443;
        urlCopy = urlCopy.substring(8);
    } else if (urlCopy.startsWith("http://")) {
        _secure = false;
        _port = 80;
        urlCopy = urlCopy.substring(7);
    } else {
        // Default to http
        _secure = false;
        _port = 80;
    }
    
    // Find path and query separators
    int pathIndex = urlCopy.indexOf('/');
    int queryIndex = urlCopy.indexOf('?');

    // If there is no explicit path, handle URLs like "example.com?foo=bar"
    if (pathIndex == -1) {
        if (queryIndex == -1) {
            _host = urlCopy;
            _path = "/";
        } else {
            _host = urlCopy.substring(0, queryIndex);
            _path = String('/') + urlCopy.substring(queryIndex);
        }
    } else if (queryIndex != -1 && queryIndex < pathIndex) {
        // Query appears before path separator
        _host = urlCopy.substring(0, queryIndex);
        _path = String('/') + urlCopy.substring(queryIndex);
    } else {
        _host = urlCopy.substring(0, pathIndex);
        _path = urlCopy.substring(pathIndex);
    }
    
    // Check for port in hostname
    int portIndex = _host.indexOf(':');
    if (portIndex != -1) {
        _port = _host.substring(portIndex + 1).toInt();
        _host = _host.substring(0, portIndex);
    }
    
    return !_host.isEmpty();
}

String AsyncHttpRequest::methodToString() const {
    switch (_method) {
        case HTTP_GET: return "GET";
        case HTTP_POST: return "POST";
        case HTTP_PUT: return "PUT";
        case HTTP_DELETE: return "DELETE";
        case HTTP_HEAD: return "HEAD";
        case HTTP_PATCH: return "PATCH";
        default: return "GET";
    }
}