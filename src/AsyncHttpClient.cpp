#include "AsyncHttpClient.h"

AsyncHttpClient::AsyncHttpClient() 
    : _defaultTimeout(10000), _defaultUserAgent("ESPAsyncWebClient/1.0.0") {
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
    
    context->client->onTimeout([this, context](void* arg, AsyncClient* client, uint32_t time) {
        handleTimeout(context, client);
    });
    
    // Start connection
    if (!context->client->connect(context->request->getHost().c_str(), context->request->getPort())) {
        triggerError(context, -1, "Failed to initiate connection");
        return;
    }
    
    // Set timeout
    context->timeoutTimer = millis();
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
                    context->response->appendBody(bodyData.c_str(), bodyData.length());
                    context->receivedContentLength += bodyData.length();
                }
                context->responseBuffer = "";
            } else {
                triggerError(context, -2, "Failed to parse response headers");
                return;
            }
        }
    } else {
        // We're receiving body data
        context->response->appendBody(data, len);
        context->receivedContentLength += len;
    }
    
    // Check if we've received all expected content
    if (context->headersComplete && !context->responseProcessed &&
        context->expectedContentLength > 0 && 
        context->receivedContentLength >= context->expectedContentLength) {
        processResponse(context);
    }
}

void AsyncHttpClient::handleDisconnect(RequestContext* context, AsyncClient* client) {
    if (context->responseProcessed) return;
    
    if (context->headersComplete) {
        processResponse(context);
    } else {
        triggerError(context, -3, "Connection closed before headers received");
    }
}

void AsyncHttpClient::handleError(RequestContext* context, AsyncClient* client, int8_t error) {
    if (context->responseProcessed) return;
    triggerError(context, error, "Network error");
}

void AsyncHttpClient::handleTimeout(RequestContext* context, AsyncClient* client) {
    if (context->responseProcessed) return;
    triggerError(context, -4, "Request timeout");
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
    delete context;
}

void AsyncHttpClient::triggerError(RequestContext* context, int errorCode, const char* errorMessage) {
    if (context->responseProcessed) return;
    context->responseProcessed = true;
    
    if (context->onError) {
        context->onError(errorCode, errorMessage);
    }
    cleanup(context);
}