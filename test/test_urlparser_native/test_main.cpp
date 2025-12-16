#include <unity.h>

#include <string>
#include <vector>

#include "UrlParser.h"

#include "../../url_test_cases.h"

static void test_parse_url_shared_cases() {
#define X(url, expHost, expPath, expPort, expSecure, expImplicit)                                                      \
    do {                                                                                                               \
        UrlParser::ParsedUrl parsed;                                                                                   \
        TEST_ASSERT_TRUE_MESSAGE(UrlParser::parse(url, parsed), url);                                                  \
        TEST_ASSERT_EQUAL_STRING(expHost, parsed.host.c_str());                                                        \
        TEST_ASSERT_EQUAL_STRING(expPath, parsed.path.c_str());                                                        \
        TEST_ASSERT_EQUAL_UINT16(expPort, parsed.port);                                                                \
        TEST_ASSERT_EQUAL(expSecure, parsed.secure);                                                                   \
        TEST_ASSERT_EQUAL(expImplicit, parsed.schemeImplicit);                                                         \
    } while (0);
    URL_TEST_CASES
#undef X
}

static void test_rejects_urls_with_control_chars_and_whitespace() {
    struct Case {
        const char* name;
        std::string url;
    };

    const std::vector<Case> cases = {
        {"space", "http://example.com/pa th"},
        {"tab", std::string("http://example.com/") + std::string(1, '\t')},
        {"newline", std::string("http://example.com/") + std::string(1, '\n')},
        {"carriage_return", std::string("http://example.com/") + std::string(1, '\r')},
        {"vertical_tab", std::string("http://example.com/") + std::string(1, '\v')},
        {"form_feed", std::string("http://example.com/") + std::string(1, '\f')},
        {"esc", std::string("http://example.com/") + std::string(1, static_cast<char>(0x1B))},
        {"del", std::string("http://example.com/") + std::string(1, static_cast<char>(0x7F))},
        {"nul", std::string("http://exam") + std::string(1, '\0') + "ple.com/"},
    };

    for (const auto& tc : cases) {
        UrlParser::ParsedUrl parsed;
        TEST_ASSERT_FALSE_MESSAGE(UrlParser::parse(tc.url, parsed), tc.name);
    }
}

static void test_rejects_hosts_with_invalid_characters() {
    const std::vector<std::string> urls = {
        "http://exa_mple.com/",
        "http://example!.com/",
        "http://examp[le.com/",
        "http://example.com@evil.com/",
        "http://.example.com/",
        "http://example.com./",
        "http:///",
        "http://:80/",
    };

    for (const auto& url : urls) {
        UrlParser::ParsedUrl parsed;
        TEST_ASSERT_FALSE_MESSAGE(UrlParser::parse(url, parsed), url.c_str());
    }

    const std::string longHost(256, 'a');
    {
        UrlParser::ParsedUrl parsed;
        TEST_ASSERT_FALSE(UrlParser::parse(std::string("http://") + longHost + "/", parsed));
    }
}

static void test_port_boundaries_and_invalid_ports() {
    {
        UrlParser::ParsedUrl parsed;
        TEST_ASSERT_TRUE(UrlParser::parse("http://example.com:0/", parsed));
        TEST_ASSERT_EQUAL_UINT16(0, parsed.port);
    }
    {
        UrlParser::ParsedUrl parsed;
        TEST_ASSERT_TRUE(UrlParser::parse("http://example.com:65535/", parsed));
        TEST_ASSERT_EQUAL_UINT16(65535, parsed.port);
    }
    {
        UrlParser::ParsedUrl parsed;
        TEST_ASSERT_FALSE(UrlParser::parse("http://example.com:65536/", parsed));
    }

    const std::vector<std::string> invalidPorts = {
        "http://example.com:/",
        "http://example.com:",
        "http://example.com:?q=1",
        "http://example.com:-1/",
        "http://example.com:+1/",
        "http://example.com:12a3/",
        "http://example.com:18446744073709551616/",
        "http://example.com:9999999999999999999999999999999999999999/",
        "http://example.com::80/",
    };

    for (const auto& url : invalidPorts) {
        UrlParser::ParsedUrl parsed;
        TEST_ASSERT_FALSE_MESSAGE(UrlParser::parse(url, parsed), url.c_str());
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_parse_url_shared_cases);
    RUN_TEST(test_rejects_urls_with_control_chars_and_whitespace);
    RUN_TEST(test_rejects_hosts_with_invalid_characters);
    RUN_TEST(test_port_boundaries_and_invalid_ports);
    return UNITY_END();
}
