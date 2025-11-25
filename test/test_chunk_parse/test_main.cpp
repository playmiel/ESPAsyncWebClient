#include <Arduino.h>
#include <unity.h>
#include <vector>
#include <cstring>

#define private public
#include "AsyncHttpClient.h"
#undef private

static bool gSuccessCalled = false;
static bool gErrorCalled = false;
static HttpClientError gLastError = CONNECTION_FAILED;
static String gLastBody;
static std::vector<HttpHeader> gLastTrailers;

static void resetState() {
    gSuccessCalled = false;
    gErrorCalled = false;
    gLastError = CONNECTION_FAILED;
    gLastBody = "";
    gLastTrailers.clear();
}

static String trailerValue(const char* name) {
    for (const auto& trailer : gLastTrailers) {
        if (trailer.name.equalsIgnoreCase(name)) {
            return trailer.value;
        }
    }
    return String();
}

static AsyncHttpClient::RequestContext* makeContext(AsyncHttpClient& client) {
    auto ctx = new AsyncHttpClient::RequestContext();
    ctx->request = new AsyncHttpRequest(HTTP_GET, "http://example.com/res");
    ctx->response = new AsyncHttpResponse();
    ctx->transport = nullptr;
    ctx->onSuccess = [](AsyncHttpResponse* resp) {
        gSuccessCalled = true;
        gLastBody = resp->getBody();
        gLastTrailers = resp->getTrailers();
    };
    ctx->onError = [](HttpClientError error, const char* message) {
        (void)message;
        gErrorCalled = true;
        gLastError = error;
    };
    return ctx;
}

static void test_chunk_trailers_are_parsed() {
    resetState();
    AsyncHttpClient client;
    auto ctx = makeContext(client);

    ctx->headersComplete = true;
    ctx->chunked = true;

    auto feed = [&](const char* data) { client.handleData(ctx, const_cast<char*>(data), strlen(data)); };

    feed("4\r\n");
    feed("Wiki\r\n");
    feed("5\r\n");
    feed("pedia\r\n");
    feed("0\r\n");
    feed("X-Checksum: abc123\r\n");
    feed("X-Meta: done\r\n");
    feed("\r\n");

    client.handleDisconnect(ctx);

    TEST_ASSERT_TRUE(gSuccessCalled);
    TEST_ASSERT_FALSE(gErrorCalled);
    TEST_ASSERT_EQUAL_STRING("Wikipedia", gLastBody.c_str());
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(gLastTrailers.size()));
    TEST_ASSERT_EQUAL_STRING("abc123", trailerValue("X-Checksum").c_str());
    TEST_ASSERT_EQUAL_STRING("done", trailerValue("X-Meta").c_str());
}

static void test_chunk_missing_crlf_is_error() {
    resetState();
    AsyncHttpClient client;
    auto ctx = makeContext(client);

    ctx->headersComplete = true;
    ctx->chunked = true;

    auto feed = [&](const char* data) { client.handleData(ctx, const_cast<char*>(data), strlen(data)); };

    feed("4\r\n");
    feed("Wiki\n"); // missing CR before LF terminator

    client.handleDisconnect(ctx);

    TEST_ASSERT_TRUE(gErrorCalled);
    TEST_ASSERT_FALSE(gSuccessCalled);
    TEST_ASSERT_EQUAL_INT(CHUNKED_DECODE_FAILED, gLastError);
}

static void test_chunk_body_limit_enforced() {
    resetState();
    AsyncHttpClient client;
    client.setMaxBodySize(5);
    auto ctx = makeContext(client);

    String payload = "HTTP/1.1 200 OK\r\n"
                     "Transfer-Encoding: chunked\r\n"
                     "\r\n"
                     "6\r\nTooBig\r\n"
                     "0\r\n\r\n";

    client.handleData(ctx, const_cast<char*>(payload.c_str()), payload.length());

    TEST_ASSERT_TRUE(gErrorCalled);
    TEST_ASSERT_FALSE(gSuccessCalled);
    TEST_ASSERT_EQUAL_INT(MAX_BODY_SIZE_EXCEEDED, gLastError);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_chunk_trailers_are_parsed);
    RUN_TEST(test_chunk_missing_crlf_is_error);
    RUN_TEST(test_chunk_body_limit_enforced);
    UNITY_END();
}

void loop() {
    // not used
}
