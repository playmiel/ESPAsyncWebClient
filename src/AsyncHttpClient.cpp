

#include "AsyncHttpClient.h"
#include <algorithm>
#include <cstdlib>
#include <cerrno>
#include "UrlParser.h"

static constexpr size_t kMaxChunkSizeLineLen = 64;
static constexpr size_t kMaxChunkTrailerLineLen = 256;
static constexpr size_t kMaxChunkTrailerLines = 32;

AsyncHttpClient::AsyncHttpClient()
    : _defaultTimeout(10000), _defaultUserAgent(String("ESPAsyncWebClient/") + ESP_ASYNC_WEB_CLIENT_VERSION),
      _bodyChunkCallback(nullptr), _followRedirects(false), _maxRedirectHops(3), _maxHeaderBytes(0) {
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
    context->connectStartMs = millis();
    context->transport = buildTransport(context);
    if (!context->transport) {
        triggerError(context, HTTPS_NOT_SUPPORTED, "HTTPS transport unavailable");
        return;
    }
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

    if (!context->transport->connect(context->request->getHost().c_str(), context->request->getPort())) {
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
    if (context->transport) {
        context->transport->close();
        delete context->transport;
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

AsyncTransport* AsyncHttpClient::buildTransport(RequestContext* context) {
    if (!context || !context->request)
        return nullptr;
    if (context->request->isSecure()) {
        AsyncHttpTLSConfig cfg = resolveTlsConfig(context->request);
        if (cfg.handshakeTimeoutMs == 0)
            cfg.handshakeTimeoutMs = _defaultTlsConfig.handshakeTimeoutMs;
        return createTlsTransport(cfg);
    }
    return createTcpTransport();
}
