#include "ConnectionPool.h"
#include "AsyncHttpClient.h"

ConnectionPool::ConnectionPool(AsyncHttpClient* client) : _client(client) {}

ConnectionPool::~ConnectionPool() {
    std::vector<AsyncTransport*> toDelete;
    lock();
    for (auto& pooled : _idleConnections) {
        if (pooled.transport)
            toDelete.push_back(pooled.transport);
    }
    _idleConnections.clear();
    unlock();
    for (auto* transport : toDelete) {
        transport->close(true);
        delete transport;
    }
}

void ConnectionPool::lock() const {
    if (_client)
        _client->lock();
}

void ConnectionPool::unlock() const {
    if (_client)
        _client->unlock();
}

bool ConnectionPool::tlsConfigEquals(const AsyncHttpTLSConfig& a, const AsyncHttpTLSConfig& b) const {
    return a.caCert == b.caCert && a.clientCert == b.clientCert && a.clientPrivateKey == b.clientPrivateKey &&
           a.fingerprint == b.fingerprint && a.insecure == b.insecure && a.handshakeTimeoutMs == b.handshakeTimeoutMs;
}

AsyncTransport* ConnectionPool::checkoutPooledTransport(const AsyncHttpRequest* request,
                                                        const AsyncHttpTLSConfig& tlsCfg, bool keepAliveEnabled) {
    if (!keepAliveEnabled || !request)
        return nullptr;
    AsyncTransport* found = nullptr;
    lock();
    for (auto it = _idleConnections.begin(); it != _idleConnections.end();) {
        bool removeEntry = false;
        if (!it->transport || !it->transport->canSend()) {
            removeEntry = true;
        } else {
            bool match = it->host.equalsIgnoreCase(request->getHost()) && it->port == request->getPort() &&
                         it->secure == request->isSecure();
            if (match && it->secure)
                match = tlsConfigEquals(it->tlsConfig, tlsCfg);
            if (match) {
                found = it->transport;
                _idleConnections.erase(it);
                break;
            }
        }
        if (removeEntry) {
            AsyncTransport* toDelete = it->transport;
            it = _idleConnections.erase(it);
            if (toDelete) {
                toDelete->close(true);
                delete toDelete;
            }
            continue;
        }
        ++it;
    }
    unlock();
    if (found) {
        found->setDataHandler(nullptr, nullptr);
        found->setDisconnectHandler(nullptr, nullptr);
        found->setErrorHandler(nullptr, nullptr);
        found->setTimeoutHandler(nullptr, nullptr);
    }
    return found;
}

void ConnectionPool::dropPooledTransport(AsyncTransport* transport, bool closeTransport) {
    if (!transport)
        return;
    bool removed = false;
    lock();
    for (auto it = _idleConnections.begin(); it != _idleConnections.end(); ++it) {
        if (it->transport == transport) {
            _idleConnections.erase(it);
            removed = true;
            break;
        }
    }
    unlock();
    if (removed && closeTransport) {
        transport->close(true);
        delete transport;
    }
}

void ConnectionPool::pruneIdleConnections(bool keepAliveEnabled, uint32_t keepAliveIdleMs) {
    if (!keepAliveEnabled)
        return;
    uint32_t now = millis();
    std::vector<AsyncTransport*> staleTransports;
    lock();
    for (auto it = _idleConnections.begin(); it != _idleConnections.end();) {
        bool stale = !keepAliveEnabled || !it->transport || !it->transport->canSend() ||
                     (now - it->lastUsedMs) > keepAliveIdleMs;
        if (stale) {
            AsyncTransport* toDelete = it->transport;
            it = _idleConnections.erase(it);
            if (toDelete)
                staleTransports.push_back(toDelete);
        } else {
            ++it;
        }
    }
    unlock();
    for (auto* t : staleTransports) {
        t->close(true);
        delete t;
    }
}

void ConnectionPool::dropAll() {
    std::vector<AsyncTransport*> toDelete;
    lock();
    for (auto& pooled : _idleConnections) {
        if (pooled.transport)
            toDelete.push_back(pooled.transport);
    }
    _idleConnections.clear();
    unlock();
    for (auto* transport : toDelete) {
        transport->close(true);
        delete transport;
    }
}

bool ConnectionPool::shouldRecycleTransport(const AsyncHttpRequest* request,
                                            const std::shared_ptr<AsyncHttpResponse>& response,
                                            AsyncTransport* transport, bool responseProcessed, bool requestKeepAlive,
                                            bool serverRequestedClose, bool chunked, bool chunkedComplete,
                                            size_t expectedContentLength, size_t receivedContentLength,
                                            bool keepAliveEnabled) {
    if (!keepAliveEnabled || !request || !transport || !responseProcessed)
        return false;
    if (!response || response->getStatusCode() == 0)
        return false;
    if (!requestKeepAlive || serverRequestedClose)
        return false;
    if (chunked && !chunkedComplete)
        return false;
    if (!chunked && expectedContentLength > 0 && receivedContentLength < expectedContentLength)
        return false;
    if (!transport->canSend())
        return false;
    return true;
}

void ConnectionPool::releaseConnectionToPool(AsyncTransport* transport, const AsyncHttpRequest* request,
                                             const AsyncHttpTLSConfig& tlsCfg) {
    if (!transport || !request)
        return;
    PooledConnection pooled;
    pooled.transport = transport;
    pooled.host = request->getHost();
    pooled.port = request->getPort();
    pooled.secure = request->isSecure();
    pooled.tlsConfig = tlsCfg;
    pooled.lastUsedMs = millis();

    pooled.transport->setConnectHandler(nullptr, nullptr);
    pooled.transport->setTimeoutHandler(nullptr, nullptr);
    pooled.transport->setDataHandler(
        [](void* arg, AsyncTransport* t, void* data, size_t len) {
            (void)data;
            (void)len;
            auto self = static_cast<ConnectionPool*>(arg);
            self->dropPooledTransport(t, true);
        },
        this);
    pooled.transport->setDisconnectHandler(
        [](void* arg, AsyncTransport* t) {
            auto self = static_cast<ConnectionPool*>(arg);
            self->dropPooledTransport(t, true);
        },
        this);
    pooled.transport->setErrorHandler(
        [](void* arg, AsyncTransport* t, HttpClientError, const char*) {
            auto self = static_cast<ConnectionPool*>(arg);
            self->dropPooledTransport(t, true);
        },
        this);

    lock();
    _idleConnections.push_back(pooled);
    unlock();
}
