#ifndef URL_TEST_CASES_H
#define URL_TEST_CASES_H
// Shared URL test cases between Python and C++ tests.
// Format macro: X(url, host, path, port, secureBool, schemeImplicit)
#define URL_TEST_CASES                                                                                                 \
    X("http://example.com?foo=bar", "example.com", "/?foo=bar", 80, false, false)                                      \
    X("https://example.com/path?foo=bar", "example.com", "/path?foo=bar", 443, true, false)                            \
    X("http://example.com", "example.com", "/", 80, false, false)                                                      \
    X("http://example.com:8080/api", "example.com", "/api", 8080, false, false)                                        \
    X("https://example.com:4443/", "example.com", "/", 4443, true, false)                                              \
    X("example.com", "example.com", "/", 443, true, true)                                                              \
    X("example.com?x=1", "example.com", "/?x=1", 443, true, true)
#endif // URL_TEST_CASES_H
