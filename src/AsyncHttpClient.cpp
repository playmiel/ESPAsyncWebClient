

#include "AsyncHttpClient.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <ctime>
#include "UrlParser.h"

static constexpr size_t kMaxChunkSizeLineLen = 64;
static constexpr size_t kMaxChunkTrailerLineLen = 256;
static constexpr size_t kMaxChunkTrailerLines = 32;
static constexpr size_t kDefaultMaxHeaderBytes = 2800; // ~2.8 KiB
static constexpr size_t kDefaultMaxBodyBytes = 8192;   // 8 KiB
static constexpr size_t kMaxCookieCount = 16;
static constexpr size_t kMaxCookieBytes = 4096;

static String normalizeDomainForStorage(const String& domain) {
    String cleaned = domain;
    cleaned.trim();
    if (cleaned.startsWith("."))
        cleaned.remove(0, 1);
    cleaned.toLowerCase();
    return cleaned;
}

static bool equalsIgnoreCase(const String& a, const char* b) {
    size_t lenA = a.length();
    size_t lenB = strlen(b);
    if (lenA != lenB)
        return false;
    for (size_t i = 0; i < lenA; ++i) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

static int64_t currentTimeSeconds() {
    time_t now = time(nullptr);
    if (now > 0)
        return static_cast<int64_t>(now);
    // Fallback to millis-based monotonic clock when wall time is not set
    return static_cast<int64_t>(millis() / 1000);
}

static int monthFromAbbrev(const char* mon) {
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (!mon || strlen(mon) < 3)
        return -1;
    for (int i = 0; i < 12; ++i) {
        if (tolower((unsigned char)mon[0]) == tolower((unsigned char)kMonths[i][0]) &&
            tolower((unsigned char)mon[1]) == tolower((unsigned char)kMonths[i][1]) &&
            tolower((unsigned char)mon[2]) == tolower((unsigned char)kMonths[i][2])) {
            return i;
        }
    }
    return -1;
}

static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    // Howard Hinnant's days_from_civil, offset so 1970-01-01 yields 0
    y -= m <= 2 ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);           // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // [0, 146096]
    return era * 146097 + static_cast<int>(doe) - 719468;
}

static bool makeUtcTimestamp(int year, int month, int day, int hour, int minute, int second, int64_t* outEpoch) {
    if (!outEpoch)
        return false;
    if (month < 1 || month > 12 || day < 1 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
        second > 60)
        return false;
    static const uint8_t kMonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    uint8_t maxDay = kMonthDays[month - 1] + ((leap && month == 2) ? 1 : 0);
    if (static_cast<uint8_t>(day) > maxDay)
        return false;
    int64_t days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + second;
    *outEpoch = seconds;
    return true;
}

static bool parseHttpDate(const String& value, int64_t* outEpoch) {
    if (!outEpoch)
        return false;
    String date = value;
    date.trim();
    if (date.length() < 20) // Shorter than "01 Jan 1970 00:00:00 GMT"
        return false;
    int comma = date.indexOf(',');
    if (comma != -1)
        date = date.substring(comma + 1);
    date.trim();

    int day = 0, year = 0, hour = 0, minute = 0, second = 0;
    char monthBuf[4] = {0};
    char tzBuf[4] = {0};
    int matched = sscanf(date.c_str(), "%d %3s %d %d:%d:%d %3s", &day, monthBuf, &year, &hour, &minute, &second, tzBuf);
    if (matched < 6)
        return false;
    if (matched == 6)
        strncpy(tzBuf, "GMT", sizeof(tzBuf));
    if (!(equalsIgnoreCase(String(tzBuf), "GMT") || equalsIgnoreCase(String(tzBuf), "UTC")))
        return false;
    int month = monthFromAbbrev(monthBuf);
    if (month < 0)
        return false;
    int64_t epoch = 0;
    if (!makeUtcTimestamp(year, month + 1, day, hour, minute, second, &epoch))
        return false;
    *outEpoch = epoch;
    return true;
}

AsyncHttpClient::AsyncHttpClient()
    : _defaultTimeout(10000), _defaultUserAgent(String("ESPAsyncWebClient/") + ESP_ASYNC_WEB_CLIENT_VERSION),
      _bodyChunkCallback(nullptr), _maxBodySize(kDefaultMaxBodyBytes), _followRedirects(false), _maxRedirectHops(3),
      _maxHeaderBytes(kDefaultMaxHeaderBytes) {
#if defined(ARDUINO_ARCH_ESP32) && defined(ASYNC_HTTP_ENABLE_AUTOLOOP)
    // Create recursive mutex for shared containers when auto-loop may run in background
    _reqMutex = xSemaphoreCreateRecursiveMutex();
#endif
#if !ASYNC_TCP_HAS_TIMEOUT && defined(ARDUINO_ARCH_ESP32) && defined(ASYNC_HTTP_ENABLE_AUTOLOOP)
    // Optional: spawn a lightweight auto-loop task so users don't need to call client.loop() manually.
    xTaskCreatePinnedToCore(_autoLoopTaskThunk,   // task entry
                            "AsyncHttpAutoLoop",  // name
                            2048,                 // stack words
                            this,                 // parameter
                            1,                    // priority (low)
                            &_autoLoopTaskHandle, // handle out
                            tskNO_AFFINITY        // any core
    );
#endif
}

AsyncHttpClient::~AsyncHttpClient() {
#if !ASYNC_TCP_HAS_TIMEOUT && defined(ARDUINO_ARCH_ESP32) && defined(ASYNC_HTTP_ENABLE_AUTOLOOP)
    if (_autoLoopTaskHandle) {
        TaskHandle_t h = _autoLoopTaskHandle;
        _autoLoopTaskHandle = nullptr;
        vTaskDelete(h);
    }
#endif
#if defined(ARDUINO_ARCH_ESP32) && defined(ASYNC_HTTP_ENABLE_AUTOLOOP)
    if (_reqMutex) {
        vSemaphoreDelete(_reqMutex);
        _reqMutex = nullptr;
    }
#endif
    for (auto& pooled : _idleConnections) {
        if (pooled.transport) {
            pooled.transport->close(true);
            delete pooled.transport;
            pooled.transport = nullptr;
        }
    }
    _idleConnections.clear();
}

#if defined(ARDUINO_ARCH_ESP32) && defined(ASYNC_HTTP_ENABLE_AUTOLOOP)
void AsyncHttpClient::lock() const {
    if (_reqMutex)
        xSemaphoreTakeRecursive(_reqMutex, portMAX_DELAY);
}
void AsyncHttpClient::unlock() const {
    if (_reqMutex)
        xSemaphoreGiveRecursive(_reqMutex);
}
#else
void AsyncHttpClient::lock() const {}
void AsyncHttpClient::unlock() const {}
#endif

#if !ASYNC_TCP_HAS_TIMEOUT && defined(ARDUINO_ARCH_ESP32) && defined(ASYNC_HTTP_ENABLE_AUTOLOOP)
void AsyncHttpClient::_autoLoopTaskThunk(void* param) {
    AsyncHttpClient* self = static_cast<AsyncHttpClient*>(param);
    const TickType_t delayTicks = pdMS_TO_TICKS(20); // ~50 Hz tick
    while (true) {
        if (self)
            self->loop();
        vTaskDelay(delayTicks);
    }
}
#endif

uint32_t AsyncHttpClient::get(const char* url, SuccessCallback onSuccess, ErrorCallback onError) {
    return makeRequest(HTTP_METHOD_GET, url, nullptr, onSuccess, onError);
}
uint32_t AsyncHttpClient::post(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError) {
    return makeRequest(HTTP_METHOD_POST, url, data, onSuccess, onError);
}
uint32_t AsyncHttpClient::put(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError) {
    return makeRequest(HTTP_METHOD_PUT, url, data, onSuccess, onError);
}
uint32_t AsyncHttpClient::del(const char* url, SuccessCallback onSuccess, ErrorCallback onError) {
    return makeRequest(HTTP_METHOD_DELETE, url, nullptr, onSuccess, onError);
}
uint32_t AsyncHttpClient::head(const char* url, SuccessCallback onSuccess, ErrorCallback onError) {
    return makeRequest(HTTP_METHOD_HEAD, url, nullptr, onSuccess, onError);
}
uint32_t AsyncHttpClient::patch(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError) {
    return makeRequest(HTTP_METHOD_PATCH, url, data, onSuccess, onError);
}

void AsyncHttpClient::setHeader(const char* name, const char* value) {
    if (!name)
        return;
    String nameStr(name);
    String valueStr(value ? value : "");
    if (!isValidHttpHeaderName(nameStr) || !isValidHttpHeaderValue(valueStr))
        return;
    lock();
    for (auto& h : _defaultHeaders) {
        if (h.name.equalsIgnoreCase(nameStr)) {
            h.value = valueStr;
            unlock();
            return;
        }
    }
    _defaultHeaders.push_back(HttpHeader(nameStr, valueStr));
    unlock();
}

