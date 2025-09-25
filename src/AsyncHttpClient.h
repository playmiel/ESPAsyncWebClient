// Enhanced Async HTTP Client with queueing, streaming body, per-request & global chunk callbacks,
// basic auth helper, HEAD/PATCH wrappers, and separate connect timeout.

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
    typedef std::function<void(const char* data, size_t len, bool final)> BodyChunkCallback; // global
    typedef std::function<void(const char* data, size_t len, bool final)> PerRequestBodyChunkCallback; // per-request

    AsyncHttpClient();
    ~AsyncHttpClient();

    // Main HTTP methods
    void get(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    void post(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    void put(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    void del(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    void head(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    void patch(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

    // Configuration methods
    void setHeader(const char* name, const char* value);
    void setTimeout(uint32_t timeout); // total request timeout
    void setUserAgent(const char* userAgent);
    void setDefaultConnectTimeout(uint32_t ms) { _defaultConnectTimeout = ms; }
    void setMaxParallel(uint16_t maxParallel) { _maxParallel = maxParallel; tryDequeue(); }

    // Advanced request method
    void request(AsyncHttpRequest* request, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    // Abort by id (returns true if found and aborted)
    bool abort(uint32_t requestId);

    // Global streaming body callback (applies for all responses unless overridden per-request in future)
    void onBodyChunk(BodyChunkCallback cb) { _bodyChunkCallback = cb; }

    void loop(); // manual timeout / queue progression

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
        PerRequestBodyChunkCallback perRequestChunkCb;
        uint32_t connectStartMs;
        uint32_t connectTimeoutMs;
        bool headersSent;
        bool streamingBodyInProgress;
#if !ASYNC_TCP_HAS_TIMEOUT
        uint32_t timeoutTimer;
#endif
        RequestContext()
            : request(nullptr), response(nullptr), client(nullptr),
              headersComplete(false), responseProcessed(false),
              expectedContentLength(0), receivedContentLength(0),
              chunked(false), chunkedComplete(false), currentChunkRemaining(0),
              id(0), perRequestChunkCb(nullptr), connectStartMs(0), connectTimeoutMs(0),
              headersSent(false), streamingBodyInProgress(false)
#if !ASYNC_TCP_HAS_TIMEOUT
            , timeoutTimer(0)
#endif
        {}
    };

    std::vector<HttpHeader> _defaultHeaders;
    uint32_t _defaultTimeout; // total
    String _defaultUserAgent;
    BodyChunkCallback _bodyChunkCallback;
    uint32_t _nextRequestId = 1;
    uint16_t _maxParallel = 0; // 0 => unlimited
    std::vector<RequestContext*> _activeRequests;
    std::vector<RequestContext*> _pendingQueue;
    uint32_t _defaultConnectTimeout = 5000;

    // Internal methods
    void makeRequest(HttpMethod method, const char* url, const char* data,
                     SuccessCallback onSuccess, ErrorCallback onError);
    void executeOrQueue(RequestContext* context);
    void executeRequest(RequestContext* context);
    void handleConnect(RequestContext* context, AsyncClient* client);
    void handleData(RequestContext* context, AsyncClient* client, char* data, size_t len);
    void handleDisconnect(RequestContext* context, AsyncClient* client);
    void handleError(RequestContext* context, AsyncClient* client, int8_t error);
    bool parseResponseHeaders(RequestContext* context, const String& headerData);
    void processResponse(RequestContext* context);
    void cleanup(RequestContext* context);
    void triggerError(RequestContext* context, HttpClientError errorCode, const char* errorMessage);
    void tryDequeue();
    void sendStreamData(RequestContext* context);
};

#endif // ASYNC_HTTP_CLIENT_H
