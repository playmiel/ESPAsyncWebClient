#ifndef ASYNC_TRANSPORT_H
#define ASYNC_TRANSPORT_H

#include <functional>
#include <cstddef>
#include <stdint.h>
#include "HttpCommon.h"

class AsyncTransport {
  public:
    typedef std::function<void(void*, AsyncTransport*)> ConnectHandler;
    typedef std::function<void(void*, AsyncTransport*, void*, size_t)> DataHandler;
    typedef std::function<void(void*, AsyncTransport*)> DisconnectHandler;
    typedef std::function<void(void*, AsyncTransport*, HttpClientError, const char*)> ErrorHandler;
    typedef std::function<void(void*, AsyncTransport*, uint32_t)> TimeoutHandler;

    virtual ~AsyncTransport() {}

    virtual void setConnectHandler(ConnectHandler handler, void* arg) = 0;
    virtual void setDataHandler(DataHandler handler, void* arg) = 0;
    virtual void setDisconnectHandler(DisconnectHandler handler, void* arg) = 0;
    virtual void setErrorHandler(ErrorHandler handler, void* arg) = 0;
    virtual void setTimeout(uint32_t timeoutMs) = 0;
    virtual void setTimeoutHandler(TimeoutHandler handler, void* arg) = 0;
    virtual bool connect(const char* host, uint16_t port) = 0;
    virtual size_t write(const char* data, size_t len) = 0;
    virtual bool canSend() const = 0;
    virtual void close(bool now = false) = 0;
    virtual bool isSecure() const = 0;
    virtual bool isHandshaking() const = 0;
    virtual uint32_t getHandshakeStartMs() const = 0;
    virtual uint32_t getHandshakeTimeoutMs() const = 0;
};

AsyncTransport* createTcpTransport();
AsyncTransport* createTlsTransport(const AsyncHttpTLSConfig& config);

#endif // ASYNC_TRANSPORT_H
