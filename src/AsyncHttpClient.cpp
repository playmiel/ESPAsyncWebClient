// New enhanced implementation

#include "AsyncHttpClient.h"
#include <algorithm>

AsyncHttpClient::AsyncHttpClient()
    : _defaultTimeout(10000), _defaultUserAgent("ESPAsyncWebClient/1.0.2"), _bodyChunkCallback(nullptr) {}

AsyncHttpClient::~AsyncHttpClient() {}

uint32_t AsyncHttpClient::get(const char* url, SuccessCallback onSuccess, ErrorCallback onError) { return makeRequest(HTTP_GET, url, nullptr, onSuccess, onError); }
uint32_t AsyncHttpClient::post(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError) { return makeRequest(HTTP_POST, url, data, onSuccess, onError); }
uint32_t AsyncHttpClient::put(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError) { return makeRequest(HTTP_PUT, url, data, onSuccess, onError); }
uint32_t AsyncHttpClient::del(const char* url, SuccessCallback onSuccess, ErrorCallback onError) { return makeRequest(HTTP_DELETE, url, nullptr, onSuccess, onError); }
uint32_t AsyncHttpClient::head(const char* url, SuccessCallback onSuccess, ErrorCallback onError) { return makeRequest(HTTP_HEAD, url, nullptr, onSuccess, onError); }
uint32_t AsyncHttpClient::patch(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError) { return makeRequest(HTTP_PATCH, url, data, onSuccess, onError); }

void AsyncHttpClient::setHeader(const char* name, const char* value) {
    String nameStr(name), valueStr(value);
    for (auto &h : _defaultHeaders) {
        if (h.name.equalsIgnoreCase(nameStr)) { h.value = valueStr; return; }
    }
    _defaultHeaders.push_back(HttpHeader(nameStr, valueStr));
}

void AsyncHttpClient::setTimeout(uint32_t timeout) { _defaultTimeout = timeout; }
void AsyncHttpClient::setUserAgent(const char* userAgent) { _defaultUserAgent = String(userAgent); }

uint32_t AsyncHttpClient::makeRequest(HttpMethod method, const char* url, const char* data,
                                  SuccessCallback onSuccess, ErrorCallback onError) {
    AsyncHttpRequest* request = new AsyncHttpRequest(method, String(url));
    for (const auto &h : _defaultHeaders) request->setHeader(h.name, h.value);
    request->setUserAgent(_defaultUserAgent);
    request->setTimeout(_defaultTimeout);
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
    // attach per-request response chunk callback if present
    // per-request chunk callback removed
    if (request->isSecure()) { // not supported yet
        triggerError(ctx, HTTPS_NOT_SUPPORTED, "HTTPS not implemented");
        return ctx->id;
    }
    executeOrQueue(ctx);
    return ctx->id;
}

// Removed per-request chunk overload

bool AsyncHttpClient::abort(uint32_t requestId) {
    // Active requests: we must be careful as triggerError() will cleanup and erase from _activeRequests
    for (size_t i = 0; i < _activeRequests.size(); ++i) {
        RequestContext* ctx = _activeRequests[i];
        if (ctx->id == requestId && !ctx->responseProcessed) { triggerError(ctx, ABORTED, "Aborted by user"); return true; }
    }
    // Pending queue: still not executed
    for (auto it = _pendingQueue.begin(); it != _pendingQueue.end(); ++it) {
        if ((*it)->id == requestId) {
            RequestContext* ctx = *it; _pendingQueue.erase(it); triggerError(ctx, ABORTED, "Aborted by user"); return true;
        }
    }
    return false;
}

void AsyncHttpClient::executeOrQueue(RequestContext* context) {
    if (_maxParallel > 0 && _activeRequests.size() >= _maxParallel) { _pendingQueue.push_back(context); return; }
    executeRequest(context);
}

