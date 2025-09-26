#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <Arduino.h>
#include <vector>
#include <functional>
#include "HttpCommon.h"

enum HttpMethod { HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE, HTTP_HEAD, HTTP_PATCH };

class AsyncHttpRequest {
  public:
    AsyncHttpRequest(HttpMethod method, const String& url);
    ~AsyncHttpRequest();

    // Request configuration
    HttpMethod getMethod() const {
        return _method;
    }
    const String& getUrl() const {
        return _url;
    }
    const String& getHost() const {
        return _host;
    }
    const String& getPath() const {
        return _path;
    }
    uint16_t getPort() const {
        return _port;
    }
    bool isSecure() const {
        return _secure;
    }

    // Headers management
    void setHeader(const String& name, const String& value);
    const String& getHeader(const String& name) const;
    const std::vector<HttpHeader>& getHeaders() const {
        return _headers;
    }

    // Body management
    void setBody(const String& body) {
        _body = body;
    }
    const String& getBody() const {
        return _body;
    }
    bool hasBody() const {
        return !_body.isEmpty() || _bodyProvider != nullptr;
    }
    typedef std::function<int(uint8_t* buffer, size_t maxLen, bool* final)>
        BodyStreamProvider; // returns bytes written or -1
    void setBodyStream(size_t totalLength, BodyStreamProvider provider) {
        _streamLength = totalLength;
        _bodyProvider = provider;
    }
    bool hasBodyStream() const {
        return _bodyProvider != nullptr;
    }
    size_t getStreamLength() const {
        return _streamLength;
    }
    BodyStreamProvider getBodyProvider() const {
        return _bodyProvider;
    }

    // Timeout
    void setTimeout(uint32_t timeout) {
        _timeout = timeout;
    }
    uint32_t getTimeout() const {
        return _timeout;
    }

    // User Agent
    void setUserAgent(const String& userAgent) {
        setHeader("User-Agent", userAgent);
    }

    // Build HTTP request string
    String buildHttpRequest() const;
    String buildHeadersOnly() const; // when streaming body

    // URL parsing
    bool parseUrl(const String& url);

    // Query parameter builder (when starting from base URL). Call before sending.
    void addQueryParam(const String& key, const String& value);
    void finalizeQueryParams();

    // Basic auth helper
    void setBasicAuth(const String& user, const String& pass);

    // Accept-Encoding convenience (gzip)
    void enableGzipAcceptEncoding(bool enable = true);

    // Avoid storing body in memory (use only streaming callbacks). Effective only if a response chunk callback (per-request or global) is present.
    void setNoStoreBody(bool enable=true) { _noStoreBody = enable; }
    bool getNoStoreBody() const { return _noStoreBody; }

    // (Per-request response chunk callback removed – use global client.onBodyChunk)

  private:
    HttpMethod _method;
    String _url;
    String _host;
    String _path;
    uint16_t _port;
    bool _secure;
    std::vector<HttpHeader> _headers;
    String _body;
    size_t _streamLength = 0;
    BodyStreamProvider _bodyProvider = nullptr;
    uint32_t _timeout;
    static String _emptyString;
    bool _queryFinalized = true;
    bool _acceptGzip = false;
    bool _noStoreBody = false;

    String methodToString() const;
};

#endif // HTTP_REQUEST_H