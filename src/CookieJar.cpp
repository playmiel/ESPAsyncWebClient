#include "CookieJar.h"
#include <cctype>
#include <cstring>
#include "AsyncHttpClient.h"
#include "HttpCommon.h"
#include "HttpHelpers.h"

static constexpr size_t kMaxCookieCount = 16;
static constexpr size_t kMaxCookieBytes = 4096;

static uint8_t countDomainDots(const String& domain) {
    uint8_t dots = 0;
    for (size_t i = 0; i < domain.length(); ++i) {
        if (domain.charAt(i) == '.')
            ++dots;
    }
    return dots;
}

CookieJar::CookieJar(AsyncHttpClient* client) : _client(client) {}

void CookieJar::lock() const {
    if (_client)
        _client->lock();
}

void CookieJar::unlock() const {
    if (_client)
        _client->unlock();
}

void CookieJar::clearCookies() {
    lock();
    _cookies.clear();
    unlock();
}

void CookieJar::setAllowCookieDomainAttribute(bool enable) {
    lock();
    _allowCookieDomainAttribute = enable;
    unlock();
}

void CookieJar::addAllowedCookieDomain(const char* domain) {
    if (!domain || strlen(domain) == 0)
        return;
    String normalized = normalizeDomainForStorage(String(domain));
    if (normalized.length() == 0)
        return;
    if (normalized.indexOf('.') == -1)
        return;
    lock();
    for (const auto& existing : _allowedCookieDomains) {
        if (existing.equalsIgnoreCase(normalized)) {
            unlock();
            return;
        }
    }
    _allowedCookieDomains.push_back(normalized);
    unlock();
}

void CookieJar::clearAllowedCookieDomains() {
    lock();
    _allowedCookieDomains.clear();
    unlock();
}

void CookieJar::setCookie(const char* name, const char* value, const char* path, const char* domain, bool secure) {
    if (!name || strlen(name) == 0)
        return;
    if (!isValidHttpHeaderValue(String(name)))
        return;
    if (strchr(name, '=') || strchr(name, ';'))
        return;
    if (value && !isValidHttpHeaderValue(String(value)))
        return;
    int64_t now = currentTimeSeconds();
    StoredCookie cookie;
    cookie.name = String(name);
    cookie.value = value ? String(value) : String();
    cookie.path = (path && strlen(path) > 0) ? String(path) : String("/");
    cookie.domain = domain ? String(domain) : String();
    cookie.hostOnly = false; // Manual cookies are treated as domain cookies (domain=="" means "any host").
    cookie.secure = secure;
    cookie.createdAt = now;
    cookie.lastAccessAt = now;
    if (!cookie.path.startsWith("/"))
        cookie.path = "/" + cookie.path;
    if (cookie.domain.startsWith("."))
        cookie.domain.remove(0, 1);

    lock();
    purgeExpiredCookies(now);
    for (auto it = _cookies.begin(); it != _cookies.end();) {
        if (it->name.equalsIgnoreCase(cookie.name) && it->domain.equalsIgnoreCase(cookie.domain) &&
            it->path.equals(cookie.path)) {
            it = _cookies.erase(it);
        } else {
            ++it;
        }
    }
    if (!cookie.value.isEmpty()) {
        if (_cookies.size() >= kMaxCookieCount)
            evictOneCookieLocked();
        _cookies.push_back(cookie);
    }
    unlock();
}

bool CookieJar::isIpLiteral(const String& host) const {
    if (host.length() == 0)
        return false;
    bool hasColon = false;
    bool hasDot = false;
    for (size_t i = 0; i < host.length(); ++i) {
        char c = host.charAt(i);
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            continue;
        }
        if (c == '.') {
            hasDot = true;
            continue;
        }
        if (c == ':') {
            hasColon = true;
            continue;
        }
        return false;
    }
    return hasColon || hasDot;
}

bool CookieJar::normalizeCookieDomain(String& domain, const String& host, bool domainAttributeProvided,
                                      bool* outHostOnly) const {
    if (outHostOnly)
        *outHostOnly = true;
    String hostLower = host;
    hostLower.toLowerCase();
    String cleaned = normalizeDomainForStorage(domain);

    // No Domain= attribute (or empty) => host-only cookie.
    if (!domainAttributeProvided || cleaned.length() == 0) {
        domain = hostLower;
        if (outHostOnly)
            *outHostOnly = true;
        return true;
    }

    // Reject Domain= on IP literals and unrelated domains.
    if (isIpLiteral(hostLower))
        return false;
    if (!domainMatches(cleaned, hostLower))
        return false;

    // Public suffix and "TLD-like" Domain= attributes are ignored (stored as host-only instead).
    // This avoids broad cookie scope even when the embedded public-suffix list is incomplete.
    if (hostLower.indexOf('.') == -1 || cleaned.indexOf('.') == -1) {
        domain = hostLower;
        if (outHostOnly)
            *outHostOnly = true;
        return true;
    }

    bool allowDomainCookie = false;
    lock();
    if (_allowCookieDomainAttribute) {
        for (const auto& allowedDomain : _allowedCookieDomains) {
            if (allowedDomain.equalsIgnoreCase(cleaned)) {
                allowDomainCookie = true;
                break;
            }
        }
    }
    unlock();

    if (allowDomainCookie) {
        domain = cleaned;
        if (outHostOnly)
            *outHostOnly = false;
    } else {
        domain = hostLower;
        if (outHostOnly)
            *outHostOnly = true;
    }
    return true;
}

