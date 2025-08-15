#ifndef ASYNC_HTTP_CLIENT_H
#define ASYNC_HTTP_CLIENT_H

#include <Arduino.h>
#include <functional>
#include <vector>
#include "HttpRequest.h"
#include "HttpResponse.h"


    #include <AsyncTCP.h>


class AsyncHttpClient {
public:
    typedef std::function<void(AsyncHttpResponse*)> SuccessCallback;
    typedef std::function<void(int, const char*)> ErrorCallback;

    AsyncHttpClient();
    ~AsyncHttpClient();
    
    // Main HTTP methods
    void get(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    void post(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    void put(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    void del(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    
    // Configuration methods
    void setHeader(const char* name, const char* value);
    void setTimeout(uint32_t timeout);
    void setUserAgent(const char* userAgent);

    // Advanced request method
    void request(AsyncHttpRequest* request, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

    // Periodic maintenance. Should be called regularly from the Arduino loop
    // to handle request timeouts when AsyncTCP does not provide its own
    // timeout mechanism.
    void loop();

private:
    struct RequestContext {
        AsyncHttpRequest* request;
        AsyncHttpResponse* response;
        SuccessCallback onSuccess;
        ErrorCallback onError;
        AsyncClient* client;
        String responseBuffer;
        bool headersComplete;
        bool responseProcessed;
        size_t expectedContentLength;
        size_t receivedContentLength;
        uint32_t timeoutTimer;
        
        RequestContext() : request(nullptr), response(nullptr), client(nullptr), 
                          headersComplete(false), responseProcessed(false),
                          expectedContentLength(0), receivedContentLength(0), 
                          timeoutTimer(0) {}
    };
    
    std::vector<HttpHeader> _defaultHeaders;
    uint32_t _defaultTimeout;
    String _defaultUserAgent;

    std::vector<RequestContext*> _activeRequests;
    
    // Internal methods
    void makeRequest(HttpMethod method, const char* url, const char* data, 
                    SuccessCallback onSuccess, ErrorCallback onError);
    void executeRequest(RequestContext* context);
    void handleConnect(RequestContext* context, AsyncClient* client);
    void handleData(RequestContext* context, AsyncClient* client, char* data, size_t len);
    void handleDisconnect(RequestContext* context, AsyncClient* client);
    void handleError(RequestContext* context, AsyncClient* client, int8_t error);
    void handleTimeout(RequestContext* context, AsyncClient* client);
    
    bool parseResponseHeaders(RequestContext* context, const String& headerData);
    void processResponse(RequestContext* context);
    void cleanup(RequestContext* context);
    void triggerError(RequestContext* context, int errorCode, const char* errorMessage);
};

#endif // ASYNC_HTTP_CLIENT_H