void AsyncHttpClient::removeHeader(const char* name) {
    if (!name)
        return;
    String nameStr(name);
    lock();
    for (auto it = _defaultHeaders.begin(); it != _defaultHeaders.end();) {
        if (it->name.equalsIgnoreCase(nameStr)) {
            it = _defaultHeaders.erase(it);
        } else {
            ++it;
        }
    }
    unlock();
}

void AsyncHttpClient::clearHeaders() {
    lock();
    _defaultHeaders.clear();
    unlock();
}

void AsyncHttpClient::setTimeout(uint32_t timeout) {
    _defaultTimeout = timeout;
}
void AsyncHttpClient::setUserAgent(const char* userAgent) {
    lock();
    _defaultUserAgent = userAgent ? String(userAgent) : String();
    unlock();
}

void AsyncHttpClient::setFollowRedirects(bool enable, uint8_t maxHops) {
    lock();
    _followRedirects = enable;
    if (maxHops == 0)
        maxHops = 1;
    _maxRedirectHops = maxHops;
    unlock();
}

void AsyncHttpClient::setRedirectHeaderPolicy(RedirectHeaderPolicy policy) {
    lock();
    _redirectHeaderPolicy = policy;
    unlock();
}

void AsyncHttpClient::addRedirectSafeHeader(const char* name) {
    if (!name || strlen(name) == 0)
        return;
    String headerName(name);
    headerName.trim();
    if (headerName.length() == 0)
        return;
    headerName.toLowerCase();
    lock();
    for (const auto& existing : _redirectSafeHeaders) {
        if (existing.equalsIgnoreCase(headerName)) {
            unlock();
            return;
        }
    }
    _redirectSafeHeaders.push_back(headerName);
    unlock();
}

void AsyncHttpClient::clearRedirectSafeHeaders() {
    lock();
    _redirectSafeHeaders.clear();
    unlock();
}

void AsyncHttpClient::setMaxHeaderBytes(size_t maxBytes) {
    lock();
    _maxHeaderBytes = maxBytes;
    unlock();
}

void AsyncHttpClient::setMaxBodySize(size_t maxSize) {
    lock();
    _maxBodySize = maxSize;
    unlock();
}

void AsyncHttpClient::setMaxParallel(uint16_t maxParallel) {
    lock();
    _maxParallel = maxParallel;
    unlock();
    tryDequeue();
}

void AsyncHttpClient::setDefaultTlsConfig(const AsyncHttpTLSConfig& config) {
    lock();
    _defaultTlsConfig = config;
    if (_defaultTlsConfig.handshakeTimeoutMs == 0)
        _defaultTlsConfig.handshakeTimeoutMs = 12000;
    unlock();
}

void AsyncHttpClient::setTlsCACert(const char* pem) {
    lock();
    _defaultTlsConfig.caCert = pem ? String(pem) : String();
    unlock();
}

void AsyncHttpClient::setTlsClientCert(const char* certPem, const char* privateKeyPem) {
    lock();
    _defaultTlsConfig.clientCert = certPem ? String(certPem) : String();
    _defaultTlsConfig.clientPrivateKey = privateKeyPem ? String(privateKeyPem) : String();
    unlock();
}

void AsyncHttpClient::setTlsFingerprint(const char* fingerprintHex) {
    lock();
    _defaultTlsConfig.fingerprint = fingerprintHex ? String(fingerprintHex) : String();
    unlock();
}

void AsyncHttpClient::setTlsInsecure(bool allowInsecure) {
    lock();
    _defaultTlsConfig.insecure = allowInsecure;
    unlock();
}

void AsyncHttpClient::setTlsHandshakeTimeout(uint32_t timeoutMs) {
    lock();
    _defaultTlsConfig.handshakeTimeoutMs = timeoutMs;
    unlock();
}

void AsyncHttpClient::setKeepAlive(bool enable, uint16_t idleMs) {
    std::vector<PooledConnection> dropped;
    lock();
    _keepAliveEnabled = enable;
    _keepAliveIdleMs = idleMs == 0 ? 1000 : idleMs;
    if (!enable && !_idleConnections.empty()) {
        dropped.swap(_idleConnections);
    }
    unlock();
    for (auto& conn : dropped) {
        if (conn.transport) {
            conn.transport->close(true);
            delete conn.transport;
        }
    }
}

void AsyncHttpClient::clearCookies() {
    lock();
    _cookies.clear();
    unlock();
}

void AsyncHttpClient::setAllowCookieDomainAttribute(bool enable) {
    lock();
    _allowCookieDomainAttribute = enable;
    unlock();
}

void AsyncHttpClient::addAllowedCookieDomain(const char* domain) {
    if (!domain || strlen(domain) == 0)
        return;
    String normalized = normalizeDomainForStorage(String(domain));
    if (normalized.length() == 0)
        return;
    if (normalized.indexOf('.') == -1)
        return;
    lock();
    for (const auto& existing : _allowedCookieDomains) {
        if (existing.equalsIgnoreCase(normalized)) {
            unlock();
            return;
        }
    }
    _allowedCookieDomains.push_back(normalized);
    unlock();
}

void AsyncHttpClient::clearAllowedCookieDomains() {
    lock();
    _allowedCookieDomains.clear();
    unlock();
}

void AsyncHttpClient::setCookie(const char* name, const char* value, const char* path, const char* domain,
                                bool secure) {
    if (!name || strlen(name) == 0)
        return;
    if (!isValidHttpHeaderValue(String(name)))
        return;
    if (strchr(name, '=') || strchr(name, ';'))
        return;
    if (value && !isValidHttpHeaderValue(String(value)))
        return;
    int64_t now = currentTimeSeconds();
    StoredCookie cookie;
    cookie.name = String(name);
    cookie.value = value ? String(value) : String();
    cookie.path = (path && strlen(path) > 0) ? String(path) : String("/");
    cookie.domain = domain ? String(domain) : String();
    cookie.hostOnly = false; // Manual cookies are treated as domain cookies (domain=="" means "any host").
    cookie.secure = secure;
    cookie.createdAt = now;
    cookie.lastAccessAt = now;
    if (!cookie.path.startsWith("/"))
        cookie.path = "/" + cookie.path;
    if (cookie.domain.startsWith("."))
        cookie.domain.remove(0, 1);

    lock();
    purgeExpiredCookies(now);
    for (auto it = _cookies.begin(); it != _cookies.end();) {
        if (it->name.equalsIgnoreCase(cookie.name) && it->domain.equalsIgnoreCase(cookie.domain) &&
            it->path.equals(cookie.path)) {
            it = _cookies.erase(it);
        } else {
            ++it;
        }
    }
    if (!cookie.value.isEmpty()) {
        if (_cookies.size() >= kMaxCookieCount)
            evictOneCookieLocked();
        _cookies.push_back(cookie);
    }
    unlock();
}

uint32_t AsyncHttpClient::makeRequest(HttpMethod method, const char* url, const char* data, SuccessCallback onSuccess,
                                      ErrorCallback onError) {
    if (!url || strlen(url) == 0) {
        if (onError)
            onError(CONNECTION_FAILED, "URL is empty");
        return 0;
    }
    // Snapshot global defaults under lock to avoid concurrent modification issues
    std::vector<HttpHeader> headersCopy;
    String uaCopy;
    uint32_t timeoutCopy;
    lock();
    headersCopy = _defaultHeaders; // copy
    uaCopy = _defaultUserAgent;    // copy
    timeoutCopy = _defaultTimeout; // copy
    unlock();

    AsyncHttpRequest* request = new AsyncHttpRequest(method, String(url));
    for (const auto& h : headersCopy)
        request->setHeader(h.name, h.value);
    request->setUserAgent(uaCopy);
    request->setTimeout(timeoutCopy);
    if (_keepAliveEnabled) {
        request->setHeader("Connection", "keep-alive");
        uint16_t timeoutSec = static_cast<uint16_t>(std::max<uint32_t>(1, _keepAliveIdleMs / 1000));
        request->setHeader("Keep-Alive", String("timeout=") + String(timeoutSec));
    }
    if (data) {
        request->setBody(String(data));
        request->setHeader("Content-Type", "application/x-www-form-urlencoded");
    }
    request->finalizeQueryParams(); // ensure built queries closed
    return this->request(request, onSuccess, onError);
}

