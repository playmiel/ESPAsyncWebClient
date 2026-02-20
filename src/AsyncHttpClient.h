// Enhanced Async HTTP Client with queueing, streaming body, global chunk callback,
// basic auth helper, HEAD/PATCH wrappers, and separate connect timeout.

#ifndef ASYNC_HTTP_CLIENT_H
#define ASYNC_HTTP_CLIENT_H

#include <Arduino.h>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "HttpCommon.h"
#include "AsyncTransport.h"
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
#include "GzipDecoder.h"
#endif
#include <AsyncTCP.h>
#if defined(ARDUINO_ARCH_ESP32) && defined(ASYNC_HTTP_ENABLE_AUTOLOOP)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif

class CookieJar;
class ConnectionPool;
class RedirectHandler;

class AsyncHttpClient {
  public:
    typedef std::function<void(std::shared_ptr<AsyncHttpResponse>)> SuccessCallback;
    typedef std::function<void(HttpClientError, const char*)> ErrorCallback;
    // Body chunk data is only valid during the callback; copy if you need it later.
    typedef std::function<void(const char* data, size_t len, bool final)> BodyChunkCallback; // global
    // (Per-request chunk callback removed for API simplification)

    enum class RedirectHeaderPolicy {
        // Cross-origin redirects forward only a small safe set (+ any added via allowlist).
        kDropAllCrossOrigin,
        // Legacy heuristic-based filtering on cross-origin redirects.
        kLegacyDropSensitiveOnly,
        // Preserve all request headers across redirects (unsafe).
        kPreserveAll
    };

    AsyncHttpClient();
    ~AsyncHttpClient();

