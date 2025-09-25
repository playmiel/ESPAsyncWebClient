// New enhanced implementation

#include "AsyncHttpClient.h"
#include <algorithm>

AsyncHttpClient::AsyncHttpClient()
    : _defaultTimeout(10000), _defaultUserAgent("ESPAsyncWebClient/1.0.1"), _bodyChunkCallback(nullptr) {}

AsyncHttpClient::~AsyncHttpClient() {}

void AsyncHttpClient::get(const char* url, SuccessCallback onSuccess, ErrorCallback onError) { makeRequest(HTTP_GET, url, nullptr, onSuccess, onError); }
void AsyncHttpClient::post(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError) { makeRequest(HTTP_POST, url, data, onSuccess, onError); }
void AsyncHttpClient::put(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError) { makeRequest(HTTP_PUT, url, data, onSuccess, onError); }
void AsyncHttpClient::del(const char* url, SuccessCallback onSuccess, ErrorCallback onError) { makeRequest(HTTP_DELETE, url, nullptr, onSuccess, onError); }
void AsyncHttpClient::head(const char* url, SuccessCallback onSuccess, ErrorCallback onError) { makeRequest(HTTP_HEAD, url, nullptr, onSuccess, onError); }
void AsyncHttpClient::patch(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError) { makeRequest(HTTP_PATCH, url, data, onSuccess, onError); }

void AsyncHttpClient::setHeader(const char* name, const char* value) {
    String nameStr(name), valueStr(value);
    for (auto &h : _defaultHeaders) {
        if (h.name.equalsIgnoreCase(nameStr)) { h.value = valueStr; return; }
    }
    _defaultHeaders.push_back(HttpHeader(nameStr, valueStr));
}

void AsyncHttpClient::setTimeout(uint32_t timeout) { _defaultTimeout = timeout; }
void AsyncHttpClient::setUserAgent(const char* userAgent) { _defaultUserAgent = String(userAgent); }

void AsyncHttpClient::makeRequest(HttpMethod method, const char* url, const char* data,
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
    this->request(request, onSuccess, onError);
}

void AsyncHttpClient::request(AsyncHttpRequest* request, SuccessCallback onSuccess, ErrorCallback onError) {
    RequestContext* ctx = new RequestContext();
    ctx->request = request;
    ctx->response = new AsyncHttpResponse();
    ctx->onSuccess = onSuccess;
    ctx->onError = onError;
    ctx->id = _nextRequestId++;
    ctx->connectTimeoutMs = _defaultConnectTimeout;
    if (request->isSecure()) { // not supported yet
        triggerError(ctx, HTTPS_NOT_SUPPORTED, "HTTPS not implemented");
        return;
    }
    executeOrQueue(ctx);
}

bool AsyncHttpClient::abort(uint32_t requestId) {
    for (auto *ctx : _activeRequests) {
        if (ctx->id == requestId && !ctx->responseProcessed) { triggerError(ctx, CONNECTION_CLOSED, "Aborted by user"); return true; }
    }
    for (auto it = _pendingQueue.begin(); it != _pendingQueue.end(); ++it) {
        if ((*it)->id == requestId) {
            RequestContext* ctx = *it; _pendingQueue.erase(it); triggerError(ctx, CONNECTION_CLOSED, "Aborted by user"); return true;
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
                        context->response->appendBody(bodyData.c_str(), bodyData.length());
                        context->receivedContentLength += bodyData.length();
                        if (context->perRequestChunkCb) context->perRequestChunkCb(bodyData.c_str(), bodyData.length(), false);
                        if (_bodyChunkCallback) _bodyChunkCallback(bodyData.c_str(), bodyData.length(), false);
                        context->responseBuffer = "";
                    }
                } else context->responseBuffer = "";
            } else { triggerError(context, HEADER_PARSE_FAILED, "Failed to parse response headers"); return; }
        }
    } else if (!context->chunked) {
        context->response->appendBody(data, len);
        context->receivedContentLength += len;
        if (context->perRequestChunkCb) context->perRequestChunkCb(data, len, false);
        if (_bodyChunkCallback) _bodyChunkCallback(data, len, false);
    }

    while (context->headersComplete && context->chunked && !context->chunkedComplete) {
        if (context->currentChunkRemaining == 0) {
            int lineEnd = context->responseBuffer.indexOf("\r\n");
            if (lineEnd == -1) break;
            String sizeLine = context->responseBuffer.substring(0, lineEnd); sizeLine.trim();
            char *endptr = nullptr; uint32_t chunkSize = strtoul(sizeLine.c_str(), &endptr, 16);
            if (endptr == nullptr) { triggerError(context, CHUNKED_DECODE_FAILED, "Chunk size parse error"); return; }
            context->currentChunkRemaining = chunkSize;
            context->responseBuffer.remove(0, lineEnd + 2);
            if (chunkSize == 0) { context->chunkedComplete = true; int crlf = context->responseBuffer.indexOf("\r\n"); if (crlf != -1) context->responseBuffer.remove(0, crlf + 2); break; }
        }
        if ((int)context->responseBuffer.length() < (int)(context->currentChunkRemaining + 2)) break;
        String chunkData = context->responseBuffer.substring(0, context->currentChunkRemaining);
        context->response->appendBody(chunkData.c_str(), chunkData.length());
        context->receivedContentLength += chunkData.length();
        if (context->perRequestChunkCb) context->perRequestChunkCb(chunkData.c_str(), chunkData.length(), false);
        if (_bodyChunkCallback) _bodyChunkCallback(chunkData.c_str(), chunkData.length(), false);
        context->responseBuffer.remove(0, context->currentChunkRemaining + 2);
        context->currentChunkRemaining = 0;
    }

    if (context->headersComplete && !context->responseProcessed) {
        bool complete = false;
        if (context->chunked && context->chunkedComplete) complete = true;
        else if (!context->chunked && context->expectedContentLength > 0 && context->receivedContentLength >= context->expectedContentLength) complete = true;
        if (complete) {
            if (context->perRequestChunkCb) context->perRequestChunkCb(nullptr, 0, true);
            if (_bodyChunkCallback) _bodyChunkCallback(nullptr, 0, true);
            processResponse(context);
        }
    }
}