uint32_t AsyncHttpClient::request(AsyncHttpRequest* request, SuccessCallback onSuccess, ErrorCallback onError) {
    if (!request) {
        if (onError)
            onError(CONNECTION_FAILED, "Request is null");
        return 0;
    }
    if (request->getHost().length() == 0 || request->getPath().length() == 0) {
        if (onError)
            onError(CONNECTION_FAILED, "Invalid URL");
        delete request;
        return 0;
    }
    RequestContext* ctx = new RequestContext();
    ctx->request = request;
    ctx->response = new AsyncHttpResponse();
    ctx->onSuccess = onSuccess;
    ctx->onError = onError;
    ctx->id = _nextRequestId++;
    ctx->connectTimeoutMs = _defaultConnectTimeout;
    if (_keepAliveEnabled && request) {
        String conn = request->getHeader("Connection");
        if (conn.isEmpty())
            request->setHeader("Connection", "keep-alive");
        if (request->getHeader("Keep-Alive").isEmpty()) {
            uint16_t timeoutSec = static_cast<uint16_t>(std::max<uint32_t>(1, _keepAliveIdleMs / 1000));
            request->setHeader("Keep-Alive", String("timeout=") + String(timeoutSec));
        }
    }
    executeOrQueue(ctx);
    return ctx->id;
}

// Removed per-request chunk overload

bool AsyncHttpClient::abort(uint32_t requestId) {
    // Active requests: we must be careful as triggerError() will cleanup and erase from _activeRequests
    lock();
    for (size_t i = 0; i < _activeRequests.size(); ++i) {
        RequestContext* ctx = _activeRequests[i];
        if (ctx->id == requestId && !ctx->responseProcessed) {
            unlock();
            triggerError(ctx, ABORTED, "Aborted by user");
            return true;
        }
    }
    // Pending queue: still not executed
    for (auto it = _pendingQueue.begin(); it != _pendingQueue.end(); ++it) {
        if ((*it)->id == requestId) {
            RequestContext* ctx = *it;
            _pendingQueue.erase(it);
            unlock();
            triggerError(ctx, ABORTED, "Aborted by user");
            return true;
        }
    }
    unlock();
    return false;
}

void AsyncHttpClient::executeOrQueue(RequestContext* context) {
    lock();
    if (_maxParallel > 0 && _activeRequests.size() >= _maxParallel) {
        _pendingQueue.push_back(context);
        unlock();
        return;
    }
    unlock();
    executeRequest(context);
}

void AsyncHttpClient::executeRequest(RequestContext* context) {
    applyCookies(context->request);
    context->connectStartMs = millis();
    context->resolvedTlsConfig = resolveTlsConfig(context->request);
    String connHeader = context->request->getHeader("Connection");
    context->requestKeepAlive = _keepAliveEnabled && !equalsIgnoreCase(connHeader, "close");
    AsyncTransport* pooled = nullptr;
    if (context->requestKeepAlive)
        pooled = checkoutPooledTransport(context->request, context->resolvedTlsConfig);
    context->transport = pooled ? pooled : buildTransport(context);
    context->usingPooledConnection = pooled != nullptr;
    if (!context->transport) {
        triggerError(context, HTTPS_NOT_SUPPORTED, "HTTPS transport unavailable");
        return;
    }
    if (context->usingPooledConnection)
        context->connectTimeoutMs = 0;
    lock();
    bool alreadyTracked = std::find(_activeRequests.begin(), _activeRequests.end(), context) != _activeRequests.end();
    if (!alreadyTracked)
        _activeRequests.push_back(context);
    unlock();

    context->transport->setConnectHandler(
        [this](void* arg, AsyncTransport* t) {
            (void)t;
            auto ctx = static_cast<RequestContext*>(arg);
            handleConnect(ctx);
        },
        context);
    context->transport->setDataHandler(
        [this](void* arg, AsyncTransport* t, void* data, size_t len) {
            (void)t;
            auto ctx = static_cast<RequestContext*>(arg);
            handleData(ctx, static_cast<char*>(data), len);
        },
        context);
    context->transport->setDisconnectHandler(
        [this](void* arg, AsyncTransport* t) {
            (void)t;
            auto ctx = static_cast<RequestContext*>(arg);
            handleDisconnect(ctx);
        },
        context);
    context->transport->setErrorHandler(
        [this](void* arg, AsyncTransport* t, HttpClientError error, const char* message) {
            (void)t;
            auto ctx = static_cast<RequestContext*>(arg);
            handleTransportError(ctx, error, message);
        },
        context);

#if ASYNC_TCP_HAS_TIMEOUT
    context->transport->setTimeout(context->request->getTimeout());
    context->transport->setTimeoutHandler(
        [this](void* arg, AsyncTransport* transport, uint32_t t) {
            (void)transport;
            auto ctx = static_cast<RequestContext*>(arg);
            triggerError(ctx, REQUEST_TIMEOUT, "Request timeout");
        },
        context);
#else
    context->timeoutTimer = millis();
#endif

    if (context->usingPooledConnection) {
        handleConnect(context); // Already connected, just send request
    } else if (!context->transport->connect(context->request->getHost().c_str(), context->request->getPort())) {
        triggerError(context, CONNECTION_FAILED, "Failed to initiate connection");
        return;
    }
}

void AsyncHttpClient::handleConnect(RequestContext* context) {
    if (!context || !context->transport)
        return;
    if (context->request->hasBodyStream()) {
        String headers = context->request->buildHeadersOnly();
        context->transport->write(headers.c_str(), headers.length());
        context->headersSent = true;
        context->streamingBodyInProgress = true;
        sendStreamData(context);
    } else {
        String full = context->request->buildHttpRequest();
        context->transport->write(full.c_str(), full.length());
        context->headersSent = true;
    }
}

