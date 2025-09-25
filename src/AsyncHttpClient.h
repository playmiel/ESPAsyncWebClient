#ifndef ASYNC_HTTP_CLIENT_H
#define ASYNC_HTTP_CLIENT_H

#include <Arduino.h>
#include <functional>
#include <vector>
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "HttpCommon.h"


    #include <AsyncTCP.h>


class AsyncHttpClient {
public:
    typedef std::function<void(AsyncHttpResponse*)> SuccessCallback;
    typedef std::function<void(HttpClientError, const char*)> ErrorCallback;
    typedef std::function<void(const char* data, size_t len, bool final)> BodyChunkCallback;

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

    // Optional streaming body callback (global for now)
    void onBodyChunk(BodyChunkCallback cb) { _bodyChunkCallback = cb; }

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
        bool chunked;
        bool chunkedComplete;
        size_t currentChunkRemaining;
    uint32_t id;
        
#if !ASYNC_TCP_HAS_TIMEOUT
        uint32_t timeoutTimer;
#endif

                RequestContext()
                        : request(nullptr), response(nullptr), client(nullptr),
                            headersComplete(false), responseProcessed(false),
                            expectedContentLength(0), receivedContentLength(0),
                            chunked(false), chunkedComplete(false), currentChunkRemaining(0)
                  , id(0)
#if !ASYNC_TCP_HAS_TIMEOUT
              , timeoutTimer(0)
#endif
        {}
    bool abort(uint32_t requestId);
    };
    
    std::vector<HttpHeader> _defaultHeaders;
    uint32_t _defaultTimeout;
    String _defaultUserAgent;
    BodyChunkCallback _bodyChunkCallback;
    uint32_t _nextRequestId = 1;
    
public:
    void loop();
private:
    std::vector<RequestContext*> _activeRequests; // used also for future abort logic even with native timeout
#if !ASYNC_TCP_HAS_TIMEOUT
#endif

    // Internal methods
    void makeRequest(HttpMethod method, const char* url, const char* data,
                    SuccessCallback onSuccess, ErrorCallback onError);
    void executeRequest(RequestContext* context);
    void handleConnect(RequestContext* context, AsyncClient* client);
    void handleData(RequestContext* context, AsyncClient* client, char* data, size_t len);
    void handleDisconnect(RequestContext* context, AsyncClient* client);
    void handleError(RequestContext* context, AsyncClient* client, int8_t error);

    bool parseResponseHeaders(RequestContext* context, const String& headerData);
    void processResponse(RequestContext* context);
    void cleanup(RequestContext* context);
    void triggerError(RequestContext* context, HttpClientError errorCode, const char* errorMessage);
};

#endif // ASYNC_HTTP_CLIENT_H
