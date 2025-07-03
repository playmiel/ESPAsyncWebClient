#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <Arduino.h>
#include <vector>
#include "HttpCommon.h"

enum HttpMethod {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_HEAD,
    HTTP_PATCH
};

class AsyncHttpRequest {
public:
    AsyncHttpRequest(HttpMethod method, const String& url);
    ~AsyncHttpRequest();
    
    // Request configuration
    HttpMethod getMethod() const { return _method; }
    const String& getUrl() const { return _url; }
    const String& getHost() const { return _host; }
    const String& getPath() const { return _path; }
    uint16_t getPort() const { return _port; }
    bool isSecure() const { return _secure; }
    
    // Headers management
    void setHeader(const String& name, const String& value);
    const String& getHeader(const String& name) const;
    const std::vector<HttpHeader>& getHeaders() const { return _headers; }
    
    // Body management
    void setBody(const String& body) { _body = body; }
    const String& getBody() const { return _body; }
    
    // Timeout
    void setTimeout(uint32_t timeout) { _timeout = timeout; }
    uint32_t getTimeout() const { return _timeout; }
    
    // User Agent
    void setUserAgent(const String& userAgent) { setHeader("User-Agent", userAgent); }
    
    // Build HTTP request string
    String buildHttpRequest() const;
    
    // URL parsing
    bool parseUrl(const String& url);

private:
    HttpMethod _method;
    String _url;
    String _host;
    String _path;
    uint16_t _port;
    bool _secure;
    std::vector<HttpHeader> _headers;
    String _body;
    uint32_t _timeout;
    static String _emptyString;
    
    String methodToString() const;
};

#endif // HTTP_REQUEST_H