void AsyncHttpClient::handleData(RequestContext* context, char* data, size_t len) {
    if (!context)
        return;
    bool storeBody = context->request && !context->request->getNoStoreBody();
    bool bufferThisChunk = (!context->headersComplete || context->chunked);
    if (bufferThisChunk)
        context->responseBuffer.concat(data, len);
    bool enforceLimit = shouldEnforceBodyLimit(context);
    auto wouldExceedBodyLimit = [&](size_t incoming) -> bool {
        if (!enforceLimit)
            return false;
        size_t current = context->receivedBodyLength;
        if (current >= _maxBodySize)
            return true;
        return incoming > (_maxBodySize - current);
    };

    auto emitBodyBytes = [&](const char* out, size_t outLen) -> bool {
        if (!out || outLen == 0)
            return true;
        if (wouldExceedBodyLimit(outLen)) {
            triggerError(context, MAX_BODY_SIZE_EXCEEDED, "Body exceeds configured maximum");
            return false;
        }
        if (storeBody) {
            context->response->appendBody(out, outLen);
        }
        context->receivedBodyLength += outLen;
        auto cb = _bodyChunkCallback;
        if (cb)
            cb(out, outLen, false);
        return true;
    };

    auto deliverWireBytes = [&](const char* wire, size_t wireLen) -> bool {
        if (wireLen == 0)
            return true;
        context->receivedContentLength += wireLen;
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
        if (context->gzipDecodeActive) {
            size_t offset = 0;
            while (offset < wireLen) {
                const uint8_t* outPtr = nullptr;
                size_t outLen = 0;
                size_t consumed = 0;
                GzipDecoder::Result r = context->gzipDecoder.write(reinterpret_cast<const uint8_t*>(wire + offset),
                                                                  wireLen - offset, &consumed, &outPtr, &outLen, true);
                if (outLen > 0) {
                    if (!emitBodyBytes(reinterpret_cast<const char*>(outPtr), outLen))
                        return false;
                }
                if (r == GzipDecoder::Result::kError) {
                    triggerError(context, GZIP_DECODE_FAILED, context->gzipDecoder.lastError());
                    return false;
                }
                offset += consumed;
                if (consumed == 0 && outLen == 0) {
                    triggerError(context, GZIP_DECODE_FAILED, "Gzip decoder stalled");
                    return false;
                }
                if (r == GzipDecoder::Result::kNeedMoreInput && offset >= wireLen) {
                    break;
                }
            }
            return true;
        }
#endif
        return emitBodyBytes(wire, wireLen);
    };

    auto finalizeDecoding = [&]() -> bool {
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
        if (!context->gzipDecodeActive)
            return true;
        for (;;) {
            const uint8_t* outPtr = nullptr;
            size_t outLen = 0;
            GzipDecoder::Result r = context->gzipDecoder.finish(&outPtr, &outLen);
            if (outLen > 0) {
                if (!emitBodyBytes(reinterpret_cast<const char*>(outPtr), outLen))
                    return false;
            }
            if (r == GzipDecoder::Result::kDone)
                return true;
            if (r == GzipDecoder::Result::kOk)
                continue;
            triggerError(context, GZIP_DECODE_FAILED, context->gzipDecoder.lastError());
            return false;
        }
#else
        return true;
#endif
    };

    if (!context->headersComplete) {
        int headerEnd = context->responseBuffer.indexOf("\r\n\r\n");
        if (_maxHeaderBytes > 0) {
            size_t headerBytes = headerEnd != -1 ? static_cast<size_t>(headerEnd + 4)
                                                 : static_cast<size_t>(context->responseBuffer.length());
            if (headerBytes > _maxHeaderBytes) {
                triggerError(context, HEADERS_TOO_LARGE, "Response headers exceed configured maximum");
                return;
            }
        }
        if (headerEnd != -1) {
            String headerData = context->responseBuffer.substring(0, headerEnd);
            if (parseResponseHeaders(context, headerData)) {
                context->headersComplete = true;
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
                bool gzipActive = context->gzipDecodeActive;
#else
                bool gzipActive = false;
#endif
                if (enforceLimit && !gzipActive && context->expectedContentLength > 0 &&
                    context->expectedContentLength > _maxBodySize) {
                    triggerError(context, MAX_BODY_SIZE_EXCEEDED, "Body exceeds configured maximum");
                    return;
                }
                if (storeBody && !gzipActive && context->expectedContentLength > 0 && !context->chunked &&
                    (!enforceLimit || context->expectedContentLength <= _maxBodySize)) {
                    context->response->reserveBody(context->expectedContentLength);
                }
                context->responseBuffer.remove(0, headerEnd + 4);
                if (handleRedirect(context))
                    return;
                if (!context->chunked && context->responseBuffer.length() > 0) {
                    size_t incomingLen = context->responseBuffer.length();
                    if (!gzipActive && wouldExceedBodyLimit(incomingLen)) {
                        triggerError(context, MAX_BODY_SIZE_EXCEEDED, "Body exceeds configured maximum");
                        return;
                    }
                    if (!deliverWireBytes(context->responseBuffer.c_str(), incomingLen))
                        return;
                    context->responseBuffer = "";
                }
            } else {
                triggerError(context, HEADER_PARSE_FAILED, "Failed to parse response headers");
                return;
            }
        }
    } else if (!context->chunked) {
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
        bool gzipActive = context->gzipDecodeActive;
#else
        bool gzipActive = false;
#endif
        if (!gzipActive && wouldExceedBodyLimit(len)) {
            triggerError(context, MAX_BODY_SIZE_EXCEEDED, "Body exceeds configured maximum");
            return;
        }
        if (!deliverWireBytes(data, len))
            return;
    }

    while (context->headersComplete && context->chunked && !context->chunkedComplete) {
        if (context->awaitingFinalChunkTerminator) {
            int lineEndT = context->responseBuffer.indexOf("\r\n");
            if (lineEndT == -1) {
                int lfPos = context->responseBuffer.indexOf('\n');
                if (lfPos != -1 && (lfPos == 0 || context->responseBuffer.charAt(lfPos - 1) != '\r')) {
                    triggerError(context, CHUNKED_DECODE_FAILED, "Chunk trailer missing CRLF");
                    return;
                }
                break;
            }
            if (lineEndT == 0) {
                context->responseBuffer.remove(0, 2);
                context->awaitingFinalChunkTerminator = false;
                context->chunkedComplete = true;
                continue;
            }
            if (lineEndT > (int)kMaxChunkTrailerLineLen) {
                triggerError(context, CHUNKED_DECODE_FAILED, "Chunk trailer line too long");
                return;
            }
            if (context->trailerLineCount >= kMaxChunkTrailerLines) {
                triggerError(context, CHUNKED_DECODE_FAILED, "Too many chunk trailers");
                return;
            }
            String trailerLine = context->responseBuffer.substring(0, lineEndT);
            int colonPos = trailerLine.indexOf(':');
            if (colonPos == -1) {
                triggerError(context, CHUNKED_DECODE_FAILED, "Chunk trailer missing colon");
                return;
            }
            String name = trailerLine.substring(0, colonPos);
            String value = trailerLine.substring(colonPos + 1);
            name.trim();
            value.trim();
            if (name.length() == 0) {
                triggerError(context, CHUNKED_DECODE_FAILED, "Chunk trailer name empty");
                return;
            }
            context->response->setTrailer(name, value);
            context->trailerLineCount++;
            context->responseBuffer.remove(0, lineEndT + 2);
            continue;
        }

        if (context->currentChunkRemaining == 0) {
            if (context->responseBuffer.length() > kMaxChunkSizeLineLen &&
                context->responseBuffer.indexOf("\r\n") == -1) {
                triggerError(context, CHUNKED_DECODE_FAILED, "Chunk size line too long");
                return;
            }
            int lineEnd = context->responseBuffer.indexOf("\r\n");
            if (lineEnd == -1) {
                int lfPos = context->responseBuffer.indexOf('\n');
                if (lfPos != -1 && (lfPos == 0 || context->responseBuffer.charAt(lfPos - 1) != '\r')) {
                    triggerError(context, CHUNKED_DECODE_FAILED, "Chunk size missing CRLF");
                    return;
                }
                break;
            }
            if (lineEnd > (int)kMaxChunkSizeLineLen) {
                triggerError(context, CHUNKED_DECODE_FAILED, "Chunk size line too long");
                return;
            }
            String sizeLine = context->responseBuffer.substring(0, lineEnd);
            sizeLine.trim();
            uint32_t chunkSize = 0;
            if (!parseChunkSizeLine(sizeLine, &chunkSize)) {
                triggerError(context, CHUNKED_DECODE_FAILED, "Chunk size parse error");
                return;
            }
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
            bool gzipActive = context->gzipDecodeActive;
#else
            bool gzipActive = false;
#endif
            if (!gzipActive && chunkSize > 0 && wouldExceedBodyLimit(chunkSize)) {
                triggerError(context, MAX_BODY_SIZE_EXCEEDED, "Body exceeds configured maximum");
                return;
            }
            context->currentChunkRemaining = chunkSize;
            context->responseBuffer.remove(0, lineEnd + 2);
            if (chunkSize == 0) {
                context->awaitingFinalChunkTerminator = true;
                context->trailerLineCount = 0;
                continue;
            }
        }
        size_t needed = context->currentChunkRemaining + 2;
        if (context->responseBuffer.length() < needed)
            break;
        if (context->responseBuffer.charAt(context->currentChunkRemaining) != '\r' ||
            context->responseBuffer.charAt(context->currentChunkRemaining + 1) != '\n') {
            triggerError(context, CHUNKED_DECODE_FAILED, "Chunk missing terminating CRLF");
            return;
        }
        size_t chunkLen = context->currentChunkRemaining;
        const char* chunkPtr = context->responseBuffer.c_str();
        if (!deliverWireBytes(chunkPtr, chunkLen))
            return;
        context->responseBuffer.remove(0, needed);
        context->currentChunkRemaining = 0;
    }

    if (context->headersComplete && !context->responseProcessed) {
        bool complete = false;
        if (context->chunked && context->chunkedComplete)
            complete = true;
        else if (!context->chunked && context->expectedContentLength > 0 &&
                 context->receivedContentLength >= context->expectedContentLength)
            complete = true;
        if (complete) {
            if (!finalizeDecoding())
                return;
            processResponse(context);
        }
    }
}

bool AsyncHttpClient::parseChunkSizeLine(const String& line, uint32_t* outSize) {
    if (!outSize)
        return false;
    if (line.length() == 0)
        return false;
    if (line.length() > kMaxChunkSizeLineLen)
        return false;
    // Spec allows chunk extensions after size separated by ';'. We ignore extensions for now.
    int semi = line.indexOf(';');
    String sizePart = semi == -1 ? line : line.substring(0, semi);
    sizePart.trim();
    const char* cstr = sizePart.c_str();
    char* endptr = nullptr;
    errno = 0;
    unsigned long val = strtoul(cstr, &endptr, 16);
    if (cstr[0] == '\0')
        return false; // empty after trim
    if (endptr == cstr)
        return false; // no digits parsed
    if (errno == ERANGE)
        return false; // overflow
    // Must have consumed all hex digits; any leftover (excluding chunk extensions already removed) is invalid
    if (*endptr != '\0')
        return false;
    *outSize = (uint32_t)val;
    return true;
}

