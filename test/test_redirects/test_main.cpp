#include <Arduino.h>
#include <unity.h>
#include <cstring>

#define private public
#include "AsyncHttpClient.h"
#include "CookieJar.h"
#include "RedirectHandler.h"
#undef private

static AsyncHttpClient::RequestContext* makeRedirectContext(HttpMethod method, const char* url) {
    auto ctx = new AsyncHttpClient::RequestContext();
    ctx->request.reset(new AsyncHttpRequest(method, url));
    ctx->response = std::make_shared<AsyncHttpResponse>();
    ctx->transport = nullptr;
    ctx->headersComplete = true;
    return ctx;
}

static void cleanupContext(AsyncHttpClient::RequestContext* ctx) {
    if (!ctx)
        return;
    ctx->request.reset();
    ctx->response.reset();
    delete ctx;
}

static void test_redirect_same_host_get() {
    AsyncHttpClient client;
    client.setFollowRedirects(true, 3);
    auto ctx = makeRedirectContext(HTTP_METHOD_POST, "http://example.com/path");
    ctx->request->setHeader("Authorization", "Bearer token");
    ctx->request->setHeader("Content-Type", "text/plain");
    ctx->request->setBody("payload");

    ctx->response->setStatusCode(302);
    ctx->response->setHeader("Location", "/next");

    std::unique_ptr<AsyncHttpRequest> newReq;
    HttpClientError err = CONNECTION_FAILED;
    String message;
    bool decision = client._redirectHandler->buildRedirectRequest(ctx, &newReq, &err, &message);

    TEST_ASSERT_TRUE(decision);
    TEST_ASSERT_NOT_NULL(newReq.get());
    TEST_ASSERT_EQUAL(HTTP_METHOD_GET, newReq->getMethod());
    TEST_ASSERT_TRUE(newReq->getBody().isEmpty());
    TEST_ASSERT_EQUAL_STRING("Bearer token", newReq->getHeader("Authorization").c_str());
    TEST_ASSERT_TRUE(newReq->getHeader("Content-Type").isEmpty());

    cleanupContext(ctx);
}

static void test_redirect_cross_host_preserve_method_strip_auth() {
    AsyncHttpClient client;
    client.setFollowRedirects(true, 3);
    auto ctx = makeRedirectContext(HTTP_METHOD_POST, "http://example.com/login");
    ctx->request->setHeader("Authorization", "Bearer token");
    ctx->request->setHeader("Proxy-Authorization", "Basic abc");
    ctx->request->setHeader("Content-Type", "application/json");
    ctx->request->setBody("{\"name\":\"demo\"}");

    ctx->response->setStatusCode(307);
    ctx->response->setHeader("Location", "http://other.example.com/session");

    std::unique_ptr<AsyncHttpRequest> newReq;
    HttpClientError err = CONNECTION_FAILED;
    String message;
    bool decision = client._redirectHandler->buildRedirectRequest(ctx, &newReq, &err, &message);

    TEST_ASSERT_TRUE(decision);
    TEST_ASSERT_NOT_NULL(newReq.get());
    TEST_ASSERT_EQUAL(HTTP_METHOD_POST, newReq->getMethod());
    TEST_ASSERT_EQUAL_STRING("{\"name\":\"demo\"}", newReq->getBody().c_str());
    TEST_ASSERT_TRUE(newReq->getHeader("Authorization").isEmpty());
    TEST_ASSERT_TRUE(newReq->getHeader("Proxy-Authorization").isEmpty());
    TEST_ASSERT_EQUAL_STRING("application/json", newReq->getHeader("Content-Type").c_str());

    cleanupContext(ctx);
}

