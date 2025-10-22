#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <Arduino.h>
#include <vector>
#include "HttpCommon.h"

class AsyncHttpResponse {
  public:
    AsyncHttpResponse();
    ~AsyncHttpResponse();

    // Response status
    int getStatusCode() const {
        return _statusCode;
    }
    const String& getStatusText() const {
        return _statusText;
    }

    // Response headers
    const String& getHeader(const String& name) const;
    const std::vector<HttpHeader>& getHeaders() const {
        return _headers;
    }

    // Response body
    const String& getBody() const {
        return _body;
    }
    size_t getContentLength() const {
        return _contentLength;
    }

    // Response info
    bool isSuccess() const {
        return _statusCode >= 200 && _statusCode < 300;
    }
    bool isRedirect() const {
        return _statusCode >= 300 && _statusCode < 400;
    }
    bool isError() const {
        return _statusCode >= 400;
    }

    // Internal methods (used by AsyncHttpClient)
    void setStatusCode(int code) {
        _statusCode = code;
    }
    void setStatusText(const String& text) {
        _statusText = text;
    }
    void setHeader(const String& name, const String& value);
    void appendBody(const char* data, size_t len);
    void setContentLength(size_t length);
    void clear();

  private:
    int _statusCode;
    String _statusText;
    std::vector<HttpHeader> _headers;
    String _body;
    size_t _contentLength;
    static String _emptyString;
};

#endif // HTTP_RESPONSE_H