void AsyncHttpClient::handleDisconnect(RequestContext* context) {
    if (context->responseProcessed)
        return;
    if (!context->headersComplete) {
        triggerError(context, CONNECTION_CLOSED, "Connection closed before headers received");
        return;
    }
    // Headers parsed: determine if body complete.
    if (context->chunked && !context->chunkedComplete) {
        // Connection closed before receiving final chunk
        triggerError(context, CHUNKED_DECODE_FAILED, "Failed to decode chunked body");
        return;
    }
    if (!context->chunked && context->expectedContentLength > 0 &&
        context->receivedContentLength < context->expectedContentLength) {
        // Body truncated
        triggerError(context, CONNECTION_CLOSED_MID_BODY, "Truncated response");
        return;
    }
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
    if (context->gzipDecodeActive) {
        bool storeBody = context->request && !context->request->getNoStoreBody();
        bool enforceLimit = shouldEnforceBodyLimit(context);
        auto wouldExceedBodyLimit = [&](size_t incoming) -> bool {
            if (!enforceLimit)
                return false;
            size_t current = context->receivedBodyLength;
            if (current >= _maxBodySize)
                return true;
            return incoming > (_maxBodySize - current);
        };
        auto emitBodyBytes = [&](const char* out, size_t outLen) -> bool {
            if (!out || outLen == 0)
                return true;
            if (wouldExceedBodyLimit(outLen)) {
                triggerError(context, MAX_BODY_SIZE_EXCEEDED, "Body exceeds configured maximum");
                return false;
            }
            if (storeBody) {
                context->response->appendBody(out, outLen);
            }
            context->receivedBodyLength += outLen;
            auto cb = _bodyChunkCallback;
            if (cb)
                cb(out, outLen, false);
            return true;
        };
        for (;;) {
            const uint8_t* outPtr = nullptr;
            size_t outLen = 0;
            GzipDecoder::Result r = context->gzipDecoder.finish(&outPtr, &outLen);
            if (outLen > 0) {
                if (!emitBodyBytes(reinterpret_cast<const char*>(outPtr), outLen))
                    return;
            }
            if (r == GzipDecoder::Result::kDone)
                break;
            if (r == GzipDecoder::Result::kOk)
                continue;
            triggerError(context, GZIP_DECODE_FAILED, context->gzipDecoder.lastError());
            return;
        }
    }
#endif
    // Otherwise success: either Content-Length reached, or no Content-Length and closure marks the end
    processResponse(context);
}

void AsyncHttpClient::handleTransportError(RequestContext* context, HttpClientError error, const char* message) {
    if (context->responseProcessed)
        return;
    if (!message)
        message = httpClientErrorToString(error);
    triggerError(context, error, message);
}

bool AsyncHttpClient::parseResponseHeaders(RequestContext* context, const String& headerData) {
    int firstLineEnd = headerData.indexOf("\r\n");
    if (firstLineEnd == -1)
        return false;
    String statusLine = headerData.substring(0, firstLineEnd);
    int firstSpace = statusLine.indexOf(' ');
    int secondSpace = statusLine.indexOf(' ', firstSpace + 1);
    if (firstSpace == -1 || secondSpace == -1)
        return false;
    int statusCode = statusLine.substring(firstSpace + 1, secondSpace).toInt();
    String statusText = statusLine.substring(secondSpace + 1);
    context->response->setStatusCode(statusCode);
    context->response->setStatusText(statusText);
    int lineStart = firstLineEnd + 2;
    while (lineStart < headerData.length()) {
        int lineEnd = headerData.indexOf("\r\n", lineStart);
        if (lineEnd == -1)
            break;
        String line = headerData.substring(lineStart, lineEnd);
        int colonPos = line.indexOf(':');
        if (colonPos != -1) {
            String name = line.substring(0, colonPos);
            String value = line.substring(colonPos + 1);
            name.trim();
            value.trim();
            context->response->setHeader(name, value);
            if (name.equalsIgnoreCase("Content-Length")) {
                long parsed = value.toInt();
                if (parsed < 0)
                    parsed = 0;
                context->expectedContentLength = (size_t)parsed;
                context->response->setContentLength(context->expectedContentLength);
            } else if (name.equalsIgnoreCase("Transfer-Encoding") && value.equalsIgnoreCase("chunked")) {
                context->chunked = true;
            } else if (name.equalsIgnoreCase("Content-Encoding")) {
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
                String lower = value;
                lower.toLowerCase();
                if (lower.indexOf("gzip") != -1) {
                    context->gzipEncoded = true;
                    context->gzipDecodeActive = true;
                    context->gzipDecoder.begin();
                }
#endif
            } else if (name.equalsIgnoreCase("Connection")) {
                String lower = value;
                lower.toLowerCase();
                if (lower.indexOf("close") != -1)
                    context->serverRequestedClose = true;
            } else if (name.equalsIgnoreCase("Set-Cookie")) {
                storeResponseCookie(context->request, value);
            }
        }
        lineStart = lineEnd + 2;
    }
    return true;
}

void AsyncHttpClient::processResponse(RequestContext* context) {
    if (context->responseProcessed)
        return;
    if (handleRedirect(context))
        return;
    auto cb = _bodyChunkCallback;
    if (cb && !context->notifiedEndCallback) {
        context->notifiedEndCallback = true;
        cb(nullptr, 0, true);
    }
    context->responseProcessed = true;
    if (context->onSuccess)
        context->onSuccess(context->response);
    cleanup(context);
}

void AsyncHttpClient::cleanup(RequestContext* context) {
    AsyncTransport* toDelete = nullptr;
    if (context->transport) {
        if (shouldRecycleTransport(context)) {
            releaseConnectionToPool(context);
        } else {
            toDelete = context->transport;
        }
        context->transport = nullptr;
    }
    if (context->request)
        delete context->request;
    if (context->response)
        delete context->response;
    lock();
    auto it = std::find(_activeRequests.begin(), _activeRequests.end(), context);
    if (it != _activeRequests.end())
        _activeRequests.erase(it);
    unlock();
    if (toDelete) {
        toDelete->close();
        delete toDelete;
    }
    delete context;
    tryDequeue();
}

void AsyncHttpClient::triggerError(RequestContext* context, HttpClientError errorCode, const char* errorMessage) {
    if (context->responseProcessed)
        return;
    context->responseProcessed = true;
    if (context->onError)
        context->onError(errorCode, errorMessage);
    cleanup(context);
}

