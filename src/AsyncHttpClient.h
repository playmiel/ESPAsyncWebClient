// Enhanced Async HTTP Client with queueing, streaming body, global chunk callback,
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
#if defined(ARDUINO_ARCH_ESP32) && defined(ASYNC_HTTP_ENABLE_AUTOLOOP)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif

class AsyncHttpClient {
  public:
    typedef std::function<void(AsyncHttpResponse*)> SuccessCallback;
    typedef std::function<void(HttpClientError, const char*)> ErrorCallback;
    typedef std::function<void(const char* data, size_t len, bool final)> BodyChunkCallback; // global
    // (Per-request chunk callback removed for API simplification)

    AsyncHttpClient();
    ~AsyncHttpClient();

    // Main HTTP methods
    uint32_t get(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    uint32_t post(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    uint32_t put(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    uint32_t del(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    uint32_t head(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    uint32_t patch(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);

#ifdef ASYNC_HTTP_LEGACY_VOID_API
    // Legacy void-return adapters (opt-in). They discard the returned request ID.
    void get_legacy(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr) {
        (void)get(url, onSuccess, onError);
    }
    void post_legacy(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr) {
        (void)post(url, data, onSuccess, onError);
    }
    void put_legacy(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr) {
        (void)put(url, data, onSuccess, onError);
    }
    void del_legacy(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr) {
        (void)del(url, onSuccess, onError);
    }
    void head_legacy(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr) {
        (void)head(url, onSuccess, onError);
    }
    void patch_legacy(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr) {
        (void)patch(url, data, onSuccess, onError);
    }
#endif

    // Configuration methods
    void setHeader(const char* name, const char* value);
    void setTimeout(uint32_t timeout); // total request timeout
    void setUserAgent(const char* userAgent);
    void setDefaultConnectTimeout(uint32_t ms) {
        _defaultConnectTimeout = ms;
    }
    void setMaxParallel(uint16_t maxParallel) {
        _maxParallel = maxParallel;
        tryDequeue();
    }

    // Advanced request method
    uint32_t request(AsyncHttpRequest* request, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    // Removed per-request chunk overload (was experimental)
#ifdef ASYNC_HTTP_LEGACY_VOID_API
    void request_legacy(AsyncHttpRequest* request, SuccessCallback onSuccess, ErrorCallback onError = nullptr) {
        (void)request;
        (void)this->request(request, onSuccess, onError);
    }
#endif
    // Abort by id (returns true if found and aborted)
    bool abort(uint32_t requestId);

    // Global streaming body callback (applies for all responses unless overridden per-request in future)
    void onBodyChunk(BodyChunkCallback cb) {
        _bodyChunkCallback = cb;
    }

    void loop(); // manual timeout / queue progression

  private:
    // Lightweight locking helpers (no-op unless ESP32 auto-loop task is enabled)
    void lock();
    void unlock();

    // Internal auto-loop task for fallback timeout mode (ESP32 only)
#if !ASYNC_TCP_HAS_TIMEOUT && defined(ARDUINO_ARCH_ESP32) && defined(ASYNC_HTTP_ENABLE_AUTOLOOP)
    static void _autoLoopTaskThunk(void* param);
    TaskHandle_t _autoLoopTaskHandle = nullptr;
#endif

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
        // perRequestChunkCb removed
        uint32_t connectStartMs;
        uint32_t connectTimeoutMs;
        bool headersSent;
        bool streamingBodyInProgress;
#if !ASYNC_TCP_HAS_TIMEOUT
        uint32_t timeoutTimer;
#endif
        RequestContext()
            : request(nullptr), response(nullptr), client(nullptr), headersComplete(false), responseProcessed(false),
              expectedContentLength(0), receivedContentLength(0), chunked(false), chunkedComplete(false),
              currentChunkRemaining(0), id(0), connectStartMs(0), connectTimeoutMs(0), headersSent(false),
              streamingBodyInProgress(false)
#if !ASYNC_TCP_HAS_TIMEOUT
              ,
              timeoutTimer(0)
#endif
        {
        }
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

#if defined(ARDUINO_ARCH_ESP32) && defined(ASYNC_HTTP_ENABLE_AUTOLOOP)
    SemaphoreHandle_t _reqMutex = nullptr; // recursive mutex
#endif

    // Internal methods
    uint32_t makeRequest(HttpMethod method, const char* url, const char* data, SuccessCallback onSuccess,
                         ErrorCallback onError);
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

  public:
    // Exposed publicly for tests and advanced internal usage
    static bool parseChunkSizeLine(const String& line, uint32_t* outSize);

  private:
};

#endif // ASYNC_HTTP_CLIENT_H
