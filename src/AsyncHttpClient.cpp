

#include "AsyncHttpClient.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include "UrlParser.h"

static constexpr size_t kMaxChunkSizeLineLen = 64;
static constexpr size_t kMaxChunkTrailerLineLen = 256;
static constexpr size_t kMaxChunkTrailerLines = 32;
static constexpr size_t kDefaultMaxHeaderBytes = 2800; // ~2.8 KiB
static constexpr size_t kDefaultMaxBodyBytes = 8192;   // 8 KiB
static constexpr size_t kMaxCookieCount = 16;
static constexpr size_t kMaxCookieBytes = 4096;
static const char* kPublicSuffixes[] = {"com",  "net",  "org", "gov", "edu", "mil", "int",
                                        "co.uk", "ac.uk", "gov.uk", "uk",  "io",  "co"};

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
void AsyncHttpClient::lock() {
    if (_reqMutex)
        xSemaphoreTakeRecursive(_reqMutex, portMAX_DELAY);
}
void AsyncHttpClient::unlock() {
    if (_reqMutex)
        xSemaphoreGiveRecursive(_reqMutex);
}
#else
void AsyncHttpClient::lock() {}
void AsyncHttpClient::unlock() {}
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
    return makeRequest(HTTP_GET, url, nullptr, onSuccess, onError);
}
uint32_t AsyncHttpClient::post(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError) {
    return makeRequest(HTTP_POST, url, data, onSuccess, onError);
}
uint32_t AsyncHttpClient::put(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError) {
    return makeRequest(HTTP_PUT, url, data, onSuccess, onError);
}
uint32_t AsyncHttpClient::del(const char* url, SuccessCallback onSuccess, ErrorCallback onError) {
    return makeRequest(HTTP_DELETE, url, nullptr, onSuccess, onError);
}
uint32_t AsyncHttpClient::head(const char* url, SuccessCallback onSuccess, ErrorCallback onError) {
    return makeRequest(HTTP_HEAD, url, nullptr, onSuccess, onError);
}
uint32_t AsyncHttpClient::patch(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError) {
    return makeRequest(HTTP_PATCH, url, data, onSuccess, onError);
}

