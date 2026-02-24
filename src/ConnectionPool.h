#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include <Arduino.h>
#include <memory>
#include <vector>
#include "AsyncTransport.h"
#include "HttpRequest.h"
#include "HttpResponse.h"

class AsyncHttpClient;

class ConnectionPool {
  public:
    struct PooledConnection {
        AsyncTransport* transport = nullptr;
        String host;
        uint16_t port = 0;
        bool secure = false;
        AsyncHttpTLSConfig tlsConfig;
        uint32_t lastUsedMs = 0;
    };

    explicit ConnectionPool(AsyncHttpClient* client);
    ~ConnectionPool();

    AsyncTransport* checkoutPooledTransport(const AsyncHttpRequest* request, const AsyncHttpTLSConfig& tlsCfg,
                                            bool keepAliveEnabled);
    void releaseConnectionToPool(AsyncTransport* transport, const AsyncHttpRequest* request,
                                 const AsyncHttpTLSConfig& tlsCfg);
    void dropPooledTransport(AsyncTransport* transport, bool closeTransport);
    void pruneIdleConnections(bool keepAliveEnabled, uint32_t keepAliveIdleMs);
    void dropAll();

    static bool shouldRecycleTransport(const AsyncHttpRequest* request,
                                       const std::shared_ptr<AsyncHttpResponse>& response, AsyncTransport* transport,
                                       bool responseProcessed, bool requestKeepAlive, bool serverRequestedClose,
                                       bool chunked, bool chunkedComplete, size_t expectedContentLength,
                                       size_t receivedContentLength, bool keepAliveEnabled);

  private:
    void lock() const;
    void unlock() const;
    bool tlsConfigEquals(const AsyncHttpTLSConfig& a, const AsyncHttpTLSConfig& b) const;

    AsyncHttpClient* _client = nullptr;
    std::vector<PooledConnection> _idleConnections;
};

#endif // CONNECTION_POOL_H