bool AsyncHttpClient::isRedirectStatus(int status) const {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

String AsyncHttpClient::resolveRedirectUrl(const AsyncHttpRequest* request, const String& location) const {
    if (!request)
        return String();
    String loc = location;
    loc.trim();
    if (loc.length() == 0)
        return String();
    if (loc.startsWith("http://") || loc.startsWith("https://"))
        return loc;

    String base = request->isSecure() ? "https://" : "http://";
    base += request->getHost();
    uint16_t port = request->getPort();
    bool defaultPort = request->isSecure() ? (port == 443) : (port == 80);
    if (!defaultPort) {
        base += ":";
        base += String(port);
    }

    if (loc.startsWith("//")) {
        return String(request->isSecure() ? "https:" : "http:") + loc;
    }
    String path = request->getPath();
    if (!path.startsWith("/"))
        path = "/" + path;
    String pathNoQuery = path;
    int queryIdx = pathNoQuery.indexOf('?');
    if (queryIdx != -1)
        pathNoQuery = pathNoQuery.substring(0, queryIdx);

    if (loc.startsWith("/")) {
        return base + loc;
    }
    if (loc.startsWith("?")) {
        return base + pathNoQuery + loc;
    }

    int lastSlash = pathNoQuery.lastIndexOf('/');
    String prefix;
    if (lastSlash == -1) {
        prefix = "/";
    } else {
        prefix = pathNoQuery.substring(0, lastSlash + 1);
    }
    return base + prefix + loc;
}

bool AsyncHttpClient::isSameOrigin(const AsyncHttpRequest* original, const AsyncHttpRequest* redirect) const {
    if (!original || !redirect)
        return false;
    if (!original->getHost().equalsIgnoreCase(redirect->getHost()))
        return false;
    if (original->getPort() != redirect->getPort())
        return false;
    if (original->isSecure() != redirect->isSecure())
        return false;
    return true;
}

bool AsyncHttpClient::buildRedirectRequest(RequestContext* context, AsyncHttpRequest** outRequest,
                                           HttpClientError* outError, String* outErrorMessage) {
    if (outRequest)
        *outRequest = nullptr;
    if (outError)
        *outError = CONNECTION_FAILED;
    if (outErrorMessage)
        *outErrorMessage = "";

    if (!context || !_followRedirects || !context->response || !context->request)
        return false;

    int status = context->response->getStatusCode();
    if (!isRedirectStatus(status))
        return false;

    String location = context->response->getHeader("Location");
    if (location.length() == 0)
        return false;

    if (context->redirectCount >= _maxRedirectHops) {
        if (outError)
            *outError = TOO_MANY_REDIRECTS;
        if (outErrorMessage)
            *outErrorMessage = "Too many redirects";
        return true;
    }

    String targetUrl = resolveRedirectUrl(context->request, location);
    if (targetUrl.length() == 0)
        return false;

    HttpMethod newMethod = context->request->getMethod();
    bool dropBody = false;
    if (status == 301 || status == 302 || status == 303) {
        newMethod = HTTP_METHOD_GET;
        dropBody = true;
    }

    AsyncHttpRequest* newRequest = new AsyncHttpRequest(newMethod, targetUrl);
    newRequest->setTimeout(context->request->getTimeout());
    newRequest->setNoStoreBody(context->request->getNoStoreBody());

    bool sameOrigin = isSameOrigin(context->request, newRequest);
    RedirectHeaderPolicy headerPolicy;
    std::vector<String> redirectSafeHeaders;
    lock();
    headerPolicy = _redirectHeaderPolicy;
    redirectSafeHeaders = _redirectSafeHeaders;
    unlock();

    auto isCrossOriginSensitiveHeader = [](const String& name) {
        String lower = name;
        lower.toLowerCase();
        return lower.equals("authorization") || lower.equals("proxy-authorization") || lower.equals("cookie") ||
               lower.equals("cookie2") || lower.startsWith("x-api-key") || lower.startsWith("x-auth-token") ||
               lower.startsWith("x-access-token");
    };
    auto isDefaultCrossOriginSafeHeader = [dropBody](const String& name) {
        if (equalsIgnoreCase(name, "User-Agent"))
            return true;
        if (equalsIgnoreCase(name, "Accept"))
            return true;
        if (equalsIgnoreCase(name, "Accept-Encoding"))
            return true;
        if (equalsIgnoreCase(name, "Accept-Language"))
            return true;
        if (!dropBody && equalsIgnoreCase(name, "Content-Type"))
            return true;
        return false;
    };
    auto isAllowlistedForCrossOrigin = [&redirectSafeHeaders](const String& name) {
        String lower = name;
        lower.toLowerCase();
        for (const auto& allowed : redirectSafeHeaders) {
            if (allowed.equalsIgnoreCase(lower))
                return true;
        }
        return false;
    };
    const auto& headers = context->request->getHeaders();
    for (const auto& hdr : headers) {
        if (hdr.name.equalsIgnoreCase("Content-Length"))
            continue;
        // Always rebuild cookies for the redirected request from the cookie jar (avoids duplicates and leaks).
        if (hdr.name.equalsIgnoreCase("Cookie") || hdr.name.equalsIgnoreCase("Cookie2"))
            continue;
        // Prevent callers from pinning an old Host header across redirects.
        if (hdr.name.equalsIgnoreCase("Host"))
            continue;
        if (dropBody && hdr.name.equalsIgnoreCase("Content-Type"))
            continue;
        if (!sameOrigin) {
            if (headerPolicy == RedirectHeaderPolicy::kLegacyDropSensitiveOnly) {
                if (isCrossOriginSensitiveHeader(hdr.name))
                    continue;
            } else if (headerPolicy == RedirectHeaderPolicy::kDropAllCrossOrigin) {
                if (!isDefaultCrossOriginSafeHeader(hdr.name) && !isAllowlistedForCrossOrigin(hdr.name))
                    continue;
            }
        }
        newRequest->setHeader(hdr.name, hdr.value);
    }

    if (!dropBody) {
        if (!context->request->getBody().isEmpty()) {
            newRequest->setBody(context->request->getBody());
        } else if (context->request->hasBodyStream()) {
            delete newRequest;
            return false; // cannot replay streamed bodies automatically
        }
    }

    if (outRequest)
        *outRequest = newRequest;
    else
        delete newRequest;

    return true;
}

void AsyncHttpClient::resetContextForRedirect(RequestContext* context, AsyncHttpRequest* newRequest) {
    if (!context)
        return;
    if (context->transport) {
        context->transport->close();
        delete context->transport;
        context->transport = nullptr;
    }
    if (context->request) {
        delete context->request;
        context->request = nullptr;
    }
    if (context->response) {
        delete context->response;
        context->response = nullptr;
    }
    context->request = newRequest;
    context->response = new AsyncHttpResponse();
    context->responseBuffer = "";
    context->headersComplete = false;
    context->responseProcessed = false;
    context->expectedContentLength = 0;
    context->receivedContentLength = 0;
    context->receivedBodyLength = 0;
    context->chunked = false;
    context->chunkedComplete = false;
    context->currentChunkRemaining = 0;
    context->awaitingFinalChunkTerminator = false;
    context->trailerLineCount = 0;
    context->headersSent = false;
    context->streamingBodyInProgress = false;
    context->notifiedEndCallback = false;
    context->requestKeepAlive = false;
    context->serverRequestedClose = false;
    context->usingPooledConnection = false;
    context->resolvedTlsConfig = AsyncHttpTLSConfig();
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
    context->gzipEncoded = false;
    context->gzipDecodeActive = false;
    context->gzipDecoder.reset();
#endif
#if !ASYNC_TCP_HAS_TIMEOUT
    context->timeoutTimer = millis();
#endif
    executeRequest(context);
}

bool AsyncHttpClient::handleRedirect(RequestContext* context) {
    AsyncHttpRequest* newRequest = nullptr;
    HttpClientError redirectError = CONNECTION_FAILED;
    String errorMessage;
    bool decision = buildRedirectRequest(context, &newRequest, &redirectError, &errorMessage);
    if (!decision)
        return false;
    if (!newRequest) {
        triggerError(context, redirectError, errorMessage.c_str());
        return true;
    }

    context->redirectCount++;
    resetContextForRedirect(context, newRequest);
    return true;
}

void AsyncHttpClient::loop() {
    uint32_t now = millis();
    pruneIdleConnections();
    // Iterate safely even if callbacks remove entries: use index loop.
    lock();
    for (size_t i = 0; i < _activeRequests.size();) {
        RequestContext* ctx = _activeRequests[i];
#if !ASYNC_TCP_HAS_TIMEOUT
        if (!ctx->responseProcessed && (now - ctx->timeoutTimer) >= ctx->request->getTimeout()) {
            unlock();
            triggerError(ctx, REQUEST_TIMEOUT, "Request timeout");
            lock();
        }
#endif
        if (!ctx->responseProcessed && ctx->transport && !ctx->headersSent && ctx->connectTimeoutMs > 0 &&
            (now - ctx->connectStartMs) > ctx->connectTimeoutMs) {
            unlock();
            triggerError(ctx, CONNECT_TIMEOUT, "Connect timeout");
            lock();
        }
        if (!ctx->responseProcessed && ctx->transport && ctx->transport->isHandshaking()) {
            uint32_t hsTimeout = ctx->transport->getHandshakeTimeoutMs();
            uint32_t hsStart = ctx->transport->getHandshakeStartMs();
            if (hsTimeout > 0 && hsStart > 0 && (now - hsStart) > hsTimeout) {
                unlock();
                triggerError(ctx, TLS_HANDSHAKE_TIMEOUT, "TLS handshake timeout");
                lock();
            }
        }
        if (!ctx->responseProcessed && ctx->streamingBodyInProgress && ctx->request->hasBodyStream()) {
            unlock();
            sendStreamData(ctx);
            lock();
        }
        // If triggerError/processResponse cleaned up ctx, current index now holds a different pointer.
        if (i < _activeRequests.size() && _activeRequests[i] == ctx) {
            ++i; // still present, advance
        }
        // else do not advance: current i now refers to next element after erase
    }
    unlock();
}

void AsyncHttpClient::tryDequeue() {
    while (true) {
        lock();
        bool canStart = (_maxParallel == 0 || _activeRequests.size() < _maxParallel);
        if (!canStart || _pendingQueue.empty()) {
            unlock();
            break;
        }
        RequestContext* ctx = _pendingQueue.front();
        _pendingQueue.erase(_pendingQueue.begin());
        unlock();
        executeRequest(ctx);
    }
}

void AsyncHttpClient::sendStreamData(RequestContext* context) {
    if (!context->transport || !context->request->hasBodyStream())
        return;
    if (!context->transport->canSend())
        return;
    auto provider = context->request->getBodyProvider();
    if (!provider)
        return;
    uint8_t temp[512];
    bool final = false;
    int written = provider(temp, sizeof(temp), &final);
    if (written < 0) {
        triggerError(context, BODY_STREAM_READ_FAILED, "Body stream read failed");
        return;
    }
    if (written > (int)sizeof(temp)) {
        triggerError(context, BODY_STREAM_READ_FAILED, "Body stream provider overrun");
        return;
    }
    if (written > 0)
        context->transport->write((const char*)temp, written);
    if (final)
        context->streamingBodyInProgress = false;
}

bool AsyncHttpClient::shouldEnforceBodyLimit(RequestContext* context) {
    if (_maxBodySize == 0)
        return false;
    if (!context || !context->request)
        return true;
    if (context->request->getNoStoreBody())
        return false;
    return true;
}

AsyncHttpTLSConfig AsyncHttpClient::resolveTlsConfig(const AsyncHttpRequest* request) const {
    AsyncHttpTLSConfig cfg;
    lock();
    cfg = _defaultTlsConfig;
    unlock();

    auto sanitize = [](AsyncHttpTLSConfig* c) {
        if (!c)
            return;
        if (c->handshakeTimeoutMs == 0)
            c->handshakeTimeoutMs = 12000;
#if !ASYNC_HTTP_ALLOW_INSECURE_TLS
        // Allow skipping CA validation only when pinning is configured.
        if (c->insecure && c->fingerprint.length() == 0)
            c->insecure = false;
#endif
    };

    if (!request || !request->hasTlsConfig()) {
        sanitize(&cfg);
        return cfg;
    }
    const AsyncHttpTLSConfig* overrideCfg = request->getTlsConfig();
    if (!overrideCfg) {
        sanitize(&cfg);
        return cfg;
    }
    if (overrideCfg->caCert.length() > 0)
        cfg.caCert = overrideCfg->caCert;
    if (overrideCfg->clientCert.length() > 0)
        cfg.clientCert = overrideCfg->clientCert;
    if (overrideCfg->clientPrivateKey.length() > 0)
        cfg.clientPrivateKey = overrideCfg->clientPrivateKey;
    if (overrideCfg->fingerprint.length() > 0)
        cfg.fingerprint = overrideCfg->fingerprint;
    cfg.insecure = overrideCfg->insecure;
    if (overrideCfg->handshakeTimeoutMs > 0)
        cfg.handshakeTimeoutMs = overrideCfg->handshakeTimeoutMs;
    sanitize(&cfg);
    return cfg;
}

bool AsyncHttpClient::tlsConfigEquals(const AsyncHttpTLSConfig& a, const AsyncHttpTLSConfig& b) const {
    return a.caCert == b.caCert && a.clientCert == b.clientCert && a.clientPrivateKey == b.clientPrivateKey &&
           a.fingerprint == b.fingerprint && a.insecure == b.insecure && a.handshakeTimeoutMs == b.handshakeTimeoutMs;
}

AsyncTransport* AsyncHttpClient::checkoutPooledTransport(const AsyncHttpRequest* request,
                                                         const AsyncHttpTLSConfig& tlsCfg) {
    if (!request)
        return nullptr;
    AsyncTransport* found = nullptr;
    lock();
    for (auto it = _idleConnections.begin(); it != _idleConnections.end();) {
        bool removeEntry = false;
        if (!it->transport || !it->transport->canSend()) {
            removeEntry = true;
        } else {
            bool match = it->host.equalsIgnoreCase(request->getHost()) && it->port == request->getPort() &&
                         it->secure == request->isSecure();
            if (match && it->secure)
                match = tlsConfigEquals(it->tlsConfig, tlsCfg);
            if (match) {
                found = it->transport;
                _idleConnections.erase(it);
                break;
            }
        }
        if (removeEntry) {
            AsyncTransport* toDelete = it->transport;
            _idleConnections.erase(it);
            if (toDelete) {
                toDelete->close(true);
                delete toDelete;
            }
            continue;
        }
        ++it;
    }
    unlock();
    if (found) {
        found->setDataHandler(nullptr, nullptr);
        found->setDisconnectHandler(nullptr, nullptr);
        found->setErrorHandler(nullptr, nullptr);
        found->setTimeoutHandler(nullptr, nullptr);
    }
    return found;
}

void AsyncHttpClient::dropPooledTransport(AsyncTransport* transport, bool closeTransport) {
    if (!transport)
        return;
    bool removed = false;
    lock();
    for (auto it = _idleConnections.begin(); it != _idleConnections.end(); ++it) {
        if (it->transport == transport) {
            _idleConnections.erase(it);
            removed = true;
            break;
        }
    }
    unlock();
    if (removed && closeTransport) {
        transport->close(true);
        delete transport;
    }
}

void AsyncHttpClient::pruneIdleConnections() {
    if (!_keepAliveEnabled)
        return;
    uint32_t now = millis();
    std::vector<AsyncTransport*> staleTransports;
    lock();
    for (auto it = _idleConnections.begin(); it != _idleConnections.end();) {
        bool stale = !_keepAliveEnabled || !it->transport || !it->transport->canSend() ||
                     (now - it->lastUsedMs) > _keepAliveIdleMs;
        if (stale) {
            AsyncTransport* toDelete = it->transport;
            it = _idleConnections.erase(it);
            if (toDelete)
                staleTransports.push_back(toDelete);
        } else {
            ++it;
        }
    }
    unlock();
    for (auto* t : staleTransports) {
        t->close(true);
        delete t;
    }
}

bool AsyncHttpClient::shouldRecycleTransport(RequestContext* context) const {
    if (!_keepAliveEnabled || !context || !context->transport || !context->request || !context->responseProcessed)
        return false;
    if (!context->response || context->response->getStatusCode() == 0)
        return false;
    if (!context->requestKeepAlive || context->serverRequestedClose)
        return false;
    if (context->chunked && !context->chunkedComplete)
        return false;
    if (!context->chunked && context->expectedContentLength > 0 &&
        context->receivedContentLength < context->expectedContentLength)
        return false;
    if (!context->transport->canSend())
        return false;
    return true;
}

void AsyncHttpClient::releaseConnectionToPool(RequestContext* context) {
    if (!context || !context->request || !context->transport)
        return;
    PooledConnection pooled;
    pooled.transport = context->transport;
    pooled.host = context->request->getHost();
    pooled.port = context->request->getPort();
    pooled.secure = context->request->isSecure();
    pooled.tlsConfig = context->resolvedTlsConfig;
    pooled.lastUsedMs = millis();

    pooled.transport->setConnectHandler(nullptr, nullptr);
    pooled.transport->setTimeoutHandler(nullptr, nullptr);
    pooled.transport->setDataHandler(
        [](void* arg, AsyncTransport* t, void* data, size_t len) {
            (void)data;
            (void)len;
            auto self = static_cast<AsyncHttpClient*>(arg);
            self->dropPooledTransport(t, true);
        },
        this);
    pooled.transport->setDisconnectHandler(
        [](void* arg, AsyncTransport* t) {
            auto self = static_cast<AsyncHttpClient*>(arg);
            self->dropPooledTransport(t, true);
        },
        this);
    pooled.transport->setErrorHandler(
        [](void* arg, AsyncTransport* t, HttpClientError, const char*) {
            auto self = static_cast<AsyncHttpClient*>(arg);
            self->dropPooledTransport(t, true);
        },
        this);

    lock();
    _idleConnections.push_back(pooled);
    unlock();
}

AsyncTransport* AsyncHttpClient::buildTransport(RequestContext* context) {
    if (!context || !context->request)
        return nullptr;
    if (context->request->isSecure()) {
        AsyncHttpTLSConfig cfg = context->resolvedTlsConfig;
        if (cfg.handshakeTimeoutMs == 0)
            cfg.handshakeTimeoutMs = _defaultTlsConfig.handshakeTimeoutMs;
        return createTlsTransport(cfg);
    }
    return createTcpTransport();
}

bool AsyncHttpClient::isIpLiteral(const String& host) const {
    if (host.length() == 0)
        return false;
    bool hasColon = false;
    bool hasDot = false;
    for (size_t i = 0; i < host.length(); ++i) {
        char c = host.charAt(i);
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            continue;
        }
        if (c == '.') {
            hasDot = true;
            continue;
        }
        if (c == ':') {
            hasColon = true;
            continue;
        }
        return false;
    }
    return hasColon || hasDot;
}