void AsyncHttpClient::setHeader(const char* name, const char* value) {
    String nameStr(name), valueStr(value);
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
    _defaultUserAgent = String(userAgent);
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

void AsyncHttpClient::setCookie(const char* name, const char* value, const char* path, const char* domain,
                                bool secure) {
    if (!name || strlen(name) == 0)
        return;
    StoredCookie cookie;
    cookie.name = String(name);
    cookie.value = value ? String(value) : String();
    cookie.path = (path && strlen(path) > 0) ? String(path) : String("/");
    cookie.domain = domain ? String(domain) : String();
    cookie.secure = secure;
    if (!cookie.path.startsWith("/"))
        cookie.path = "/" + cookie.path;
    if (cookie.domain.startsWith("."))
        cookie.domain.remove(0, 1);

    lock();
    for (auto it = _cookies.begin(); it != _cookies.end();) {
        if (it->name.equalsIgnoreCase(cookie.name) && it->domain.equalsIgnoreCase(cookie.domain) &&
            it->path.equals(cookie.path)) {
            it = _cookies.erase(it);
        } else {
            ++it;
        }
    }
    if (!cookie.value.isEmpty())
        _cookies.push_back(cookie);
    unlock();
}

uint32_t AsyncHttpClient::makeRequest(HttpMethod method, const char* url, const char* data, SuccessCallback onSuccess,
                                      ErrorCallback onError) {
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
    context->responseBuffer.concat(data, len);
    bool enforceLimit = shouldEnforceBodyLimit(context);
    auto wouldExceedLimit = [&](size_t incoming) -> bool {
        if (!enforceLimit)
            return false;
        size_t current = context->receivedContentLength;
        if (current >= _maxBodySize)
            return true;
        return incoming > (_maxBodySize - current);
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
                if (enforceLimit && context->expectedContentLength > 0 &&
                    context->expectedContentLength > _maxBodySize) {
                    triggerError(context, MAX_BODY_SIZE_EXCEEDED, "Body exceeds configured maximum");
                    return;
                }
                context->responseBuffer.remove(0, headerEnd + 4);
                if (handleRedirect(context))
                    return;
                if (!context->chunked && context->responseBuffer.length() > 0) {
                    size_t incomingLen = context->responseBuffer.length();
                    if (wouldExceedLimit(incomingLen)) {
                        triggerError(context, MAX_BODY_SIZE_EXCEEDED, "Body exceeds configured maximum");
                        return;
                    }
                    if (!context->request->getNoStoreBody()) {
                        context->response->appendBody(context->responseBuffer.c_str(), incomingLen);
                    }
                    context->receivedContentLength += incomingLen;
                    auto cb = _bodyChunkCallback;
                    if (cb)
                        cb(context->responseBuffer.c_str(), incomingLen, false);
                    context->responseBuffer = "";
                }
            } else {
                triggerError(context, HEADER_PARSE_FAILED, "Failed to parse response headers");
                return;
            }
        }
    } else if (!context->chunked) {
        if (wouldExceedLimit(len)) {
            triggerError(context, MAX_BODY_SIZE_EXCEEDED, "Body exceeds configured maximum");
            return;
        }
        if (!context->request->getNoStoreBody()) {
            context->response->appendBody(data, len);
        }
        context->receivedContentLength += len;
        auto cb2 = _bodyChunkCallback;
        if (cb2)
            cb2(data, len, false);
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
            if (chunkSize > 0 && wouldExceedLimit(chunkSize)) {
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
        if (wouldExceedLimit(chunkLen)) {
            triggerError(context, MAX_BODY_SIZE_EXCEEDED, "Body exceeds configured maximum");
            return;
        }
        const char* chunkPtr = context->responseBuffer.c_str();
        if (!context->request->getNoStoreBody()) {
            context->response->appendBody(chunkPtr, chunkLen);
        }
        context->receivedContentLength += chunkLen;
        auto cb3 = _bodyChunkCallback;
        if (cb3)
            cb3(chunkPtr, chunkLen, false);
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
            value.trim();
            context->response->setHeader(name, value);
            if (name.equalsIgnoreCase("Content-Length")) {
                long parsed = value.toInt();
                if (parsed < 0)
                    parsed = 0;
                context->expectedContentLength = (size_t)parsed;
                context->response->setContentLength(context->expectedContentLength);
                bool storeBody = !(context->request->getNoStoreBody() && _bodyChunkCallback);
                if (storeBody)
                    context->response->reserveBody(context->expectedContentLength);
            } else if (name.equalsIgnoreCase("Transfer-Encoding") && value.equalsIgnoreCase("chunked")) {
                context->chunked = true;
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
        newMethod = HTTP_GET;
        dropBody = true;
    }

    AsyncHttpRequest* newRequest = new AsyncHttpRequest(newMethod, targetUrl);
    newRequest->setTimeout(context->request->getTimeout());
    newRequest->setNoStoreBody(context->request->getNoStoreBody());

    bool sameOrigin = isSameOrigin(context->request, newRequest);
    const auto& headers = context->request->getHeaders();
    for (const auto& hdr : headers) {
        if (hdr.name.equalsIgnoreCase("Content-Length"))
            continue;
        if (dropBody && hdr.name.equalsIgnoreCase("Content-Type"))
            continue;
        if (!sameOrigin &&
            (hdr.name.equalsIgnoreCase("Authorization") || hdr.name.equalsIgnoreCase("Proxy-Authorization")))
            continue;
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
    if (context->request->getNoStoreBody() && _bodyChunkCallback)
        return false;
    return true;
}

AsyncHttpTLSConfig AsyncHttpClient::resolveTlsConfig(const AsyncHttpRequest* request) const {
    AsyncHttpTLSConfig cfg = _defaultTlsConfig;
    if (!request || !request->hasTlsConfig())
        return cfg;
    const AsyncHttpTLSConfig* overrideCfg = request->getTlsConfig();
    if (!overrideCfg)
        return cfg;
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
    if (cfg.handshakeTimeoutMs == 0)
        cfg.handshakeTimeoutMs = _defaultTlsConfig.handshakeTimeoutMs;
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

static bool isPublicSuffix(const String& domain) {
    if (domain.length() == 0)
        return false;
    String lower = domain;
    lower.toLowerCase();
    for (auto suffix : kPublicSuffixes) {
        if (lower.equals(suffix))
            return true;
    }
    return false;
}

bool AsyncHttpClient::normalizeCookieDomain(String& domain, const String& host, bool domainAttributeProvided) const {
    String cleaned = domain;
    cleaned.trim();
    if (cleaned.startsWith("."))
        cleaned.remove(0, 1);

    if (!domainAttributeProvided || cleaned.length() == 0) {
        domain = host;
        return true;
    }

    String hostLower = host;
    hostLower.toLowerCase();
    cleaned.toLowerCase();

    if (isIpLiteral(hostLower))
        return false;
    if (!domainMatches(cleaned, hostLower))
        return false;
    // Heuristic public-suffix guard: require both host and domain to have at least one dot
    if (hostLower.indexOf('.') == -1)
        return false;
    if (cleaned.indexOf('.') == -1)
        return false;
    if (isPublicSuffix(cleaned))
        return false;

    domain = cleaned;
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

bool AsyncHttpClient::cookieMatchesRequest(const StoredCookie& cookie, const AsyncHttpRequest* request) const {
    if (!request)
        return false;
    if (cookie.secure && !request->isSecure())
        return false;
    if (!domainMatches(cookie.domain, request->getHost()))
        return false;
    if (!pathMatches(cookie.path, request->getPath()))
        return false;
    return !cookie.value.isEmpty();
}

void AsyncHttpClient::applyCookies(AsyncHttpRequest* request) {
    if (!request)
        return;
    String cookieHeader;
    std::vector<StoredCookie> cookiesCopy;
    lock();
    cookiesCopy = _cookies;
    unlock();
    size_t estimatedLen = 0;
    if (!cookiesCopy.empty()) {
        estimatedLen += (cookiesCopy.size() - 1) * 2; // separators
        for (const auto& cookie : cookiesCopy)
            estimatedLen += cookie.name.length() + 1 + cookie.value.length();
        cookieHeader.reserve(estimatedLen);
    }
    for (const auto& cookie : cookiesCopy) {
        if (cookieMatchesRequest(cookie, request)) {
            if (!cookieHeader.isEmpty())
                cookieHeader += "; ";
            cookieHeader += cookie.name;
            cookieHeader += "=";
            cookieHeader += cookie.value;
        }
    }
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
                long age = val.toInt();
                if (age <= 0)
                    remove = true;
            }
        }
        pos = next;
    }

    if (!normalizeCookieDomain(cookie.domain, request->getHost(), domainAttributeProvided))
        return;
    if (!cookie.path.startsWith("/"))
        cookie.path = "/" + cookie.path;
    size_t payloadSize = cookie.name.length() + cookie.value.length() + cookie.domain.length() + cookie.path.length();
    if (payloadSize > kMaxCookieBytes)
        return;

    lock();
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
            _cookies.erase(_cookies.begin());
        _cookies.push_back(cookie);
    }
    unlock();
}
