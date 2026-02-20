#ifndef COOKIE_JAR_H
#define COOKIE_JAR_H

#include <Arduino.h>
#include <vector>
#include "HttpRequest.h"

class AsyncHttpClient;

class CookieJar {
  public:
    explicit CookieJar(AsyncHttpClient* client);

    void clearCookies();
    void setAllowCookieDomainAttribute(bool enable);
    void addAllowedCookieDomain(const char* domain);
    void clearAllowedCookieDomains();
    void setCookie(const char* name, const char* value, const char* path, const char* domain, bool secure);

    void applyCookies(AsyncHttpRequest* request);
    void storeResponseCookie(const AsyncHttpRequest* request, const String& setCookieValue);

  private:
    struct StoredCookie {
        String name;
        String value;
        String domain;
        String path;
        bool hostOnly = true;
        bool secure = false;
        int64_t expiresAt = -1; // -1 means no expiration set
        int64_t createdAt = 0;
        int64_t lastAccessAt = 0;
    };

    void lock() const;
    void unlock() const;
    bool cookieMatchesRequest(const StoredCookie& cookie, const AsyncHttpRequest* request, int64_t nowSeconds) const;
    bool isCookieExpired(const StoredCookie& cookie, int64_t nowSeconds) const;
    void purgeExpiredCookies(int64_t nowSeconds);
    void evictOneCookieLocked();
    bool domainMatches(const String& cookieDomain, const String& host) const;
    bool pathMatches(const String& cookiePath, const String& requestPath) const;
    bool normalizeCookieDomain(String& domain, const String& host, bool domainAttributeProvided,
                               bool* outHostOnly) const;
    bool isIpLiteral(const String& host) const;

    AsyncHttpClient* _client = nullptr;
    bool _allowCookieDomainAttribute = false;
    std::vector<String> _allowedCookieDomains;
    std::vector<StoredCookie> _cookies;
};

#endif // COOKIE_JAR_H