bool AsyncHttpClient::normalizeCookieDomain(String& domain, const String& host, bool domainAttributeProvided,
                                            bool* outHostOnly) const {
    if (outHostOnly)
        *outHostOnly = true;
    String hostLower = host;
    hostLower.toLowerCase();
    String cleaned = normalizeDomainForStorage(domain);

    // No Domain= attribute (or empty) => host-only cookie.
    if (!domainAttributeProvided || cleaned.length() == 0) {
        domain = hostLower;
        if (outHostOnly)
            *outHostOnly = true;
        return true;
    }

    // Reject Domain= on IP literals and unrelated domains.
    if (isIpLiteral(hostLower))
        return false;
    if (!domainMatches(cleaned, hostLower))
        return false;

    // Public suffix and "TLD-like" Domain= attributes are ignored (stored as host-only instead).
    // This avoids broad cookie scope even when the embedded public-suffix list is incomplete.
    if (hostLower.indexOf('.') == -1 || cleaned.indexOf('.') == -1) {
        domain = hostLower;
        if (outHostOnly)
            *outHostOnly = true;
        return true;
    }

    bool allowDomainCookie = false;
    lock();
    if (_allowCookieDomainAttribute) {
        for (const auto& allowedDomain : _allowedCookieDomains) {
            if (allowedDomain.equalsIgnoreCase(cleaned)) {
                allowDomainCookie = true;
                break;
            }
        }
    }
    unlock();

    if (allowDomainCookie) {
        domain = cleaned;
        if (outHostOnly)
            *outHostOnly = false;
    } else {
        domain = hostLower;
        if (outHostOnly)
            *outHostOnly = true;
    }
    return true;
}

