#include "AsyncHttpClient.h"
#include <algorithm>

AsyncHttpClient::AsyncHttpClient() 
    : _defaultTimeout(10000), _defaultUserAgent("ESPAsyncWebClient/1.0.1"), _bodyChunkCallback(nullptr) {
}

AsyncHttpClient::~AsyncHttpClient() {
}

void AsyncHttpClient::get(const char* url, SuccessCallback onSuccess, ErrorCallback onError) {
    makeRequest(HTTP_GET, url, nullptr, onSuccess, onError);
}

void AsyncHttpClient::post(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError) {
    makeRequest(HTTP_POST, url, data, onSuccess, onError);
}

void AsyncHttpClient::put(const char* url, const char* data, SuccessCallback onSuccess, ErrorCallback onError) {
    makeRequest(HTTP_PUT, url, data, onSuccess, onError);
}

void AsyncHttpClient::del(const char* url, SuccessCallback onSuccess, ErrorCallback onError) {
    makeRequest(HTTP_DELETE, url, nullptr, onSuccess, onError);
}

void AsyncHttpClient::setHeader(const char* name, const char* value) {
    String nameStr = String(name);
    String valueStr = String(value);
    
    // Check if header already exists and update it
    for (auto& header : _defaultHeaders) {
        if (header.name.equalsIgnoreCase(nameStr)) {
            header.value = valueStr;
            return;
        }
    }
    // Add new header
    _defaultHeaders.push_back(HttpHeader(nameStr, valueStr));
}

void AsyncHttpClient::setTimeout(uint32_t timeout) {
    _defaultTimeout = timeout;
}

void AsyncHttpClient::setUserAgent(const char* userAgent) {
    _defaultUserAgent = String(userAgent);
}

void AsyncHttpClient::makeRequest(HttpMethod method, const char* url, const char* data, 
                                 SuccessCallback onSuccess, ErrorCallback onError) {
    AsyncHttpRequest* request = new AsyncHttpRequest(method, String(url));
    
    // Apply default headers
    for (const auto& header : _defaultHeaders) {
        request->setHeader(header.name, header.value);
    }
    
    // Set user agent
    request->setUserAgent(_defaultUserAgent);
    
    // Set timeout
    request->setTimeout(_defaultTimeout);
    
    // Set body if provided
    if (data) {
        request->setBody(String(data));
        request->setHeader("Content-Type", "application/x-www-form-urlencoded");
    }
    
    this->request(request, onSuccess, onError);
}

void AsyncHttpClient::request(AsyncHttpRequest* request, SuccessCallback onSuccess, ErrorCallback onError) {
    RequestContext* context = new RequestContext();
    context->request = request;
    context->response = new AsyncHttpResponse();
    context->onSuccess = onSuccess;
    context->onError = onError;
    context->id = _nextRequestId++;
bool AsyncHttpClient::abort(uint32_t requestId) {
    for (auto* ctx : _activeRequests) {
        if (ctx->id == requestId && !ctx->responseProcessed) {
            triggerError(ctx, CONNECTION_CLOSED, "Aborted by user");
            return true;
        }
    }
    return false;
}
    
    if (request->isSecure()) { // HTTPS not implemented yet
        triggerError(context, HTTPS_NOT_SUPPORTED, "HTTPS not implemented");
        return;
    }

    executeRequest(context);
}