void AsyncHttpClient::handleDisconnect(RequestContext* context, AsyncClient* client) {
    if (context->responseProcessed) return;
    if (context->headersComplete) processResponse(context);
    else triggerError(context, CONNECTION_CLOSED, "Connection closed before headers received");
}

void AsyncHttpClient::handleError(RequestContext* context, AsyncClient* client, int8_t error) {
    if (context->responseProcessed) return; triggerError(context, static_cast<HttpClientError>(error), "Network error"); }

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
    if (context->responseProcessed) return; context->responseProcessed = true; if (context->onSuccess) context->onSuccess(context->response); cleanup(context); }

void AsyncHttpClient::cleanup(RequestContext* context) {
    if (context->client) { context->client->close(); delete context->client; }
    if (context->request) delete context->request;
    if (context->response) delete context->response;
    auto it = std::find(_activeRequests.begin(), _activeRequests.end(), context); if (it != _activeRequests.end()) _activeRequests.erase(it);
    delete context; tryDequeue();
}

void AsyncHttpClient::triggerError(RequestContext* context, HttpClientError errorCode, const char* errorMessage) {
    if (context->responseProcessed) return; context->responseProcessed = true; if (context->onError) context->onError(errorCode, errorMessage); cleanup(context); }

void AsyncHttpClient::loop() {
    uint32_t now = millis();
    for (auto *ctx : _activeRequests) {
#if !ASYNC_TCP_HAS_TIMEOUT
        if (!ctx->responseProcessed && (now - ctx->timeoutTimer) >= ctx->request->getTimeout()) triggerError(ctx, REQUEST_TIMEOUT, "Request timeout");
#endif
        if (!ctx->responseProcessed && ctx->client && !ctx->headersSent && ctx->connectTimeoutMs > 0 && (now - ctx->connectStartMs) > ctx->connectTimeoutMs) triggerError(ctx, CONNECT_TIMEOUT, "Connect timeout");
        if (!ctx->responseProcessed && ctx->streamingBodyInProgress && ctx->request->hasBodyStream()) sendStreamData(ctx);
    }
}

void AsyncHttpClient::tryDequeue() {
    while (_maxParallel == 0 || _activeRequests.size() < _maxParallel) {
        if (_pendingQueue.empty()) break; RequestContext* ctx = _pendingQueue.front(); _pendingQueue.erase(_pendingQueue.begin()); executeRequest(ctx);
    }
}

void AsyncHttpClient::sendStreamData(RequestContext* context) {
    if (!context->client || !context->request->hasBodyStream()) return; if (!context->client->canSend()) return;
    auto provider = context->request->getBodyProvider(); if (!provider) return;
    uint8_t temp[512]; bool final=false; int written = provider(temp, sizeof(temp), &final);
    if (written < 0) { triggerError(context, BODY_STREAM_READ_FAILED, "Body stream read failed"); return; }
    if (written > 0) context->client->write((const char*)temp, written);
    if (final) context->streamingBodyInProgress = false;
}