bool CookieJar::domainMatches(const String& cookieDomain, const String& host) const {
    if (cookieDomain.length() == 0)
        return true;
    if (host.equalsIgnoreCase(cookieDomain))
        return true;
    if (host.length() <= cookieDomain.length())
        return false;
    size_t offset = host.length() - cookieDomain.length();
    if (host.charAt(offset - 1) != '.')
        return false;
    return host.substring(offset).equalsIgnoreCase(cookieDomain);
}

bool CookieJar::pathMatches(const String& cookiePath, const String& requestPath) const {
    String req = requestPath;
    int q = req.indexOf('?');
    if (q != -1)
        req = req.substring(0, q);
    if (!req.startsWith("/"))
        req = "/" + req;
    String cpath = cookiePath.length() > 0 ? cookiePath : "/";
    if (!cpath.startsWith("/"))
        cpath = "/" + cpath;
    if (req.equals(cpath))
        return true;
    if (!req.startsWith(cpath))
        return false;
    if (cpath.endsWith("/"))
        return true;
    return req.length() > cpath.length() && req.charAt(cpath.length()) == '/';
}

bool CookieJar::cookieMatchesRequest(const StoredCookie& cookie, const AsyncHttpRequest* request,
                                     int64_t nowSeconds) const {
    if (!request)
        return false;
    if (isCookieExpired(cookie, nowSeconds))
        return false;
    if (cookie.secure && !request->isSecure())
        return false;
    if (cookie.hostOnly) {
        if (!request->getHost().equalsIgnoreCase(cookie.domain))
            return false;
    } else {
        if (!domainMatches(cookie.domain, request->getHost()))
            return false;
    }
    if (!pathMatches(cookie.path, request->getPath()))
        return false;
    return !cookie.value.isEmpty();
}

bool CookieJar::isCookieExpired(const StoredCookie& cookie, int64_t nowSeconds) const {
    return cookie.expiresAt != -1 && nowSeconds >= cookie.expiresAt;
}

void CookieJar::purgeExpiredCookies(int64_t nowSeconds) {
    for (auto it = _cookies.begin(); it != _cookies.end();) {
        if (isCookieExpired(*it, nowSeconds)) {
            it = _cookies.erase(it);
        } else {
            ++it;
        }
    }
}

void CookieJar::evictOneCookieLocked() {
    if (_cookies.empty())
        return;
    size_t bestIndex = 0;
    for (size_t i = 1; i < _cookies.size(); ++i) {
        const StoredCookie& best = _cookies[bestIndex];
        const StoredCookie& candidate = _cookies[i];

        if (candidate.lastAccessAt != best.lastAccessAt) {
            if (candidate.lastAccessAt < best.lastAccessAt)
                bestIndex = i;
            continue;
        }

        bool candidateSession = candidate.expiresAt == -1;
        bool bestSession = best.expiresAt == -1;
        if (candidateSession != bestSession) {
            if (candidateSession)
                bestIndex = i;
            continue;
        }

        uint8_t candidateDots = countDomainDots(candidate.domain);
        uint8_t bestDots = countDomainDots(best.domain);
        if (candidateDots != bestDots) {
            if (candidateDots < bestDots)
                bestIndex = i;
            continue;
        }

        if (candidate.domain.length() != best.domain.length()) {
            if (candidate.domain.length() < best.domain.length())
                bestIndex = i;
            continue;
        }

        if (candidate.path.length() != best.path.length()) {
            if (candidate.path.length() < best.path.length())
                bestIndex = i;
            continue;
        }

        if (candidate.createdAt != best.createdAt) {
            if (candidate.createdAt < best.createdAt)
                bestIndex = i;
            continue;
        }
    }
    _cookies.erase(_cookies.begin() + static_cast<std::vector<StoredCookie>::difference_type>(bestIndex));
}