void AsyncHttpClient::executeRequest(RequestContext* context) {
    context->client = new AsyncClient();

    // Set up callbacks
    context->client->onConnect([this, context](void* arg, AsyncClient* client) {
        handleConnect(context, client);
    });

    context->client->onData([this, context](void* arg, AsyncClient* client, void* data, size_t len) {
        handleData(context, client, (char*)data, len);
    });

    context->client->onDisconnect([this, context](void* arg, AsyncClient* client) {
        handleDisconnect(context, client);
    });

    context->client->onError([this, context](void* arg, AsyncClient* client, int8_t error) {
        handleError(context, client, error);
    });

    // Always track context for potential future abort/inspection
    _activeRequests.push_back(context);

#if ASYNC_TCP_HAS_TIMEOUT
    context->client->setTimeout(context->request->getTimeout());
    context->client->onTimeout([this, context](void* arg, AsyncClient* client, uint32_t time) {
        triggerError(context, REQUEST_TIMEOUT, "Request timeout");
    });
#else
    context->timeoutTimer = millis();
#endif

    // Start connection
    if (!context->client->connect(context->request->getHost().c_str(), context->request->getPort())) {
        triggerError(context, CONNECTION_FAILED, "Failed to initiate connection");
        return;
    }
}

void AsyncHttpClient::handleConnect(RequestContext* context, AsyncClient* client) {
    String httpRequest = context->request->buildHttpRequest();
    client->write(httpRequest.c_str(), httpRequest.length());
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
                        context->responseBuffer = bodyData; // keep for chunk parser
                    } else {
                        context->response->appendBody(bodyData.c_str(), bodyData.length());
                        context->receivedContentLength += bodyData.length();
                        if (_bodyChunkCallback) _bodyChunkCallback(bodyData.c_str(), bodyData.length(), false);
                        context->responseBuffer = "";
                    }
                } else {
                    context->responseBuffer = "";
                }
            } else {
                triggerError(context, HEADER_PARSE_FAILED, "Failed to parse response headers");
                return;
            }
        }
    } else if (!context->chunked) {
        // Normal body streaming
    context->response->appendBody(data, len);
    context->receivedContentLength += len;
    if (_bodyChunkCallback) _bodyChunkCallback(data, len, false);
    }
    
    // Chunked decoding loop
    while (context->headersComplete && context->chunked && !context->chunkedComplete) {
        if (context->currentChunkRemaining == 0) {
            int lineEnd = context->responseBuffer.indexOf("\r\n");
            if (lineEnd == -1) break; // wait more data
            String sizeLine = context->responseBuffer.substring(0, lineEnd);
            sizeLine.trim();
            char *endptr = nullptr; // strtoul on Arduino returns endptr differently but acceptable
            uint32_t chunkSize = strtoul(sizeLine.c_str(), &endptr, 16);
            if (endptr == nullptr) {
                triggerError(context, CHUNKED_DECODE_FAILED, "Chunk size parse error");
                return;
            }
            context->currentChunkRemaining = chunkSize;
            context->responseBuffer.remove(0, lineEnd + 2);
            if (chunkSize == 0) {
                // final chunk -> mark complete (ignore potential trailers for now)
                context->chunkedComplete = true;
                // consume trailing CRLF if present
                int crlfPos = context->responseBuffer.indexOf("\r\n");
                if (crlfPos != -1) {
                    context->responseBuffer.remove(0, crlfPos + 2);
                }
                break;
            }
        }
        if ((int)context->responseBuffer.length() < (int)(context->currentChunkRemaining + 2)) {
            break; // incomplete chunk payload
        }
        String chunkData = context->responseBuffer.substring(0, context->currentChunkRemaining);
    context->response->appendBody(chunkData.c_str(), chunkData.length());
    context->receivedContentLength += chunkData.length();
    if (_bodyChunkCallback) _bodyChunkCallback(chunkData.c_str(), chunkData.length(), false);
        context->responseBuffer.remove(0, context->currentChunkRemaining + 2); // remove data + CRLF
        context->currentChunkRemaining = 0;
    }

    // Completion check
    if (context->headersComplete && !context->responseProcessed) {
        bool complete = false;
        if (context->chunked && context->chunkedComplete) {
            complete = true;
        } else if (!context->chunked && context->expectedContentLength > 0 && context->receivedContentLength >= context->expectedContentLength) {
            complete = true; // extra bytes beyond Content-Length will be ignored
        }
        if (complete) {
            if (_bodyChunkCallback) _bodyChunkCallback(nullptr, 0, true);
            processResponse(context);
        }
    }
}

