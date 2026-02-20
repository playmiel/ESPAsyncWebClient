#include "RedirectHandler.h"
#include "HttpHelpers.h"
#include <cstring>

RedirectHandler::RedirectHandler(AsyncHttpClient* client) : _client(client) {}

void RedirectHandler::lock() const {
    if (_client)
        _client->lock();
}

void RedirectHandler::unlock() const {
    if (_client)
        _client->unlock();
}

void RedirectHandler::setRedirectHeaderPolicy(AsyncHttpClient::RedirectHeaderPolicy policy) {
    lock();
    _redirectHeaderPolicy = policy;
    unlock();
}

void RedirectHandler::addRedirectSafeHeader(const char* name) {
    if (!name || strlen(name) == 0)
        return;
    String normalized = name;
    normalized.trim();
    normalized.toLowerCase();
    if (normalized.length() == 0)
        return;
    lock();
    for (const auto& existing : _redirectSafeHeaders) {
        if (existing == normalized) {
            unlock();
            return;
        }
    }
    _redirectSafeHeaders.push_back(normalized);
    unlock();
}

void RedirectHandler::clearRedirectSafeHeaders() {
    lock();
    _redirectSafeHeaders.clear();
    unlock();
}

bool RedirectHandler::isRedirectStatus(int status) const {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

String RedirectHandler::resolveRedirectUrl(const AsyncHttpRequest* request, const String& location) const {
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

bool RedirectHandler::isSameOrigin(const AsyncHttpRequest* original, const AsyncHttpRequest* redirect) const {
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

bool RedirectHandler::buildRedirectRequest(AsyncHttpClient::RequestContext* context,
                                           std::unique_ptr<AsyncHttpRequest>* outRequest, HttpClientError* outError,
                                           String* outErrorMessage) {
    if (outRequest)
        outRequest->reset();
    if (outError)
        *outError = CONNECTION_FAILED;
    if (outErrorMessage)
        *outErrorMessage = "";

    if (!context || !_client || !_client->_followRedirects || !context->response || !context->request)
        return false;

    int status = context->response->getStatusCode();
    if (!isRedirectStatus(status))
        return false;

    String location = context->response->getHeader("Location");
    if (location.length() == 0)
        return false;

    if (context->redirect.redirectCount >= _client->_maxRedirectHops) {
        if (outError)
            *outError = TOO_MANY_REDIRECTS;
        if (outErrorMessage)
            *outErrorMessage = "Too many redirects";
        return true;
    }

    String targetUrl = resolveRedirectUrl(context->request.get(), location);
    if (targetUrl.length() == 0)
        return false;

    HttpMethod newMethod = context->request->getMethod();
    bool dropBody = false;
    if (status == 301 || status == 302 || status == 303) {
        newMethod = HTTP_METHOD_GET;
        dropBody = true;
    }

    std::unique_ptr<AsyncHttpRequest> newRequest(new AsyncHttpRequest(newMethod, targetUrl));
    newRequest->setTimeout(context->request->getTimeout());
    newRequest->setNoStoreBody(context->request->getNoStoreBody());

    bool sameOrigin = isSameOrigin(context->request.get(), newRequest.get());
    AsyncHttpClient::RedirectHeaderPolicy headerPolicy;
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
        String lower = name;
        lower.toLowerCase();
        if (lower == "user-agent")
            return true;
        if (lower == "accept")
            return true;
        if (lower == "accept-encoding")
            return true;
        if (lower == "accept-language")
            return true;
        if (!dropBody && lower == "content-type")
            return true;
        return false;
    };
    auto isAllowlistedForCrossOrigin = [&redirectSafeHeaders](const String& name) {
        String lower = name;
        lower.toLowerCase();
        for (const auto& allowed : redirectSafeHeaders) {
            if (allowed == lower)
                return true;
        }
        return false;
    };
    const auto& headers = context->request->getHeaders();
    for (const auto& hdr : headers) {
        if (hdr.name == "content-length")
            continue;
        // Always rebuild cookies for the redirected request from the cookie jar (avoids duplicates and leaks).
        if (hdr.name == "cookie" || hdr.name == "cookie2")
            continue;
        // Prevent callers from pinning an old Host header across redirects.
        if (hdr.name == "host")
            continue;
        if (dropBody && hdr.name == "content-type")
            continue;
        if (!sameOrigin) {
            if (headerPolicy == AsyncHttpClient::RedirectHeaderPolicy::kLegacyDropSensitiveOnly) {
                if (isCrossOriginSensitiveHeader(hdr.name))
                    continue;
            } else if (headerPolicy == AsyncHttpClient::RedirectHeaderPolicy::kDropAllCrossOrigin) {
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
            return false; // cannot replay streamed bodies automatically
        }
    }

    if (outRequest)
        *outRequest = std::move(newRequest);

    return true;
}

void RedirectHandler::resetContextForRedirect(AsyncHttpClient::RequestContext* context,
                                              std::unique_ptr<AsyncHttpRequest> newRequest) {
    if (!context || !_client)
        return;
    if (context->transport) {
        context->transport->close();
        delete context->transport;
        context->transport = nullptr;
    }
    context->request.reset();
    context->response.reset();
    context->request = std::move(newRequest);
    context->response = std::make_shared<AsyncHttpResponse>();
    context->responseBuffer = "";
    context->headersComplete = false;
    context->responseProcessed = false;
    context->expectedContentLength = 0;
    context->receivedContentLength = 0;
    context->receivedBodyLength = 0;
    context->chunk.chunked = false;
    context->chunk.chunkedComplete = false;
    context->chunk.currentChunkRemaining = 0;
    context->chunk.awaitingFinalChunkTerminator = false;
    context->chunk.trailerLineCount = 0;
    context->headersSent = false;
    context->streamingBodyInProgress = false;
    context->notifiedEndCallback = false;
    context->requestKeepAlive = false;
    context->serverRequestedClose = false;
    context->usingPooledConnection = false;
    context->resolvedTlsConfig = AsyncHttpTLSConfig();
#if ASYNC_HTTP_ENABLE_GZIP_DECODE
    context->gzip.gzipEncoded = false;
    context->gzip.gzipDecodeActive = false;
    context->gzip.gzipDecoder.reset();
#endif
#if !ASYNC_TCP_HAS_TIMEOUT
    context->timing.timeoutTimer = millis();
#endif
    _client->executeRequest(context);
}

bool RedirectHandler::handleRedirect(AsyncHttpClient::RequestContext* context) {
    std::unique_ptr<AsyncHttpRequest> newRequest;
    HttpClientError redirectError = CONNECTION_FAILED;
    String errorMessage;
    bool decision = buildRedirectRequest(context, &newRequest, &redirectError, &errorMessage);
    if (!decision)
        return false;
    if (!newRequest) {
        if (_client)
            _client->triggerError(context, redirectError, errorMessage.c_str());
        return true;
    }

    context->redirect.redirectCount++;
    resetContextForRedirect(context, std::move(newRequest));
    return true;
}