void AsyncHttpClient::executeRequest(RequestContext* context) {
    context->client = new AsyncClient();
    context->connectStartMs = millis();
    _activeRequests.push_back(context);

    context->client->onConnect([this, context](void* arg, AsyncClient* c){ handleConnect(context, c); });
    context->client->onData([this, context](void* arg, AsyncClient* c, void* d, size_t l){ handleData(context, c, (char*)d, l); });
    context->client->onDisconnect([this, context](void* arg, AsyncClient* c){ handleDisconnect(context, c); });
    context->client->onError([this, context](void* arg, AsyncClient* c, int8_t e){ handleError(context, c, e); });

#if ASYNC_TCP_HAS_TIMEOUT
    context->client->setTimeout(context->request->getTimeout());
    context->client->onTimeout([this, context](void* arg, AsyncClient* c, uint32_t t){ triggerError(context, REQUEST_TIMEOUT, "Request timeout"); });
#else
    context->timeoutTimer = millis();
#endif

    if (!context->client->connect(context->request->getHost().c_str(), context->request->getPort())) {
        triggerError(context, CONNECTION_FAILED, "Failed to initiate connection");
        return;
    }
}

void AsyncHttpClient::handleConnect(RequestContext* context, AsyncClient* client) {
    if (context->request->hasBodyStream()) {
        String headers = context->request->buildHeadersOnly();
        client->write(headers.c_str(), headers.length());
        context->headersSent = true;
        context->streamingBodyInProgress = true;
        sendStreamData(context);
    } else {
        String full = context->request->buildHttpRequest();
        client->write(full.c_str(), full.length());
        context->headersSent = true;
    }
}

void AsyncHttpClient::handleData(RequestContext* context, AsyncClient* client, char* data, size_t len) {
    context->responseBuffer.concat(data, len);
    if (!context->headersComplete) {
        int headerEnd = context->responseBuffer.indexOf("\r\n\r\n");
        if (headerEnd != -1) {
            String headerData = context->responseBuffer.substring(0, headerEnd);
            if (parseResponseHeaders(context, headerData)) {
                context->headersComplete = true;
                String bodyData = context->responseBuffer.substring(headerEnd + 4);
                if (!bodyData.isEmpty()) {
                    if (context->chunked) {
                        context->responseBuffer = bodyData;
                    } else {
                        if (!(context->request->getNoStoreBody() && _bodyChunkCallback)) {
                            context->response->appendBody(bodyData.c_str(), bodyData.length());
                        }
                        context->receivedContentLength += bodyData.length();
                        if (_bodyChunkCallback) _bodyChunkCallback(bodyData.c_str(), bodyData.length(), false);
                        context->responseBuffer = "";
                    }
                } else context->responseBuffer = "";
            } else { triggerError(context, HEADER_PARSE_FAILED, "Failed to parse response headers"); return; }
        }
    } else if (!context->chunked) {
    if (!(context->request->getNoStoreBody() && _bodyChunkCallback)) {
            context->response->appendBody(data, len);
        }
        context->receivedContentLength += len;
        if (_bodyChunkCallback) _bodyChunkCallback(data, len, false);
    }

    while (context->headersComplete && context->chunked && !context->chunkedComplete) {
        if (context->currentChunkRemaining == 0) {
            int lineEnd = context->responseBuffer.indexOf("\r\n");
            if (lineEnd == -1) break;
            String sizeLine = context->responseBuffer.substring(0, lineEnd); sizeLine.trim();
            uint32_t chunkSize = 0;
            if (!parseChunkSizeLine(sizeLine, &chunkSize)) { triggerError(context, CHUNKED_DECODE_FAILED, "Chunk size parse error"); return; }
            context->currentChunkRemaining = chunkSize;
            context->responseBuffer.remove(0, lineEnd + 2);
            if (chunkSize == 0) {
                // Final chunk: mark complete and purge any (optional) trailer headers.
                context->chunkedComplete = true;
                // Trailer section ends with an empty line (CRLF CRLF). We already consumed size line.
                while (true) {
                    int lineEnd2 = context->responseBuffer.indexOf("\r\n");
                    if (lineEnd2 == -1) break; // incomplete, wait for more
                    if (lineEnd2 == 0) { // empty line => end of trailers
                        context->responseBuffer.remove(0, 2);
                        break;
                    }
                    // discard this trailer line
                    context->responseBuffer.remove(0, lineEnd2 + 2);
                }
                break;
            }
        }
        if ((int)context->responseBuffer.length() < (int)(context->currentChunkRemaining + 2)) break;
        String chunkData = context->responseBuffer.substring(0, context->currentChunkRemaining);
    if (!(context->request->getNoStoreBody() && _bodyChunkCallback)) {
            context->response->appendBody(chunkData.c_str(), chunkData.length());
        }
        context->receivedContentLength += chunkData.length();
        if (_bodyChunkCallback) _bodyChunkCallback(chunkData.c_str(), chunkData.length(), false);
        context->responseBuffer.remove(0, context->currentChunkRemaining + 2);
        context->currentChunkRemaining = 0;
    }

    if (context->headersComplete && !context->responseProcessed) {
        bool complete = false;
        if (context->chunked && context->chunkedComplete) complete = true;
        else if (!context->chunked && context->expectedContentLength > 0 && context->receivedContentLength >= context->expectedContentLength) complete = true;
        if (complete) {
            if (_bodyChunkCallback) _bodyChunkCallback(nullptr, 0, true);
            processResponse(context);
        }
    }
}

