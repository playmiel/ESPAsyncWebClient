#include <Arduino.h>
#include <unity.h>

#define private public
#include "AsyncHttpClient.h"
#undef private

class MockTransport : public AsyncTransport {
  public:
    explicit MockTransport(bool secure = false) : _secure(secure) {}

    void setConnectHandler(ConnectHandler handler, void* arg) override {
        (void)handler;
        (void)arg;
    }
    void setDataHandler(DataHandler handler, void* arg) override {
        (void)handler;
        (void)arg;
    }
    void setDisconnectHandler(DisconnectHandler handler, void* arg) override {
        (void)handler;
        (void)arg;
    }
    void setErrorHandler(ErrorHandler handler, void* arg) override {
        (void)handler;
        (void)arg;
    }
    void setTimeout(uint32_t timeoutMs) override {
        (void)timeoutMs;
    }
    void setTimeoutHandler(TimeoutHandler handler, void* arg) override {
        (void)handler;
        (void)arg;
    }
    bool connect(const char* host, uint16_t port) override {
        (void)host;
        (void)port;
        return true;
    }
    size_t write(const char* data, size_t len) override {
        lastWrite = String(data).substring(0, len);
        return len;
    }
    bool canSend() const override {
        return !_closed;
    }
    void close(bool now = false) override {
        (void)now;
        _closed = true;
    }
    bool isSecure() const override {
        return _secure;
    }
    bool isHandshaking() const override {
        return false;
    }
    uint32_t getHandshakeStartMs() const override {
        return 0;
    }
    uint32_t getHandshakeTimeoutMs() const override {
        return 0;
    }

    bool closed() const {
        return _closed;
    }

    String lastWrite;

  private:
    bool _secure;
    bool _closed = false;
};

static void test_pools_connection_on_complete_body() {
    AsyncHttpClient client;
    client.setKeepAlive(true, 3000);

    auto ctx = new AsyncHttpClient::RequestContext();
    ctx->request = new AsyncHttpRequest(HTTP_GET, "http://example.com/");
    ctx->requestKeepAlive = true;
    ctx->resolvedTlsConfig = client.getDefaultTlsConfig();
    ctx->transport = new MockTransport(false);
    ctx->response = new AsyncHttpResponse();

    String headers = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: keep-alive\r\n\r\n";
    TEST_ASSERT_TRUE(client.parseResponseHeaders(ctx, headers));
    ctx->headersComplete = true;
    client.processResponse(ctx);

    TEST_ASSERT_EQUAL(1, (int)client._idleConnections.size());
    TEST_ASSERT_EQUAL_STRING("example.com", client._idleConnections[0].host.c_str());
    TEST_ASSERT_EQUAL(80, client._idleConnections[0].port);
}

static bool gErrorCalled = false;
static HttpClientError gLastError = CONNECTION_FAILED;

static void test_does_not_pool_on_truncated_body() {
    gErrorCalled = false;
    gLastError = CONNECTION_FAILED;
    AsyncHttpClient client;
    client.setKeepAlive(true, 3000);

    auto ctx = new AsyncHttpClient::RequestContext();
    ctx->request = new AsyncHttpRequest(HTTP_GET, "http://example.com/");
    ctx->requestKeepAlive = true;
    ctx->resolvedTlsConfig = client.getDefaultTlsConfig();
    ctx->transport = new MockTransport(false);
    ctx->response = new AsyncHttpResponse();
    ctx->onError = [](HttpClientError err, const char* msg) {
        (void)msg;
        gErrorCalled = true;
        gLastError = err;
    };

    String headers = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: keep-alive\r\n\r\n";
    TEST_ASSERT_TRUE(client.parseResponseHeaders(ctx, headers));
    ctx->headersComplete = true;
    ctx->receivedContentLength = 3; // truncated
    client.handleDisconnect(ctx);

    TEST_ASSERT_TRUE(gErrorCalled);
    TEST_ASSERT_EQUAL(CONNECTION_CLOSED_MID_BODY, gLastError);
    TEST_ASSERT_EQUAL(0, (int)client._idleConnections.size());
}

static void test_reuses_pooled_connection() {
    AsyncHttpClient client;
    client.setKeepAlive(true, 4000);

    // Seed pool with one connection
    auto poolCtx = new AsyncHttpClient::RequestContext();
    poolCtx->request = new AsyncHttpRequest(HTTP_GET, "http://example.com/");
    poolCtx->requestKeepAlive = true;
    poolCtx->resolvedTlsConfig = client.getDefaultTlsConfig();
    poolCtx->transport = new MockTransport(false);
    poolCtx->response = new AsyncHttpResponse();
    poolCtx->headersComplete = true;
    poolCtx->responseProcessed = true;
    poolCtx->response->setStatusCode(200);
    client.cleanup(poolCtx);

    TEST_ASSERT_EQUAL(1, (int)client._idleConnections.size());
    MockTransport* pooled = static_cast<MockTransport*>(client._idleConnections[0].transport);

    auto ctx = new AsyncHttpClient::RequestContext();
    ctx->request = new AsyncHttpRequest(HTTP_GET, "http://example.com/");
    ctx->request->setHeader("Connection", "keep-alive");
    ctx->response = new AsyncHttpResponse();
    ctx->onSuccess = [](AsyncHttpResponse* resp) {
        TEST_ASSERT_EQUAL(200, resp->getStatusCode());
    };

    client.executeRequest(ctx);
    TEST_ASSERT_TRUE(ctx->usingPooledConnection);
    TEST_ASSERT_EQUAL_PTR(pooled, ctx->transport);

    const char* frame = "HTTP/1.1 200 OK\r\nContent-Length: 4\r\nConnection: keep-alive\r\n\r\nTEST";
    client.handleData(ctx, const_cast<char*>(frame), strlen(frame));

    TEST_ASSERT_EQUAL(1, (int)client._idleConnections.size());
    TEST_ASSERT_FALSE(pooled->closed());
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_pools_connection_on_complete_body);
    RUN_TEST(test_does_not_pool_on_truncated_body);
    RUN_TEST(test_reuses_pooled_connection);
    return UNITY_END();
}

void setup() {
    delay(200);
    runUnityTests();
}

void loop() {}
