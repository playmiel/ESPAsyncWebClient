#include <Arduino.h>
#include <unity.h>
#include <cstring>

#define private public
#include "AsyncHttpClient.h"
#undef private

static AsyncHttpClient::RequestContext* makeRedirectContext(HttpMethod method, const char* url) {
    auto ctx = new AsyncHttpClient::RequestContext();
    ctx->request = new AsyncHttpRequest(method, url);
    ctx->response = new AsyncHttpResponse();
    ctx->client = nullptr;
    ctx->headersComplete = true;
    return ctx;
}

static void cleanupContext(AsyncHttpClient::RequestContext* ctx) {
    if (!ctx)
        return;
    if (ctx->request) {
        delete ctx->request;
        ctx->request = nullptr;
    }
    if (ctx->response) {
        delete ctx->response;
        ctx->response = nullptr;
    }
    delete ctx;
}

static void test_redirect_same_host_get() {
    AsyncHttpClient client;
    client.setFollowRedirects(true, 3);
    auto ctx = makeRedirectContext(HTTP_POST, "http://example.com/path");
    ctx->request->setHeader("Authorization", "Bearer token");
    ctx->request->setHeader("Content-Type", "text/plain");
    ctx->request->setBody("payload");

    ctx->response->setStatusCode(302);
    ctx->response->setHeader("Location", "/next");

    AsyncHttpRequest* newReq = nullptr;
    HttpClientError err = CONNECTION_FAILED;
    String message;
    bool decision = client.buildRedirectRequest(ctx, &newReq, &err, &message);

    TEST_ASSERT_TRUE(decision);
    TEST_ASSERT_NOT_NULL(newReq);
    TEST_ASSERT_EQUAL(HTTP_GET, newReq->getMethod());
    TEST_ASSERT_TRUE(newReq->getBody().isEmpty());
    TEST_ASSERT_EQUAL_STRING("Bearer token", newReq->getHeader("Authorization").c_str());
    TEST_ASSERT_TRUE(newReq->getHeader("Content-Type").isEmpty());

    delete newReq;
    cleanupContext(ctx);
}

static void test_redirect_cross_host_preserve_method_strip_auth() {
    AsyncHttpClient client;
    client.setFollowRedirects(true, 3);
    auto ctx = makeRedirectContext(HTTP_POST, "http://example.com/login");
    ctx->request->setHeader("Authorization", "Bearer token");
    ctx->request->setHeader("Proxy-Authorization", "Basic abc");
    ctx->request->setHeader("Content-Type", "application/json");
    ctx->request->setBody("{\"name\":\"demo\"}");

    ctx->response->setStatusCode(307);
    ctx->response->setHeader("Location", "http://other.example.com/session");

    AsyncHttpRequest* newReq = nullptr;
    HttpClientError err = CONNECTION_FAILED;
    String message;
    bool decision = client.buildRedirectRequest(ctx, &newReq, &err, &message);

    TEST_ASSERT_TRUE(decision);
    TEST_ASSERT_NOT_NULL(newReq);
    TEST_ASSERT_EQUAL(HTTP_POST, newReq->getMethod());
    TEST_ASSERT_EQUAL_STRING("{\"name\":\"demo\"}", newReq->getBody().c_str());
    TEST_ASSERT_TRUE(newReq->getHeader("Authorization").isEmpty());
    TEST_ASSERT_TRUE(newReq->getHeader("Proxy-Authorization").isEmpty());
    TEST_ASSERT_EQUAL_STRING("application/json", newReq->getHeader("Content-Type").c_str());

    delete newReq;
    cleanupContext(ctx);
}

static void test_redirect_too_many_hops() {
    AsyncHttpClient client;
    client.setFollowRedirects(true, 2);
    auto ctx = makeRedirectContext(HTTP_GET, "http://example.com/a");
    ctx->redirectCount = 2;
    ctx->response->setStatusCode(302);
    ctx->response->setHeader("Location", "/b");

    AsyncHttpRequest* newReq = nullptr;
    HttpClientError err = CONNECTION_FAILED;
    String message;
    bool decision = client.buildRedirectRequest(ctx, &newReq, &err, &message);

    TEST_ASSERT_TRUE(decision);
    TEST_ASSERT_NULL(newReq);
    TEST_ASSERT_EQUAL(TOO_MANY_REDIRECTS, err);
    TEST_ASSERT_FALSE(message.isEmpty());

    cleanupContext(ctx);
}

