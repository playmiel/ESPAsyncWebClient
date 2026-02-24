#include "AsyncTransport.h"
#include <Arduino.h>
#include <AsyncTCP.h>

class AsyncTcpTransport : public AsyncTransport {
  public:
    AsyncTcpTransport();
    ~AsyncTcpTransport() override;

    void setConnectHandler(ConnectHandler handler, void* arg) override {
        _connectHandler = handler;
        _connectArg = arg;
    }
    void setDataHandler(DataHandler handler, void* arg) override {
        _dataHandler = handler;
        _dataArg = arg;
    }
    void setDisconnectHandler(DisconnectHandler handler, void* arg) override {
        _disconnectHandler = handler;
        _disconnectArg = arg;
    }
    void setErrorHandler(ErrorHandler handler, void* arg) override {
        _errorHandler = handler;
        _errorArg = arg;
    }
    void setTimeout(uint32_t timeoutMs) override {
#if ASYNC_TCP_HAS_TIMEOUT
        if (_client)
            _client->setTimeout(timeoutMs);
#else
        (void)timeoutMs;
#endif
    }
    void setTimeoutHandler(TimeoutHandler handler, void* arg) override {
#if ASYNC_TCP_HAS_TIMEOUT
        _timeoutHandler = handler;
        _timeoutArg = arg;
        if (_client) {
            _client->onTimeout(
                [](void* arg, AsyncClient* c, uint32_t time) {
                    (void)c;
                    auto self = static_cast<AsyncTcpTransport*>(arg);
                    if (self->_timeoutHandler)
                        self->_timeoutHandler(self->_timeoutArg, self, time);
                },
                this);
        }
#else
        (void)handler;
        (void)arg;
#endif
    }
    bool connect(const char* host, uint16_t port) override {
        if (!_client)
            return false;
        return _client->connect(host, port);
    }
    size_t write(const char* data, size_t len) override {
        if (!_client)
            return 0;
        return _client->write(data, len);
    }
    bool canSend() const override {
        return _client && _client->canSend();
    }
    void close(bool now = false) override {
        (void)now;
        if (_client)
            _client->close();
    }
    bool isSecure() const override {
        return false;
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

  private:
    static void handleConnectThunk(void* arg, AsyncClient* client) {
        (void)client;
        auto self = static_cast<AsyncTcpTransport*>(arg);
        if (self->_connectHandler)
            self->_connectHandler(self->_connectArg, self);
    }
    static void handleDataThunk(void* arg, AsyncClient* client, void* data, size_t len) {
        (void)client;
        auto self = static_cast<AsyncTcpTransport*>(arg);
        if (self->_dataHandler)
            self->_dataHandler(self->_dataArg, self, data, len);
    }
    static void handleDisconnectThunk(void* arg, AsyncClient* client) {
        (void)client;
        auto self = static_cast<AsyncTcpTransport*>(arg);
        if (self->_disconnectHandler)
            self->_disconnectHandler(self->_disconnectArg, self);
    }
    static void handleErrorThunk(void* arg, AsyncClient* client, int8_t error) {
        (void)client;
        auto self = static_cast<AsyncTcpTransport*>(arg);
        if (self->_errorHandler)
            self->_errorHandler(self->_errorArg, self, CONNECTION_FAILED, "Network error");
        (void)error;
    }

    AsyncClient* _client;
    ConnectHandler _connectHandler = nullptr;
    DataHandler _dataHandler = nullptr;
    DisconnectHandler _disconnectHandler = nullptr;
    ErrorHandler _errorHandler = nullptr;
    TimeoutHandler _timeoutHandler = nullptr;
    void* _connectArg = nullptr;
    void* _dataArg = nullptr;
    void* _disconnectArg = nullptr;
    void* _errorArg = nullptr;
    void* _timeoutArg = nullptr;
};

AsyncTcpTransport::AsyncTcpTransport() {
    _client = new AsyncClient();
    _client->onConnect(handleConnectThunk, this);
    _client->onData(handleDataThunk, this);
    _client->onDisconnect(handleDisconnectThunk, this);
    _client->onError(handleErrorThunk, this);
}

AsyncTcpTransport::~AsyncTcpTransport() {
    if (_client) {
        _client->onConnect(nullptr, nullptr);
        _client->onData(nullptr, nullptr);
        _client->onDisconnect(nullptr, nullptr);
        _client->onError(nullptr, nullptr);
        _client->onTimeout(nullptr, nullptr);
        _client->close();
        delete _client;
        _client = nullptr;
    }
}

AsyncTransport* createTcpTransport() {
    return new AsyncTcpTransport();
}