static void test_redirect_cross_host_drops_unknown_headers_by_default() {
    AsyncHttpClient client;
    client.setFollowRedirects(true, 3);
    auto ctx = makeRedirectContext(HTTP_METHOD_GET, "http://example.com/a");
    ctx->request->setHeader("X-Custom-Token", "secret");
    ctx->request->setHeader("Accept", "application/json");

    ctx->response->setStatusCode(302);
    ctx->response->setHeader("Location", "http://other.example.com/b");

    std::unique_ptr<AsyncHttpRequest> newReq;
    HttpClientError err = CONNECTION_FAILED;
    String message;
    bool decision = client._redirectHandler->buildRedirectRequest(ctx, &newReq, &err, &message);

    TEST_ASSERT_TRUE(decision);
    TEST_ASSERT_NOT_NULL(newReq.get());
    TEST_ASSERT_TRUE(newReq->getHeader("X-Custom-Token").isEmpty());
    TEST_ASSERT_EQUAL_STRING("application/json", newReq->getHeader("Accept").c_str());

    cleanupContext(ctx);
}

static void test_redirect_cross_host_can_allowlist_header() {
    AsyncHttpClient client;
    client.setFollowRedirects(true, 3);
    client.addRedirectSafeHeader("X-Custom-Token");
    auto ctx = makeRedirectContext(HTTP_METHOD_GET, "http://example.com/a");
    ctx->request->setHeader("X-Custom-Token", "secret");

    ctx->response->setStatusCode(302);
    ctx->response->setHeader("Location", "http://other.example.com/b");

    std::unique_ptr<AsyncHttpRequest> newReq;
    HttpClientError err = CONNECTION_FAILED;
    String message;
    bool decision = client._redirectHandler->buildRedirectRequest(ctx, &newReq, &err, &message);

    TEST_ASSERT_TRUE(decision);
    TEST_ASSERT_NOT_NULL(newReq.get());
    TEST_ASSERT_EQUAL_STRING("secret", newReq->getHeader("X-Custom-Token").c_str());

    cleanupContext(ctx);
}

static void test_redirect_too_many_hops() {
    AsyncHttpClient client;
    client.setFollowRedirects(true, 2);
    auto ctx = makeRedirectContext(HTTP_METHOD_GET, "http://example.com/a");
    ctx->redirect.redirectCount = 2;
    ctx->response->setStatusCode(302);
    ctx->response->setHeader("Location", "/b");

    std::unique_ptr<AsyncHttpRequest> newReq;
    HttpClientError err = CONNECTION_FAILED;
    String message;
    bool decision = client._redirectHandler->buildRedirectRequest(ctx, &newReq, &err, &message);

    TEST_ASSERT_TRUE(decision);
    TEST_ASSERT_NULL(newReq.get());
    TEST_ASSERT_EQUAL(TOO_MANY_REDIRECTS, err);
    TEST_ASSERT_FALSE(message.isEmpty());

    cleanupContext(ctx);
}

static void test_redirect_to_https_supported() {
    AsyncHttpClient client;
    client.setFollowRedirects(true, 3);
    auto ctx = makeRedirectContext(HTTP_METHOD_GET, "http://example.com/path");
    ctx->response->setStatusCode(301);
    ctx->response->setHeader("Location", "https://secure.example.com/next");

    std::unique_ptr<AsyncHttpRequest> newReq;
    HttpClientError err = CONNECTION_FAILED;
    String message;
    bool decision = client._redirectHandler->buildRedirectRequest(ctx, &newReq, &err, &message);

    TEST_ASSERT_TRUE(decision);
    TEST_ASSERT_NOT_NULL(newReq.get());
    TEST_ASSERT_TRUE(newReq->isSecure());
    TEST_ASSERT_EQUAL_STRING("secure.example.com", newReq->getHost().c_str());
    TEST_ASSERT_EQUAL(443, newReq->getPort());
    TEST_ASSERT_TRUE(message.isEmpty());

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
    ctx->request.reset(new AsyncHttpRequest(HTTP_METHOD_GET, "http://example.com/"));
    ctx->response = std::make_shared<AsyncHttpResponse>();
    ctx->onError = [](HttpClientError error, const char* message) {
        (void)message;
        gHeaderErrorCalled = true;
        gHeaderLastError = error;
    };

    const char* partialHeaders = "HTTP/1.1 200 OK\r\nX-Very-Long-Header: 0123456789012345678901234567890123456789";
    client.handleData(ctx, const_cast<char*>(partialHeaders), strlen(partialHeaders));

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
    ctx->request.reset(new AsyncHttpRequest(HTTP_METHOD_GET, "http://example.com/"));
    ctx->response = std::make_shared<AsyncHttpResponse>();
    ctx->onError = [](HttpClientError error, const char* message) {
        (void)message;
        gHeaderErrorCalled = true;
        gHeaderLastError = error;
    };
    ctx->onSuccess = [](const std::shared_ptr<AsyncHttpResponse>& resp) {
        gHeaderSuccessCalled = true;
        gHeaderLastBody = resp->getBody();
    };

    const char* frame = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nHELLOWORLD";
    client.handleData(ctx, const_cast<char*>(frame), strlen(frame));

    TEST_ASSERT_FALSE(gHeaderErrorCalled);
    TEST_ASSERT_TRUE(gHeaderSuccessCalled);
    TEST_ASSERT_EQUAL_STRING("HELLOWORLD", gHeaderLastBody.c_str());
}