bool AsyncHttpClient::parseChunkSizeLine(const String& line, uint32_t* outSize) {
    if (!outSize) return false;
    if (line.length() == 0) return false;
    // Spec allows chunk extensions after size separated by ';'. We ignore extensions for now.
    int semi = line.indexOf(';');
    String sizePart = semi == -1 ? line : line.substring(0, semi);
    sizePart.trim();
    const char* cstr = sizePart.c_str();
    char* endptr = nullptr;
    unsigned long val = strtoul(cstr, &endptr, 16);
    if (endptr == nullptr || endptr == cstr) return false; // no conversion
    // Allow optional chunk extensions (ignored). Ensure remaining chars (before ';') consumed.
    if (*endptr != '\0') return false; // unexpected trailing chars before extension separator
    *outSize = (uint32_t)val;
    return true;
}

void AsyncHttpClient::handleDisconnect(RequestContext* context, AsyncClient* client) {
    if (context->responseProcessed) return;
    if (!context->headersComplete) { triggerError(context, CONNECTION_CLOSED, "Connection closed before headers received"); return; }
    // Headers parsed: determine if body complete.
    bool complete = false;
    if (context->chunked) {
        complete = context->chunkedComplete; // final zero-size chunk seen
    } else if (context->expectedContentLength > 0) {
        complete = (context->receivedContentLength >= context->expectedContentLength);
    } else {
        // No Content-Length: closure defines completion
        complete = true;
    }
    if (!complete) {
        triggerError(context, CONNECTION_CLOSED, "Connection closed mid-body");
        return;
    }
    processResponse(context);
}

void AsyncHttpClient::handleError(RequestContext* context, AsyncClient* client, int8_t error) {
    if (context->responseProcessed) return;
    triggerError(context, static_cast<HttpClientError>(error), "Network error");
}

