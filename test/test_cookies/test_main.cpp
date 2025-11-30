#include <Arduino.h>
#include <unity.h>

#define private public
#include "AsyncHttpClient.h"
#undef private

static void test_domain_matching_subdomains() {
    AsyncHttpClient client;
    TEST_ASSERT_TRUE(client.domainMatches("example.com", "sub.example.com"));
    TEST_ASSERT_TRUE(client.domainMatches("example.com", "example.com"));
    TEST_ASSERT_FALSE(client.domainMatches("example.com", "badexample.com"));
    TEST_ASSERT_FALSE(client.domainMatches("example.com", "com"));
}

static void test_multiple_cookies_and_deduplication() {
    AsyncHttpClient client;
    AsyncHttpRequest req(HTTP_GET, "http://example.com/path");

    client.storeResponseCookie(&req, "a=1; Path=/");
    client.storeResponseCookie(&req, "b=2; Path=/");
    client.storeResponseCookie(&req, "a=3; Path=/");

    client.applyCookies(&req);
    String header = req.getHeader("Cookie");
    TEST_ASSERT_FALSE(header.isEmpty());
    TEST_ASSERT_NOT_EQUAL(-1, header.indexOf("a=3"));
    TEST_ASSERT_NOT_EQUAL(-1, header.indexOf("b=2"));
    // Should only contain two cookies
    int separators = 0;
    for (size_t i = 0; i < header.length(); ++i) {
        if (header.charAt(i) == ';')
            separators++;
    }
    TEST_ASSERT_EQUAL(1, separators);
}

static void test_max_age_removes_cookie() {
    AsyncHttpClient client;
    AsyncHttpRequest req(HTTP_GET, "http://example.com/");
    client.storeResponseCookie(&req, "temp=1; Path=/");
    client.storeResponseCookie(&req, "temp=0; Max-Age=0; Path=/");

    client.applyCookies(&req);
    String header = req.getHeader("Cookie");
    TEST_ASSERT_TRUE(header.isEmpty());
    TEST_ASSERT_EQUAL(0, (int)client._cookies.size());
}

static void test_clear_and_public_set_cookie_api() {
    AsyncHttpClient client;
    AsyncHttpRequest req(HTTP_GET, "http://example.com/");

    client.setCookie("manual", "123", "/", "example.com", false);
    client.applyCookies(&req);
    TEST_ASSERT_EQUAL_STRING("manual=123", req.getHeader("Cookie").c_str());

    client.clearCookies();
    AsyncHttpRequest req2(HTTP_GET, "http://example.com/");
    client.applyCookies(&req2);
    TEST_ASSERT_TRUE(req2.getHeader("Cookie").isEmpty());
}

static void test_rejects_mismatched_domain_attribute() {
    AsyncHttpClient client;
    AsyncHttpRequest req(HTTP_GET, "http://example.com/");

    client.storeResponseCookie(&req, "evil=1; Domain=evil.com; Path=/");
    client.applyCookies(&req);
    TEST_ASSERT_TRUE(req.getHeader("Cookie").isEmpty());
    TEST_ASSERT_EQUAL(0, (int)client._cookies.size());
}

static void test_cookie_path_matching_rfc6265_rule() {
    AsyncHttpClient client;
    AsyncHttpRequest req(HTTP_GET, "http://example.com/administrator");
    client.storeResponseCookie(&req, "adminonly=1; Path=/admin");

    client.applyCookies(&req);
    TEST_ASSERT_TRUE(req.getHeader("Cookie").isEmpty());

    AsyncHttpRequest req2(HTTP_GET, "http://example.com/admin/settings");
    client.applyCookies(&req2);
    TEST_ASSERT_EQUAL_STRING("adminonly=1", req2.getHeader("Cookie").c_str());
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_domain_matching_subdomains);
    RUN_TEST(test_multiple_cookies_and_deduplication);
    RUN_TEST(test_max_age_removes_cookie);
    RUN_TEST(test_clear_and_public_set_cookie_api);
    RUN_TEST(test_rejects_mismatched_domain_attribute);
    RUN_TEST(test_cookie_path_matching_rfc6265_rule);
    return UNITY_END();
}

void setup() {
    delay(200);
    runUnityTests();
}

void loop() {}