static void test_redirect_to_https_not_supported() {
    AsyncHttpClient client;
    client.setFollowRedirects(true, 3);
    auto ctx = makeRedirectContext(HTTP_GET, "http://example.com/path");
    ctx->response->setStatusCode(301);
    ctx->response->setHeader("Location", "https://secure.example.com/next");

    AsyncHttpRequest* newReq = nullptr;
    HttpClientError err = CONNECTION_FAILED;
    String message;
    bool decision = client.buildRedirectRequest(ctx, &newReq, &err, &message);

    TEST_ASSERT_TRUE(decision);
    TEST_ASSERT_NULL(newReq);
    TEST_ASSERT_EQUAL(HTTPS_NOT_SUPPORTED, err);
    TEST_ASSERT_FALSE(message.isEmpty());

    cleanupContext(ctx);
}

static bool gHeaderErrorCalled = false;
static HttpClientError gHeaderLastError = CONNECTION_FAILED;
static bool gHeaderSuccessCalled = false;
static String gHeaderLastBody;

static void test_header_limit_triggers_error() {
    gHeaderErrorCalled = false;
    gHeaderLastError = CONNECTION_FAILED;

    AsyncHttpClient client;
    client.setMaxHeaderBytes(32);
    auto ctx = new AsyncHttpClient::RequestContext();
    ctx->request = new AsyncHttpRequest(HTTP_GET, "http://example.com/");
    ctx->response = new AsyncHttpResponse();
    ctx->onError = [](HttpClientError error, const char* message) {
        (void)message;
        gHeaderErrorCalled = true;
        gHeaderLastError = error;
    };

    const char* partialHeaders = "HTTP/1.1 200 OK\r\nX-Very-Long-Header: 0123456789012345678901234567890123456789";
    client.handleData(ctx, nullptr, const_cast<char*>(partialHeaders), strlen(partialHeaders));

    TEST_ASSERT_TRUE(gHeaderErrorCalled);
    TEST_ASSERT_EQUAL(HEADERS_TOO_LARGE, gHeaderLastError);
}

static void test_header_limit_allows_body_bytes_after_headers() {
    gHeaderErrorCalled = false;
    gHeaderLastError = CONNECTION_FAILED;
    gHeaderSuccessCalled = false;
    gHeaderLastBody = "";

    AsyncHttpClient client;
    client.setMaxHeaderBytes(48);
    auto ctx = new AsyncHttpClient::RequestContext();
    ctx->request = new AsyncHttpRequest(HTTP_GET, "http://example.com/");
    ctx->response = new AsyncHttpResponse();
    ctx->onError = [](HttpClientError error, const char* message) {
        (void)message;
        gHeaderErrorCalled = true;
        gHeaderLastError = error;
    };
    ctx->onSuccess = [](AsyncHttpResponse* resp) {
        gHeaderSuccessCalled = true;
        gHeaderLastBody = resp->getBody();
    };

    const char* frame = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nHELLOWORLD";
    client.handleData(ctx, nullptr, const_cast<char*>(frame), strlen(frame));

    TEST_ASSERT_FALSE(gHeaderErrorCalled);
    TEST_ASSERT_TRUE(gHeaderSuccessCalled);
    TEST_ASSERT_EQUAL_STRING("HELLOWORLD", gHeaderLastBody.c_str());
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_redirect_same_host_get);
    RUN_TEST(test_redirect_cross_host_preserve_method_strip_auth);
    RUN_TEST(test_redirect_too_many_hops);
    RUN_TEST(test_redirect_to_https_not_supported);
    RUN_TEST(test_header_limit_triggers_error);
    RUN_TEST(test_header_limit_allows_body_bytes_after_headers);
    UNITY_END();
}

void loop() {
    // not used
}