void AsyncHttpClient::handleDisconnect(RequestContext* context, AsyncClient* client) {
    if (context->responseProcessed) return;
    
    if (context->headersComplete) {
        processResponse(context);
    } else {
        triggerError(context, CONNECTION_CLOSED, "Connection closed before headers received");
    }
}

void AsyncHttpClient::handleError(RequestContext* context, AsyncClient* client, int8_t error) {
    if (context->responseProcessed) return;
    triggerError(context, static_cast<HttpClientError>(error), "Network error");
}

bool AsyncHttpClient::parseResponseHeaders(RequestContext* context, const String& headerData) {
    int firstLineEnd = headerData.indexOf("\r\n");
    if (firstLineEnd == -1) return false;
    
    String statusLine = headerData.substring(0, firstLineEnd);
    
    // Parse status line: "HTTP/1.1 200 OK"
    int firstSpace = statusLine.indexOf(' ');
    int secondSpace = statusLine.indexOf(' ', firstSpace + 1);
    
    if (firstSpace == -1 || secondSpace == -1) return false;
    
    int statusCode = statusLine.substring(firstSpace + 1, secondSpace).toInt();
    String statusText = statusLine.substring(secondSpace + 1);
    
    context->response->setStatusCode(statusCode);
    context->response->setStatusText(statusText);
    
    // Parse headers
    int lineStart = firstLineEnd + 2;
    while (lineStart < headerData.length()) {
        int lineEnd = headerData.indexOf("\r\n", lineStart);
        if (lineEnd == -1) break;
        
        String line = headerData.substring(lineStart, lineEnd);
        int colonPos = line.indexOf(':');
        
        if (colonPos != -1) {
            String name = line.substring(0, colonPos);
            String value = line.substring(colonPos + 1);
            value.trim();
            
            context->response->setHeader(name, value);
            
            // Check for content length
            if (name.equalsIgnoreCase("Content-Length")) {
                context->expectedContentLength = value.toInt();
                context->response->setContentLength(context->expectedContentLength);
            } else if (name.equalsIgnoreCase("Transfer-Encoding")) {
                if (value.equalsIgnoreCase("chunked")) {
                    context->chunked = true;
                }
            }
        }
        
        lineStart = lineEnd + 2;
    }
    
    return true;
}

void AsyncHttpClient::processResponse(RequestContext* context) {
    if (context->responseProcessed) return;
    context->responseProcessed = true;
    
    if (context->onSuccess) {
        // WARNING: response pointer only valid inside callback (is freed after return)
        context->onSuccess(context->response);
    }
    cleanup(context);
}

void AsyncHttpClient::cleanup(RequestContext* context) {
    if (context->client) {
        context->client->close();
        delete context->client;
    }
    if (context->request) {
        delete context->request;
    }
    if (context->response) {
        delete context->response;
    }
    auto it = std::find(_activeRequests.begin(), _activeRequests.end(), context);
    if (it != _activeRequests.end()) {
        _activeRequests.erase(it);
    }
    delete context;
}

void AsyncHttpClient::triggerError(RequestContext* context, HttpClientError errorCode, const char* errorMessage) {
    if (context->responseProcessed) return;
    context->responseProcessed = true;

    if (context->onError) {
        context->onError(errorCode, errorMessage);
    }
    cleanup(context);
}

void AsyncHttpClient::loop() {
    uint32_t now = millis();
    for (auto it = _activeRequests.begin(); it != _activeRequests.end(); ) {
        RequestContext* ctx = *it;
        #if !ASYNC_TCP_HAS_TIMEOUT
        if (!ctx->responseProcessed && (now - ctx->timeoutTimer) >= ctx->request->getTimeout()) {
            triggerError(ctx, REQUEST_TIMEOUT, "Request timeout");
        }
        #endif
        if (ctx->responseProcessed) {
            it = _activeRequests.erase(it); // removal happens after cleanup
        } else {
            ++it;
        }
    }
}