bool AsyncHttpClient::domainMatches(const String& cookieDomain, const String& host) const {
    if (cookieDomain.length() == 0)
        return true;
    if (host.equalsIgnoreCase(cookieDomain))
        return true;
    if (host.length() <= cookieDomain.length())
        return false;
    size_t offset = host.length() - cookieDomain.length();
    if (host.charAt(offset - 1) != '.')
        return false;
    return host.substring(offset).equalsIgnoreCase(cookieDomain);
}

bool AsyncHttpClient::pathMatches(const String& cookiePath, const String& requestPath) const {
    String req = requestPath;
    int q = req.indexOf('?');
    if (q != -1)
        req = req.substring(0, q);
    if (!req.startsWith("/"))
        req = "/" + req;
    String cpath = cookiePath.length() > 0 ? cookiePath : "/";
    if (!cpath.startsWith("/"))
        cpath = "/" + cpath;
    if (req.equals(cpath))
        return true;
    if (!req.startsWith(cpath))
        return false;
    if (cpath.endsWith("/"))
        return true;
    return req.length() > cpath.length() && req.charAt(cpath.length()) == '/';
}

bool AsyncHttpClient::cookieMatchesRequest(const StoredCookie& cookie, const AsyncHttpRequest* request,
                                           int64_t nowSeconds) const {
    if (!request)
        return false;
    if (isCookieExpired(cookie, nowSeconds))
        return false;
    if (cookie.secure && !request->isSecure())
        return false;
    if (cookie.hostOnly) {
        if (!request->getHost().equalsIgnoreCase(cookie.domain))
            return false;
    } else {
        if (!domainMatches(cookie.domain, request->getHost()))
            return false;
    }
    if (!pathMatches(cookie.path, request->getPath()))
        return false;
    return !cookie.value.isEmpty();
}

bool AsyncHttpClient::isCookieExpired(const StoredCookie& cookie, int64_t nowSeconds) const {
    return cookie.expiresAt != -1 && nowSeconds >= cookie.expiresAt;
}

void AsyncHttpClient::purgeExpiredCookies(int64_t nowSeconds) {
    for (auto it = _cookies.begin(); it != _cookies.end();) {
        if (isCookieExpired(*it, nowSeconds)) {
            it = _cookies.erase(it);
        } else {
            ++it;
        }
    }
}

static uint8_t countDomainDots(const String& domain) {
    uint8_t dots = 0;
    for (size_t i = 0; i < domain.length(); ++i) {
        if (domain.charAt(i) == '.')
            ++dots;
    }
    return dots;
}

void AsyncHttpClient::evictOneCookieLocked() {
    if (_cookies.empty())
        return;
    size_t bestIndex = 0;
    for (size_t i = 1; i < _cookies.size(); ++i) {
        const StoredCookie& best = _cookies[bestIndex];
        const StoredCookie& candidate = _cookies[i];

        if (candidate.lastAccessAt != best.lastAccessAt) {
            if (candidate.lastAccessAt < best.lastAccessAt)
                bestIndex = i;
            continue;
        }

        bool candidateSession = candidate.expiresAt == -1;
        bool bestSession = best.expiresAt == -1;
        if (candidateSession != bestSession) {
            if (candidateSession)
                bestIndex = i;
            continue;
        }

        uint8_t candidateDots = countDomainDots(candidate.domain);
        uint8_t bestDots = countDomainDots(best.domain);
        if (candidateDots != bestDots) {
            if (candidateDots < bestDots)
                bestIndex = i;
            continue;
        }

        if (candidate.domain.length() != best.domain.length()) {
            if (candidate.domain.length() < best.domain.length())
                bestIndex = i;
            continue;
        }

        if (candidate.path.length() != best.path.length()) {
            if (candidate.path.length() < best.path.length())
                bestIndex = i;
            continue;
        }

        if (candidate.createdAt != best.createdAt) {
            if (candidate.createdAt < best.createdAt)
                bestIndex = i;
            continue;
        }
    }
    _cookies.erase(_cookies.begin() + static_cast<std::vector<StoredCookie>::difference_type>(bestIndex));
}

void AsyncHttpClient::applyCookies(AsyncHttpRequest* request) {
    if (!request)
        return;
    int64_t now = currentTimeSeconds();
    String cookieHeader;
    lock();
    purgeExpiredCookies(now);
    size_t estimatedLen = 0;
    for (const auto& cookie : _cookies) {
        if (cookieMatchesRequest(cookie, request, now))
            estimatedLen += cookie.name.length() + 1 + cookie.value.length() + 2;
    }
    if (estimatedLen >= 2)
        estimatedLen -= 2;
    cookieHeader.reserve(estimatedLen);
    for (auto& cookie : _cookies) {
        if (cookieMatchesRequest(cookie, request, now)) {
            cookie.lastAccessAt = now;
            if (!cookieHeader.isEmpty())
                cookieHeader += "; ";
            cookieHeader += cookie.name;
            cookieHeader += "=";
            cookieHeader += cookie.value;
        }
    }
    unlock();
    if (cookieHeader.isEmpty())
        return;
    String existing = request->getHeader("Cookie");
    if (!existing.isEmpty()) {
        if (!existing.endsWith(";"))
            existing += ";";
        existing += " ";
        existing += cookieHeader;
        request->setHeader("Cookie", existing);
    } else {
        request->setHeader("Cookie", cookieHeader);
    }
}

void AsyncHttpClient::storeResponseCookie(const AsyncHttpRequest* request, const String& setCookieValue) {
    if (!request)
        return;
    String raw = setCookieValue;
    if (raw.length() == 0)
        return;
    if (raw.length() > kMaxCookieBytes)
        return;
    int64_t now = currentTimeSeconds();
    int semi = raw.indexOf(';');
    String pair = semi == -1 ? raw : raw.substring(0, semi);
    pair.trim();
    int eq = pair.indexOf('=');
    if (eq <= 0)
        return;
    StoredCookie cookie;
    cookie.name = pair.substring(0, eq);
    cookie.value = pair.substring(eq + 1);
    cookie.name.trim();
    cookie.value.trim();
    cookie.domain = request->getHost();
    cookie.path = "/";
    bool domainAttributeProvided = false;
    bool remove = cookie.value.isEmpty();
    bool maxAgeAttributeProvided = false;
    int64_t expiresAt = -1;

    int pos = semi;
    while (pos != -1) {
        int next = raw.indexOf(';', pos + 1);
        String token = raw.substring(pos + 1, next == -1 ? raw.length() : next);
        token.trim();
        if (!token.isEmpty()) {
            int eqPos = token.indexOf('=');
            String key = eqPos == -1 ? token : token.substring(0, eqPos);
            String val = eqPos == -1 ? "" : token.substring(eqPos + 1);
            key.trim();
            val.trim();
            if (key.equalsIgnoreCase("Path")) {
                cookie.path = val.length() > 0 ? val : "/";
            } else if (key.equalsIgnoreCase("Domain")) {
                cookie.domain = val;
                domainAttributeProvided = true;
            } else if (key.equalsIgnoreCase("Secure")) {
                cookie.secure = true;
            } else if (key.equalsIgnoreCase("Max-Age")) {
                maxAgeAttributeProvided = true;
                long age = val.toInt();
                if (age <= 0) {
                    remove = true;
                    expiresAt = now;
                } else {
                    expiresAt = now + static_cast<int64_t>(age);
                }
            } else if (key.equalsIgnoreCase("Expires") && !maxAgeAttributeProvided) {
                int64_t parsedExpiry = -1;
                if (parseHttpDate(val, &parsedExpiry))
                    expiresAt = parsedExpiry;
            }
        }
        pos = next;
    }

    if (!normalizeCookieDomain(cookie.domain, request->getHost(), domainAttributeProvided, &cookie.hostOnly))
        return;
    if (!cookie.path.startsWith("/"))
        cookie.path = "/" + cookie.path;
    size_t payloadSize = cookie.name.length() + cookie.value.length() + cookie.domain.length() + cookie.path.length();
    if (payloadSize > kMaxCookieBytes)
        return;
    cookie.expiresAt = expiresAt;
    cookie.createdAt = now;
    cookie.lastAccessAt = now;
    if (isCookieExpired(cookie, now))
        remove = true;

    lock();
    purgeExpiredCookies(now);
    for (auto it = _cookies.begin(); it != _cookies.end();) {
        if (it->name.equalsIgnoreCase(cookie.name) && it->domain.equalsIgnoreCase(cookie.domain) &&
            it->path.equals(cookie.path)) {
            it = _cookies.erase(it);
        } else {
            ++it;
        }
    }
    if (!remove) {
        if (_cookies.size() >= kMaxCookieCount)
            evictOneCookieLocked();
        _cookies.push_back(cookie);
    }
    unlock();
}