bool AsyncHttpClient::parseResponseHeaders(RequestContext* context, const String& headerData) {
    int firstLineEnd = headerData.indexOf("\r\n"); if (firstLineEnd == -1) return false; String statusLine = headerData.substring(0, firstLineEnd);
    int firstSpace = statusLine.indexOf(' '); int secondSpace = statusLine.indexOf(' ', firstSpace + 1); if (firstSpace == -1 || secondSpace == -1) return false;
    int statusCode = statusLine.substring(firstSpace + 1, secondSpace).toInt(); String statusText = statusLine.substring(secondSpace + 1);
    context->response->setStatusCode(statusCode); context->response->setStatusText(statusText);
    int lineStart = firstLineEnd + 2;
    while (lineStart < headerData.length()) {
        int lineEnd = headerData.indexOf("\r\n", lineStart); if (lineEnd == -1) break; String line = headerData.substring(lineStart, lineEnd); int colonPos = line.indexOf(':');
        if (colonPos != -1) {
            String name = line.substring(0, colonPos); String value = line.substring(colonPos + 1); value.trim();
            context->response->setHeader(name, value);
            if (name.equalsIgnoreCase("Content-Length")) { context->expectedContentLength = value.toInt(); context->response->setContentLength(context->expectedContentLength); }
            else if (name.equalsIgnoreCase("Transfer-Encoding") && value.equalsIgnoreCase("chunked")) { context->chunked = true; }
        }
        lineStart = lineEnd + 2;
    }
    return true;
}

void AsyncHttpClient::processResponse(RequestContext* context) {
    if (context->responseProcessed) return;
    context->responseProcessed = true;
    if (context->onSuccess) context->onSuccess(context->response);
    cleanup(context);
}

void AsyncHttpClient::cleanup(RequestContext* context) {
    if (context->client) { context->client->close(); delete context->client; }
    if (context->request) delete context->request;
    if (context->response) delete context->response;
    auto it = std::find(_activeRequests.begin(), _activeRequests.end(), context); if (it != _activeRequests.end()) _activeRequests.erase(it);
    delete context; tryDequeue();
}

void AsyncHttpClient::triggerError(RequestContext* context, HttpClientError errorCode, const char* errorMessage) {
    if (context->responseProcessed) return;
    context->responseProcessed = true;
    if (context->onError) context->onError(errorCode, errorMessage);
    cleanup(context);
}

void AsyncHttpClient::loop() {
    uint32_t now = millis();
    // Iterate safely even if callbacks remove entries: use index loop.
    for (size_t i = 0; i < _activeRequests.size(); ) {
        RequestContext* ctx = _activeRequests[i];
#if !ASYNC_TCP_HAS_TIMEOUT
        if (!ctx->responseProcessed && (now - ctx->timeoutTimer) >= ctx->request->getTimeout()) { triggerError(ctx, REQUEST_TIMEOUT, "Request timeout"); }
#endif
        if (!ctx->responseProcessed && ctx->client && !ctx->headersSent && ctx->connectTimeoutMs > 0 && (now - ctx->connectStartMs) > ctx->connectTimeoutMs) { triggerError(ctx, CONNECT_TIMEOUT, "Connect timeout"); }
        if (!ctx->responseProcessed && ctx->streamingBodyInProgress && ctx->request->hasBodyStream()) sendStreamData(ctx);
        // If triggerError/processResponse cleaned up ctx, current index now holds a different pointer.
        if (i < _activeRequests.size() && _activeRequests[i] == ctx) {
            ++i; // still present, advance
        }
        // else do not advance: current i now refers to next element after erase
    }
}

void AsyncHttpClient::tryDequeue() {
    while (_maxParallel == 0 || _activeRequests.size() < _maxParallel) {
        if (_pendingQueue.empty()) break;
        RequestContext* ctx = _pendingQueue.front();
        _pendingQueue.erase(_pendingQueue.begin());
        executeRequest(ctx);
    }
}

void AsyncHttpClient::sendStreamData(RequestContext* context) {
    if (!context->client || !context->request->hasBodyStream()) return;
    if (!context->client->canSend()) return;
    auto provider = context->request->getBodyProvider();
    if (!provider) return;
    uint8_t temp[512]; bool final=false; int written = provider(temp, sizeof(temp), &final);
    if (written < 0) {
        triggerError(context, BODY_STREAM_READ_FAILED, "Body stream read failed");
        return;
    }
    if (written > 0) context->client->write((const char*)temp, written);
    if (final) context->streamingBodyInProgress = false;
}