    // Main HTTP methods
    uint32_t get(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    uint32_t post(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    uint32_t put(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    uint32_t del(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    uint32_t head(const char* url, SuccessCallback onSuccess, ErrorCallback onError = nullptr);
    uint32_t patch(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError = nullptr);


    // Configuration methods
    void setHeader(const char* name, const char* value);
    void removeHeader(const char* name);
    void clearHeaders();
    void setTimeout(uint32_t timeout); // total request timeout
    void setUserAgent(const char* userAgent);
    void setDefaultConnectTimeout(uint32_t ms) {
        _defaultConnectTimeout = ms;
    }
    void setFollowRedirects(bool enable, uint8_t maxHops = 3);
    void setMaxHeaderBytes(size_t maxBytes);
    void setMaxBodySize(size_t maxSize);
    void setMaxParallel(uint16_t maxParallel);
    void setDefaultTlsConfig(const AsyncHttpTLSConfig& config);
    void setTlsCACert(const char* pem);
    void setTlsClientCert(const char* certPem, const char* privateKeyPem);
    void setTlsFingerprint(const char* fingerprintHex);
    void setTlsInsecure(bool allowInsecure);
    void setTlsHandshakeTimeout(uint32_t timeoutMs);
    void setKeepAlive(bool enable, uint16_t idleMs = 5000);
    AsyncHttpTLSConfig getDefaultTlsConfig() const {
        return _defaultTlsConfig;
    }
    void clearCookies();
    // By default, Domain= attributes are rejected unless they exactly match the request host.
    // To allow a server to set cookies for a parent domain (e.g., Domain=example.com from api.example.com),
    // enable this and add allowed parent domains explicitly.
    void setAllowCookieDomainAttribute(bool enable);
    void addAllowedCookieDomain(const char* domain);
    void clearAllowedCookieDomains();
    void setCookie(const char* name, const char* value, const char* path = "/", const char* domain = nullptr,
                   bool secure = false);
    void setRedirectHeaderPolicy(RedirectHeaderPolicy policy);
    void addRedirectSafeHeader(const char* name);
    void clearRedirectSafeHeaders();

    // Advanced request method
    uint32_t request(std::unique_ptr<AsyncHttpRequest> request, SuccessCallback onSuccess,
                     ErrorCallback onError = nullptr);
    // Removed per-request chunk overload (was experimental)
    // Abort by id (returns true if found and aborted)
    bool abort(uint32_t requestId);

    // Global streaming body callback (applies for all responses unless overridden per-request in future)
    void onBodyChunk(BodyChunkCallback cb) {
        // Protect against concurrent auto-loop task updates
        lock();
        _bodyChunkCallback = cb;
        unlock();
    }

    void loop(); // manual timeout / queue progression

  private:
    friend class CookieJar;
    friend class ConnectionPool;
    friend class RedirectHandler;

    // Lightweight locking helpers (no-op unless ESP32 auto-loop task is enabled)
    void lock() const;
    void unlock() const;

    // Internal auto-loop task for fallback timeout mode
#if !ASYNC_TCP_HAS_TIMEOUT && defined(ARDUINO_ARCH_ESP32) && defined(ASYNC_HTTP_ENABLE_AUTOLOOP)
    static void _autoLoopTaskThunk(void* param);
    TaskHandle_t _autoLoopTaskHandle = nullptr;
#endif

    struct RequestContext {
        struct ChunkParseState {
            bool chunked = false;
            bool chunkedComplete = false;
            size_t currentChunkRemaining = 0;
            bool awaitingFinalChunkTerminator = false;
            size_t trailerLineCount = 0;
        };

        struct RedirectState {
            uint8_t redirectCount = 0;
        };

        struct TimingState {
            uint32_t connectStartMs = 0;
            uint32_t connectTimeoutMs = 0;
#if !ASYNC_TCP_HAS_TIMEOUT
            uint32_t timeoutTimer = 0;
#endif
        };

#if ASYNC_HTTP_ENABLE_GZIP_DECODE
        struct GzipState {
            bool gzipEncoded = false;
            bool gzipDecodeActive = false;
            GzipDecoder gzipDecoder;
        };
#endif

        std::unique_ptr<AsyncHttpRequest> request;
        std::shared_ptr<AsyncHttpResponse> response;
        SuccessCallback onSuccess;
        ErrorCallback onError;
        AsyncTransport* transport = nullptr;
        String responseBuffer;
        bool headersComplete = false;
        bool responseProcessed = false;
        size_t expectedContentLength = 0;
        size_t receivedContentLength = 0;
        size_t receivedBodyLength = 0;
        ChunkParseState chunk;
        uint32_t id = 0;
        RedirectState redirect;
        bool notifiedEndCallback = false;
        // perRequestChunkCb removed
        TimingState timing;
        bool headersSent = false;
        bool streamingBodyInProgress = false;
        bool requestKeepAlive = false;
        bool serverRequestedClose = false;
        bool usingPooledConnection = false;
        AsyncHttpTLSConfig resolvedTlsConfig;
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
        GzipState gzip;
#endif
    };

    std::vector<HttpHeader> _defaultHeaders;
    uint32_t _defaultTimeout; // total
    String _defaultUserAgent;
    BodyChunkCallback _bodyChunkCallback;
    uint32_t _nextRequestId = 1;
    uint16_t _maxParallel = 0; // 0 => unlimited
    size_t _maxBodySize = 0;   // 0 => unlimited
    bool _followRedirects = false;
    uint8_t _maxRedirectHops = 3;
    size_t _maxHeaderBytes = 0;
    std::vector<std::unique_ptr<RequestContext>> _activeRequests;
    std::vector<std::unique_ptr<RequestContext>> _pendingQueue;
    uint32_t _defaultConnectTimeout = 5000;
    AsyncHttpTLSConfig _defaultTlsConfig;
    bool _keepAliveEnabled = false;
    uint32_t _keepAliveIdleMs = 5000;
    std::unique_ptr<CookieJar> _cookieJar;
    std::unique_ptr<ConnectionPool> _connectionPool;
    std::unique_ptr<RedirectHandler> _redirectHandler;

#if defined(ARDUINO_ARCH_ESP32) && defined(ASYNC_HTTP_ENABLE_AUTOLOOP)
    mutable SemaphoreHandle_t _reqMutex = nullptr; // recursive mutex
#endif

    // Internal methods
    uint32_t makeRequest(HttpMethod method, const char* url, const char* data, SuccessCallback onSuccess,
                         ErrorCallback onError);
    void executeOrQueue(std::unique_ptr<RequestContext> context);
    void executeRequest(RequestContext* context);
    void handleConnect(RequestContext* context);
    void handleData(RequestContext* context, char* data, size_t len);
    bool wouldExceedBodyLimit(RequestContext* context, size_t incoming, bool enforceLimit) const;
    bool emitBodyBytes(RequestContext* context, const char* out, size_t outLen, bool storeBody, bool enforceLimit);
    bool deliverWireBytes(RequestContext* context, const char* wire, size_t wireLen, bool storeBody, bool enforceLimit);
    bool finalizeDecoding(RequestContext* context, bool storeBody, bool enforceLimit);
    void handleDisconnect(RequestContext* context);
    void handleTransportError(RequestContext* context, HttpClientError error, const char* message);
    bool parseResponseHeaders(RequestContext* context, const String& headerData);
    static bool parseChunkSizeLine(const String& line, uint32_t* outSize);
    void processResponse(RequestContext* context);
    void cleanup(RequestContext* context);
    void triggerError(RequestContext* context, HttpClientError errorCode, const char* errorMessage);
    void tryDequeue();
    void sendStreamData(RequestContext* context);
    bool shouldEnforceBodyLimit(RequestContext* context);
    AsyncTransport* buildTransport(RequestContext* context);
    AsyncHttpTLSConfig resolveTlsConfig(const AsyncHttpRequest* request) const;

  private:
};

#endif // ASYNC_HTTP_CLIENT_H
