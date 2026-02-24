

#include "AsyncHttpClient.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <memory>
#include <utility>
#include "ConnectionPool.h"
#include "CookieJar.h"
#include "HttpHelpers.h"
#include "RedirectHandler.h"

static constexpr size_t kMaxChunkSizeLineLen = 64;
static constexpr size_t kMaxChunkTrailerLineLen = 256;
static constexpr size_t kMaxChunkTrailerLines = 32;
static constexpr size_t kDefaultMaxHeaderBytes = 2800; // ~2.8 KiB
static constexpr size_t kDefaultMaxBodyBytes = 8192;   // 8 KiB

AsyncHttpClient::AsyncHttpClient()
    : _defaultTimeout(10000), _defaultUserAgent(String("ESPAsyncWebClient/") + ESP_ASYNC_WEB_CLIENT_VERSION),
      _bodyChunkCallback(nullptr), _maxBodySize(kDefaultMaxBodyBytes), _followRedirects(false), _maxRedirectHops(3),
      _maxHeaderBytes(kDefaultMaxHeaderBytes) {
    _cookieJar.reset(new CookieJar(this));
    _connectionPool.reset(new ConnectionPool(this));
    _redirectHandler.reset(new RedirectHandler(this));
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
    if (_connectionPool) {
        _connectionPool->dropAll();
        _connectionPool.reset();
    }
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
    nameStr.toLowerCase();
    lock();
    for (auto& h : _defaultHeaders) {
        if (h.name == nameStr) {
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
    nameStr.toLowerCase();
    lock();
    for (auto it = _defaultHeaders.begin(); it != _defaultHeaders.end();) {
        if (it->name == nameStr) {
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
    if (_redirectHandler)
        _redirectHandler->setRedirectHeaderPolicy(policy);
}

void AsyncHttpClient::addRedirectSafeHeader(const char* name) {
    if (_redirectHandler)
        _redirectHandler->addRedirectSafeHeader(name);
}

void AsyncHttpClient::clearRedirectSafeHeaders() {
    if (_redirectHandler)
        _redirectHandler->clearRedirectSafeHeaders();
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
    lock();
    _keepAliveEnabled = enable;
    _keepAliveIdleMs = idleMs == 0 ? 1000 : idleMs;
    unlock();
    if (!enable && _connectionPool) {
        _connectionPool->dropAll();
    }
}

void AsyncHttpClient::clearCookies() {
    if (_cookieJar)
        _cookieJar->clearCookies();
}

void AsyncHttpClient::setAllowCookieDomainAttribute(bool enable) {
    if (_cookieJar)
        _cookieJar->setAllowCookieDomainAttribute(enable);
}

void AsyncHttpClient::addAllowedCookieDomain(const char* domain) {
    if (_cookieJar)
        _cookieJar->addAllowedCookieDomain(domain);
}

void AsyncHttpClient::clearAllowedCookieDomains() {
    if (_cookieJar)
        _cookieJar->clearAllowedCookieDomains();
}

void AsyncHttpClient::setCookie(const char* name, const char* value, const char* path, const char* domain,
                                bool secure) {
    if (_cookieJar)
        _cookieJar->setCookie(name, value, path, domain, secure);
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

    std::unique_ptr<AsyncHttpRequest> request(new AsyncHttpRequest(method, String(url)));
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
    return this->request(std::move(request), onSuccess, onError);
}

uint32_t AsyncHttpClient::request(std::unique_ptr<AsyncHttpRequest> request, SuccessCallback onSuccess,
                                  ErrorCallback onError) {
    if (!request) {
        if (onError)
            onError(CONNECTION_FAILED, "Request is null");
        return 0;
    }
    if (request->getHost().length() == 0 || request->getPath().length() == 0) {
        if (onError)
            onError(CONNECTION_FAILED, "Invalid URL");
        return 0;
    }
    std::unique_ptr<RequestContext> ctx(new RequestContext());
    ctx->request = std::move(request);
    ctx->response = std::make_shared<AsyncHttpResponse>();
    ctx->onSuccess = onSuccess;
    ctx->onError = onError;
    ctx->id = _nextRequestId++;
    ctx->timing.connectTimeoutMs = _defaultConnectTimeout;
    if (_keepAliveEnabled && ctx->request) {
        String conn = ctx->request->getHeader("Connection");
        if (conn.isEmpty())
            ctx->request->setHeader("Connection", "keep-alive");
        if (ctx->request->getHeader("Keep-Alive").isEmpty()) {
            uint16_t timeoutSec = static_cast<uint16_t>(std::max<uint32_t>(1, _keepAliveIdleMs / 1000));
            ctx->request->setHeader("Keep-Alive", String("timeout=") + String(timeoutSec));
        }
    }
    uint32_t id = ctx->id;
    executeOrQueue(std::move(ctx));
    return id;
}

// Removed per-request chunk overload

bool AsyncHttpClient::abort(uint32_t requestId) {
    // Active requests: we must be careful as triggerError() will cleanup and erase from _activeRequests
    lock();
    for (size_t i = 0; i < _activeRequests.size(); ++i) {
        RequestContext* ctx = _activeRequests[i].get();
        if (ctx && ctx->id == requestId && !ctx->responseProcessed) {
            unlock();
            triggerError(ctx, ABORTED, "Aborted by user");
            return true;
        }
    }
    // Pending queue: still not executed
    std::unique_ptr<RequestContext> pending;
    for (auto it = _pendingQueue.begin(); it != _pendingQueue.end(); ++it) {
        if ((*it)->id == requestId) {
            pending = std::move(*it);
            _pendingQueue.erase(it);
            break;
        }
    }
    unlock();
    if (pending) {
        triggerError(pending.get(), ABORTED, "Aborted by user");
        return true;
    }
    return false;
}

void AsyncHttpClient::executeOrQueue(std::unique_ptr<RequestContext> context) {
    if (!context)
        return;
    lock();
    if (_maxParallel > 0 && _activeRequests.size() >= _maxParallel) {
        _pendingQueue.push_back(std::move(context));
        unlock();
        return;
    }
    _activeRequests.push_back(std::move(context));
    RequestContext* ctx = _activeRequests.back().get();
    unlock();
    executeRequest(ctx);
}

void AsyncHttpClient::executeRequest(RequestContext* context) {
    if (_cookieJar)
        _cookieJar->applyCookies(context->request.get());
    context->timing.connectStartMs = millis();
    context->timing.connectTimeoutMs = _defaultConnectTimeout;
    context->resolvedTlsConfig = resolveTlsConfig(context->request.get());
    String connHeader = context->request->getHeader("Connection");
    context->requestKeepAlive = _keepAliveEnabled && !equalsIgnoreCase(connHeader, "close");
    AsyncTransport* pooled = nullptr;
    if (context->requestKeepAlive && _connectionPool)
        pooled = _connectionPool->checkoutPooledTransport(context->request.get(), context->resolvedTlsConfig,
                                                          _keepAliveEnabled);
    context->transport = pooled ? pooled : buildTransport(context);
    context->usingPooledConnection = pooled != nullptr;
    if (!context->transport) {
        triggerError(context, HTTPS_NOT_SUPPORTED, "HTTPS transport unavailable");
        return;
    }
    if (context->usingPooledConnection)
        context->timing.connectTimeoutMs = 0;

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
    context->timing.timeoutTimer = millis();
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

bool AsyncHttpClient::wouldExceedBodyLimit(RequestContext* context, size_t incoming, bool enforceLimit) const {
    if (!enforceLimit)
        return false;
    if (!context)
        return true;
    size_t current = context->receivedBodyLength;
    if (current >= _maxBodySize)
        return true;
    return incoming > (_maxBodySize - current);
}

bool AsyncHttpClient::emitBodyBytes(RequestContext* context, const char* out, size_t outLen, bool storeBody,
                                    bool enforceLimit) {
    if (!context)
        return false;
    if (!out || outLen == 0)
        return true;
    if (wouldExceedBodyLimit(context, outLen, enforceLimit)) {
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
}

bool AsyncHttpClient::deliverWireBytes(RequestContext* context, const char* wire, size_t wireLen, bool storeBody,
                                       bool enforceLimit) {
    if (!context)
        return false;
    if (wireLen == 0)
        return true;
    context->receivedContentLength += wireLen;
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
    if (context->gzip.gzipDecodeActive) {
        size_t offset = 0;
        while (offset < wireLen) {
            const uint8_t* outPtr = nullptr;
            size_t outLen = 0;
            size_t consumed = 0;
            GzipDecoder::Result r = context->gzip.gzipDecoder.write(
                reinterpret_cast<const uint8_t*>(wire + offset), wireLen - offset, &consumed, &outPtr, &outLen, true);
            if (outLen > 0) {
                if (!emitBodyBytes(context, reinterpret_cast<const char*>(outPtr), outLen, storeBody, enforceLimit))
                    return false;
            }
            if (r == GzipDecoder::Result::kError) {
                triggerError(context, GZIP_DECODE_FAILED, context->gzip.gzipDecoder.lastError());
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
    return emitBodyBytes(context, wire, wireLen, storeBody, enforceLimit);
}

bool AsyncHttpClient::finalizeDecoding(RequestContext* context, bool storeBody, bool enforceLimit) {
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
    if (!context || !context->gzip.gzipDecodeActive)
        return true;
    for (;;) {
        const uint8_t* outPtr = nullptr;
        size_t outLen = 0;
        GzipDecoder::Result r = context->gzip.gzipDecoder.finish(&outPtr, &outLen);
        if (outLen > 0) {
            if (!emitBodyBytes(context, reinterpret_cast<const char*>(outPtr), outLen, storeBody, enforceLimit))
                return false;
        }
        if (r == GzipDecoder::Result::kDone)
            return true;
        if (r == GzipDecoder::Result::kOk)
            continue;
        triggerError(context, GZIP_DECODE_FAILED, context->gzip.gzipDecoder.lastError());
        return false;
    }
#else
    (void)context;
    (void)storeBody;
    (void)enforceLimit;
    return true;
#endif
}

void AsyncHttpClient::handleData(RequestContext* context, char* data, size_t len) {
    if (!context)
        return;
    bool storeBody = context->request && !context->request->getNoStoreBody();
    bool bufferThisChunk = (!context->headersComplete || context->chunk.chunked);
    if (bufferThisChunk)
        context->responseBuffer.concat(data, len);
    bool enforceLimit = shouldEnforceBodyLimit(context);

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
                bool gzipActive = context->gzip.gzipDecodeActive;
#else
                bool gzipActive = false;
#endif
                if (enforceLimit && !gzipActive && context->expectedContentLength > 0 &&
                    context->expectedContentLength > _maxBodySize) {
                    triggerError(context, MAX_BODY_SIZE_EXCEEDED, "Body exceeds configured maximum");
                    return;
                }
                if (storeBody && !gzipActive && context->expectedContentLength > 0 && !context->chunk.chunked &&
                    (!enforceLimit || context->expectedContentLength <= _maxBodySize)) {
                    context->response->reserveBody(context->expectedContentLength);
                }
                context->responseBuffer.remove(0, headerEnd + 4);
                if (_redirectHandler && _redirectHandler->handleRedirect(context))
                    return;
                if (!context->chunk.chunked && context->responseBuffer.length() > 0) {
                    size_t incomingLen = context->responseBuffer.length();
                    if (!gzipActive && wouldExceedBodyLimit(context, incomingLen, enforceLimit)) {
                        triggerError(context, MAX_BODY_SIZE_EXCEEDED, "Body exceeds configured maximum");
                        return;
                    }
                    if (!deliverWireBytes(context, context->responseBuffer.c_str(), incomingLen, storeBody,
                                          enforceLimit))
                        return;
                    context->responseBuffer = "";
                }
            } else {
                triggerError(context, HEADER_PARSE_FAILED, "Failed to parse response headers");
                return;
            }
        }
    } else if (!context->chunk.chunked) {
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
        bool gzipActive = context->gzip.gzipDecodeActive;
#else
        bool gzipActive = false;
#endif
        if (!gzipActive && wouldExceedBodyLimit(context, len, enforceLimit)) {
            triggerError(context, MAX_BODY_SIZE_EXCEEDED, "Body exceeds configured maximum");
            return;
        }
        if (!deliverWireBytes(context, data, len, storeBody, enforceLimit))
            return;
    }

    while (context->headersComplete && context->chunk.chunked && !context->chunk.chunkedComplete) {
        if (context->chunk.awaitingFinalChunkTerminator) {
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
                context->chunk.awaitingFinalChunkTerminator = false;
                context->chunk.chunkedComplete = true;
                continue;
            }
            if (lineEndT > (int)kMaxChunkTrailerLineLen) {
                triggerError(context, CHUNKED_DECODE_FAILED, "Chunk trailer line too long");
                return;
            }
            if (context->chunk.trailerLineCount >= kMaxChunkTrailerLines) {
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
            context->chunk.trailerLineCount++;
            context->responseBuffer.remove(0, lineEndT + 2);
            continue;
        }

        if (context->chunk.currentChunkRemaining == 0) {
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
            bool gzipActive = context->gzip.gzipDecodeActive;
#else
            bool gzipActive = false;
#endif
            if (!gzipActive && chunkSize > 0 && wouldExceedBodyLimit(context, chunkSize, enforceLimit)) {
                triggerError(context, MAX_BODY_SIZE_EXCEEDED, "Body exceeds configured maximum");
                return;
            }
            context->chunk.currentChunkRemaining = chunkSize;
            context->responseBuffer.remove(0, lineEnd + 2);
            if (chunkSize == 0) {
                context->chunk.awaitingFinalChunkTerminator = true;
                context->chunk.trailerLineCount = 0;
                continue;
            }
        }
        size_t needed = context->chunk.currentChunkRemaining + 2;
        if (context->responseBuffer.length() < needed)
            break;
        if (context->responseBuffer.charAt(context->chunk.currentChunkRemaining) != '\r' ||
            context->responseBuffer.charAt(context->chunk.currentChunkRemaining + 1) != '\n') {
            triggerError(context, CHUNKED_DECODE_FAILED, "Chunk missing terminating CRLF");
            return;
        }
        size_t chunkLen = context->chunk.currentChunkRemaining;
        const char* chunkPtr = context->responseBuffer.c_str();
        if (!deliverWireBytes(context, chunkPtr, chunkLen, storeBody, enforceLimit))
            return;
        context->responseBuffer.remove(0, needed);
        context->chunk.currentChunkRemaining = 0;
    }

    if (context->headersComplete && !context->responseProcessed) {
        bool complete = false;
        if (context->chunk.chunked && context->chunk.chunkedComplete)
            complete = true;
        else if (!context->chunk.chunked && context->expectedContentLength > 0 &&
                 context->receivedContentLength >= context->expectedContentLength)
            complete = true;
        if (complete) {
            if (!finalizeDecoding(context, storeBody, enforceLimit))
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
    if (context->chunk.chunked && !context->chunk.chunkedComplete) {
        // Connection closed before receiving final chunk
        triggerError(context, CHUNKED_DECODE_FAILED, "Failed to decode chunked body");
        return;
    }
    if (!context->chunk.chunked && context->expectedContentLength > 0 &&
        context->receivedContentLength < context->expectedContentLength) {
        // Body truncated
        triggerError(context, CONNECTION_CLOSED_MID_BODY, "Truncated response");
        return;
    }
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
    if (context->gzip.gzipDecodeActive) {
        bool storeBody = context->request && !context->request->getNoStoreBody();
        bool enforceLimit = shouldEnforceBodyLimit(context);
        if (!finalizeDecoding(context, storeBody, enforceLimit))
            return;
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
            String lowerName = name;
            lowerName.toLowerCase();
            context->response->setHeader(lowerName, value);
            if (lowerName == "content-length") {
                long parsed = value.toInt();
                if (parsed < 0)
                    parsed = 0;
                context->expectedContentLength = (size_t)parsed;
                context->response->setContentLength(context->expectedContentLength);
            } else if (lowerName == "transfer-encoding" && value.equalsIgnoreCase("chunked")) {
                context->chunk.chunked = true;
            } else if (lowerName == "content-encoding") {
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
                String lower = value;
                lower.toLowerCase();
                if (lower.indexOf("gzip") != -1) {
                    context->gzip.gzipEncoded = true;
                    context->gzip.gzipDecodeActive = true;
                    context->gzip.gzipDecoder.begin();
                }
#endif
            } else if (lowerName == "connection") {
                String lower = value;
                lower.toLowerCase();
                if (lower.indexOf("close") != -1)
                    context->serverRequestedClose = true;
            } else if (lowerName == "set-cookie") {
                if (_cookieJar)
                    _cookieJar->storeResponseCookie(context->request.get(), value);
            }
        }
        lineStart = lineEnd + 2;
    }
    return true;
}

void AsyncHttpClient::processResponse(RequestContext* context) {
    if (context->responseProcessed)
        return;
    if (_redirectHandler && _redirectHandler->handleRedirect(context))
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
        bool recycle = false;
        if (_connectionPool) {
            recycle = _connectionPool->shouldRecycleTransport(
                context->request.get(), context->response, context->transport, context->responseProcessed,
                context->requestKeepAlive, context->serverRequestedClose, context->chunk.chunked,
                context->chunk.chunkedComplete, context->expectedContentLength, context->receivedContentLength,
                _keepAliveEnabled);
        }
        if (recycle && _connectionPool) {
            _connectionPool->releaseConnectionToPool(context->transport, context->request.get(),
                                                     context->resolvedTlsConfig);
        } else {
            toDelete = context->transport;
        }
        context->transport = nullptr;
    }
    context->request.reset();
    context->response.reset();
    lock();
    auto it = std::find_if(_activeRequests.begin(), _activeRequests.end(),
                           [context](const std::unique_ptr<RequestContext>& ptr) { return ptr.get() == context; });
    if (it != _activeRequests.end())
        _activeRequests.erase(it);
    unlock();
    if (toDelete) {
        toDelete->close();
        delete toDelete;
    }
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

void AsyncHttpClient::loop() {
    uint32_t now = millis();
    if (_connectionPool)
        _connectionPool->pruneIdleConnections(_keepAliveEnabled, _keepAliveIdleMs);
    // Iterate safely even if callbacks remove entries: use index loop.
    lock();
    for (size_t i = 0; i < _activeRequests.size();) {
        RequestContext* ctx = _activeRequests[i].get();
#if !ASYNC_TCP_HAS_TIMEOUT
        if (!ctx->responseProcessed && (now - ctx->timing.timeoutTimer) >= ctx->request->getTimeout()) {
            unlock();
            triggerError(ctx, REQUEST_TIMEOUT, "Request timeout");
            lock();
        }
#endif
        if (!ctx->responseProcessed && ctx->transport && !ctx->headersSent && ctx->timing.connectTimeoutMs > 0 &&
            (now - ctx->timing.connectStartMs) > ctx->timing.connectTimeoutMs) {
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
        if (i < _activeRequests.size() && _activeRequests[i].get() == ctx) {
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
        _activeRequests.push_back(std::move(_pendingQueue.front()));
        _pendingQueue.erase(_pendingQueue.begin());
        RequestContext* ctx = _activeRequests.back().get();
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