static void test_cookie_roundtrip_basic() {
    AsyncHttpClient client;
    auto ctx = new AsyncHttpClient::RequestContext();
    ctx->request.reset(new AsyncHttpRequest(HTTP_METHOD_GET, "http://example.com/login"));
    ctx->response = std::make_shared<AsyncHttpResponse>();

    String frame = "HTTP/1.1 200 OK\r\nSet-Cookie: session=abc123; Path=/\r\nContent-Length: 0\r\n\r\n";
    TEST_ASSERT_TRUE(client.parseResponseHeaders(ctx, frame));

    AsyncHttpRequest follow(HTTP_METHOD_GET, "http://example.com/home");
    client._cookieJar->applyCookies(&follow);
    TEST_ASSERT_EQUAL_STRING("session=abc123", follow.getHeader("Cookie").c_str());

    cleanupContext(ctx);
}

static void test_cookie_path_and_secure_rules() {
    AsyncHttpClient client;
    auto ctx = new AsyncHttpClient::RequestContext();
    ctx->request.reset(new AsyncHttpRequest(HTTP_METHOD_GET, "http://example.com/login"));
    ctx->response = std::make_shared<AsyncHttpResponse>();

    String frame = "HTTP/1.1 200 OK\r\nSet-Cookie: admin=1; Path=/admin; Secure\r\nContent-Length: 0\r\n\r\n";
    TEST_ASSERT_TRUE(client.parseResponseHeaders(ctx, frame));

    AsyncHttpRequest wrongPath(HTTP_METHOD_GET, "http://example.com/public");
    client._cookieJar->applyCookies(&wrongPath);
    TEST_ASSERT_TRUE(wrongPath.getHeader("Cookie").isEmpty());

    AsyncHttpRequest insecureTarget(HTTP_METHOD_GET, "http://example.com/admin/dashboard");
    client._cookieJar->applyCookies(&insecureTarget);
    TEST_ASSERT_TRUE(insecureTarget.getHeader("Cookie").isEmpty());

    AsyncHttpRequest secureTarget(HTTP_METHOD_GET, "https://example.com/admin/dashboard");
    client._cookieJar->applyCookies(&secureTarget);
    TEST_ASSERT_EQUAL_STRING("admin=1", secureTarget.getHeader("Cookie").c_str());

    cleanupContext(ctx);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_redirect_same_host_get);
    RUN_TEST(test_redirect_cross_host_preserve_method_strip_auth);
    RUN_TEST(test_redirect_cross_host_drops_unknown_headers_by_default);
    RUN_TEST(test_redirect_cross_host_can_allowlist_header);
    RUN_TEST(test_redirect_too_many_hops);
    RUN_TEST(test_redirect_to_https_supported);
    RUN_TEST(test_header_limit_triggers_error);
    RUN_TEST(test_header_limit_allows_body_bytes_after_headers);
    RUN_TEST(test_cookie_roundtrip_basic);
    RUN_TEST(test_cookie_path_and_secure_rules);
    UNITY_END();
}

void loop() {
    // not used
}