void CookieJar::applyCookies(AsyncHttpRequest* request) {
    if (!request)
        return;
    int64_t now = currentTimeSeconds();
    String cookieHeader;
    lock();
    purgeExpiredCookies(now);
    size_t estimatedLen = 0;
    for (const auto& cookie : _cookies) {
        if (cookieMatchesRequest(cookie, request, now))
            estimatedLen += cookie.name.length() + 1 + cookie.value.length() + 2;
    }
    if (estimatedLen >= 2)
        estimatedLen -= 2;
    cookieHeader.reserve(estimatedLen);
    for (auto& cookie : _cookies) {
        if (cookieMatchesRequest(cookie, request, now)) {
            cookie.lastAccessAt = now;
            if (!cookieHeader.isEmpty())
                cookieHeader += "; ";
            cookieHeader += cookie.name;
            cookieHeader += "=";
            cookieHeader += cookie.value;
        }
    }
    unlock();
    if (cookieHeader.isEmpty())
        return;
    String existing = request->getHeader("Cookie");
    if (!existing.isEmpty()) {
        if (!existing.endsWith(";"))
            existing += ";";
        existing += " ";
        existing += cookieHeader;
        request->setHeader("Cookie", existing);
    } else {
        request->setHeader("Cookie", cookieHeader);
    }
}

void CookieJar::storeResponseCookie(const AsyncHttpRequest* request, const String& setCookieValue) {
    if (!request)
        return;
    String raw = setCookieValue;
    if (raw.length() == 0)
        return;
    if (raw.length() > kMaxCookieBytes)
        return;
    int64_t now = currentTimeSeconds();
    int semi = raw.indexOf(';');
    String pair = semi == -1 ? raw : raw.substring(0, semi);
    pair.trim();
    int eq = pair.indexOf('=');
    if (eq <= 0)
        return;
    StoredCookie cookie;
    cookie.name = pair.substring(0, eq);
    cookie.value = pair.substring(eq + 1);
    cookie.name.trim();
    cookie.value.trim();
    cookie.domain = request->getHost();
    cookie.path = "/";
    bool domainAttributeProvided = false;
    bool remove = cookie.value.isEmpty();
    bool maxAgeAttributeProvided = false;
    int64_t expiresAt = -1;

    int pos = semi;
    while (pos != -1) {
        int next = raw.indexOf(';', pos + 1);
        String token = raw.substring(pos + 1, next == -1 ? raw.length() : next);
        token.trim();
        if (!token.isEmpty()) {
            int eqPos = token.indexOf('=');
            String key = eqPos == -1 ? token : token.substring(0, eqPos);
            String val = eqPos == -1 ? "" : token.substring(eqPos + 1);
            key.trim();
            val.trim();
            if (key.equalsIgnoreCase("Path")) {
                cookie.path = val.length() > 0 ? val : "/";
            } else if (key.equalsIgnoreCase("Domain")) {
                cookie.domain = val;
                domainAttributeProvided = true;
            } else if (key.equalsIgnoreCase("Secure")) {
                cookie.secure = true;
            } else if (key.equalsIgnoreCase("Max-Age")) {
                maxAgeAttributeProvided = true;
                long age = val.toInt();
                if (age <= 0) {
                    remove = true;
                    expiresAt = now;
                } else {
                    expiresAt = now + static_cast<int64_t>(age);
                }
            } else if (key.equalsIgnoreCase("Expires") && !maxAgeAttributeProvided) {
                int64_t parsedExpiry = -1;
                if (parseHttpDate(val, &parsedExpiry))
                    expiresAt = parsedExpiry;
            }
        }
        pos = next;
    }

    if (!normalizeCookieDomain(cookie.domain, request->getHost(), domainAttributeProvided, &cookie.hostOnly))
        return;
    if (!cookie.path.startsWith("/"))
        cookie.path = "/" + cookie.path;
    size_t payloadSize = cookie.name.length() + cookie.value.length() + cookie.domain.length() + cookie.path.length();
    if (payloadSize > kMaxCookieBytes)
        return;
    cookie.expiresAt = expiresAt;
    cookie.createdAt = now;
    cookie.lastAccessAt = now;
    if (isCookieExpired(cookie, now))
        remove = true;

    lock();
    purgeExpiredCookies(now);
    for (auto it = _cookies.begin(); it != _cookies.end();) {
        if (it->name.equalsIgnoreCase(cookie.name) && it->domain.equalsIgnoreCase(cookie.domain) &&
            it->path.equals(cookie.path)) {
            it = _cookies.erase(it);
        } else {
            ++it;
        }
    }
    if (!remove) {
        if (_cookies.size() >= kMaxCookieCount)
            evictOneCookieLocked();
        _cookies.push_back(cookie);
    }
    unlock();
}
